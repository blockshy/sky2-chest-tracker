<#
.SYNOPSIS
精简发行包共用的内容归属、迁移和文件操作保护。
.DESCRIPTION
游戏目录不保存安装收据。固定产品/入口路径配合包内实际载荷及已发布 SHA-256
白名单识别文件；旧收据只用于清理自己的旧记录，绝不能授权未知 DLL。全部计划
先只读核验，再经 ShouldProcess 和互斥锁执行；每次写入/删除前重新核对内容。
#>
$script:Sky2SupportedExeHash = 'd8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf'
$script:Sky2Loader974Hash = '031a3e5576d91dce1e438d36b9a3d462c7334ab4791990a8ff1e3ddc0e132daf'

function Assert-Sky2PlainPath {
    param([string]$Path)
    # 检查完整父链，拒绝借助目录联接、符号链接或其他重解析点越过安装边界。
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "发现链接或重解析点，未操作：$cursor" }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}
function Get-Sky2GameRoot {
    param([string]$GamePath, [switch]$CheckVersion)
    Assert-Sky2PlainPath $GamePath
    $resolved = (Resolve-Path -LiteralPath $GamePath).Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) { throw '游戏目录不存在。' }
    if (Get-Process -Name sora_2nd -ErrorAction SilentlyContinue) { throw '请先正常退出游戏。' }
    if ($CheckVersion) {
        $exe = Join-Path $resolved 'sora_2nd.exe'
        Assert-Sky2PlainPath $exe
        if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $script:Sky2SupportedExeHash) { throw '游戏 EXE 与适配版本不符。' }
    }
    return $resolved
}
function Read-Sky2Json {
    param([string]$Path)
    Assert-Sky2PlainPath $Path
    try { return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json }
    catch { throw "安装包清单无法读取：$Path" }
}
function Get-Sky2Layout {
    param([string]$Type)
    switch ($Type) {
        'standalone-proxy' { return [PSCustomObject]@{ Type=$Type; Product='Sky2ChestTracker'; Binary='xinput1_4.dll'; License='Sky2ChestTracker/LICENSES.txt'; Legacy='Sky2ChestTracker'; Receipt='Sky2ChestTracker/install.json' } }
        'asi-plugin' { return [PSCustomObject]@{ Type=$Type; Product='Sky2ChestTracker'; Binary='plugins/Sky2ChestTracker.asi'; License='plugins/Sky2ChestTracker/LICENSES.txt'; Legacy='Sky2Mods/Sky2ChestTracker'; Receipt='Sky2Mods/Sky2ChestTracker/plugin-install.json' } }
        'asi-loader' { return [PSCustomObject]@{ Type=$Type; Product='UltimateASILoader'; Binary='xinput1_4.dll'; License='plugins/Sky2ChestTracker/UltimateASILoader.LICENSE.txt'; Legacy='Sky2ModLoader'; Receipt='Sky2ModLoader/install.json' } }
        default { throw '安装包类型不受支持。' }
    }
}
function Get-Sky2Package {
    param([string]$PackageRoot, [string]$Type)
    Assert-Sky2PlainPath $PackageRoot
    $layout = Get-Sky2Layout $Type
    $manifest = Read-Sky2Json (Join-Path $PackageRoot 'installer/manifest.json')
    if ($manifest.schema -ne 2 -or $manifest.type -cne $Type -or $manifest.product -cne $layout.Product -or
        $manifest.path -cne $layout.Binary -or $manifest.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
        $manifest.exe_sha256 -ne $script:Sky2SupportedExeHash -or [string]::IsNullOrWhiteSpace($manifest.version)) { throw '安装包的类型、产品、路径、版本或哈希无效。' }
    $expected = @($layout.Binary, $layout.License)
    $files = @($manifest.files)
    if ($files.Count -ne 2) { throw '安装包载荷清单不完整。' }
    foreach ($relative in $expected) {
        $payloadMatches = @($files | Where-Object { $_.path -ceq $relative })
        if ($payloadMatches.Count -ne 1 -or $payloadMatches[0].sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw '安装包载荷路径或哈希无效。' }
        $source = Join-Path $PackageRoot ('dist/' + $relative)
        Assert-Sky2PlainPath $source
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $payloadMatches[0].sha256) { throw "安装包文件缺失或哈希不一致：$relative" }
    }
    if (($files | Where-Object { $_.path -ceq $layout.Binary }).sha256 -ne $manifest.sha256) { throw '安装包入口与载荷清单不一致。' }
    if ($Type -eq 'asi-loader' -and ($manifest.version -ne '9.7.4' -or $manifest.sha256 -ne $script:Sky2Loader974Hash)) { throw 'Loader 不是已核验的官方 9.7.4 x64 文件。' }
    $known = Read-Sky2Json (Join-Path $PackageRoot 'installer/known-files.json')
    if ($known.schema -ne 1) { throw '历史文件清单格式不受支持。' }
    foreach ($entry in @($known.files)) {
        # 清理白名单的路径必须是普通相对路径；后续还按固定产品和固定旧目录过滤。
        if ($entry.path -notmatch '^[A-Za-z0-9_./-]+$' -or $entry.path -match '(^|/)\.\.(/|$)' -or $entry.path.StartsWith('/') -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw '历史文件清单存在非法路径或哈希。' }
    }
    return [PSCustomObject]@{ Root=$PackageRoot; Layout=$layout; Manifest=$manifest; Known=@($known.files); Files=$files }
}
function Test-Sky2KnownHash {
    param($Package, [string]$Type, [string]$Path, [string]$Hash)
    $layout = Get-Sky2Layout $Type
    if ($Type -eq $Package.Layout.Type -and @($Package.Files | Where-Object { $_.path -ceq $Path -and $_.sha256 -eq $Hash }).Count) { return $true }
    return @($Package.Known | Where-Object { $_.type -ceq $Type -and $_.product -ceq $layout.Product -and $_.path -ceq $Path -and $_.sha256 -eq $Hash }).Count -gt 0
}
function Get-Sky2FileHash {
    param([string]$Path)
    Assert-Sky2PlainPath $Path
    if (Test-Path -LiteralPath $Path -PathType Container) { throw "目标路径被目录占用：$Path" }
    if (Test-Path -LiteralPath $Path -PathType Leaf) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
    return $null
}
function Assert-Sky2Unchanged {
    param([string]$Path, [AllowNull()][string]$Hash)
    $actual = Get-Sky2FileHash $Path
    if (($Hash -and $actual -ne $Hash) -or (-not $Hash -and $actual)) { throw "文件在检查后发生变化，未继续操作：$Path" }
}
function Get-Sky2InstallPlan {
    param($Package, [string]$GameRoot)
    $layout = $Package.Layout
    if ($layout.Type -eq 'standalone-proxy') {
        $asi = Join-Path $GameRoot 'plugins/Sky2ChestTracker.asi'
        Assert-Sky2PlainPath $asi
        if (Test-Path -LiteralPath $asi) { throw '已有宝箱 ASI，不能同时安装独立版。' }
    }
    if ($layout.Type -eq 'asi-plugin') {
        $loader = Get-Sky2FileHash (Join-Path $GameRoot 'xinput1_4.dll')
        if (-not $loader -or -not (Test-Sky2KnownHash $Package 'asi-loader' 'xinput1_4.dll' $loader)) { throw '请先安装已支持的公共 Loader；未知同名入口不会被接管。' }
    }
    $plan = @()
    foreach ($file in $Package.Files) {
        $target = Join-Path $GameRoot $file.path
        $old = Get-Sky2FileHash $target
        $recognized = -not $old -or (Test-Sky2KnownHash $Package $layout.Type $file.path $old)
        # 只有显式运行 Loader 安装器时允许把已发布的独立代理换成公共入口。
        if (-not $recognized -and $layout.Type -eq 'asi-loader' -and $file.path -ceq $layout.Binary) {
            $recognized = Test-Sky2KnownHash $Package 'standalone-proxy' 'xinput1_4.dll' $old
        }
        if (-not $recognized) { throw "已有未知同名文件，未覆盖：$target" }
        $plan += [PSCustomObject]@{ Source=(Join-Path $Package.Root ('dist/' + $file.path)); Target=$target; Hash=$file.sha256; OldHash=$old; Binary=($file.path -ceq $layout.Binary) }
    }
    return $plan
}
function Get-Sky2CleanupPlan {
    param($Package, [string]$GameRoot)
    $layout = $Package.Layout
    $plan = @()
    # 仅允许历史版本真实发布过的固定文档名；未来白名单添加新版本 LICENSES.txt
    # 或二进制时，也不能让旧文件清理误删刚安装的当前载荷及任何运行数据。
    $legacyNames = @('LICENSE','LICENSE.txt','THIRD_PARTY_NOTICES.md','README.md','CHANGELOG.md','CONTRIBUTING.md',
        'licenses/Dear-ImGui.txt','licenses/MinHook.txt','licenses/ED9ModManager.txt',
        'docs/BUILDING.md','docs/ARCHITECTURE.md','docs/TESTING.md')
    $allowed = @($legacyNames | ForEach-Object { $layout.Legacy + '/' + $_ })
    $paths = @($Package.Known | Where-Object { $_.type -ceq $layout.Type -and $_.product -ceq $layout.Product -and $allowed -ccontains $_.path } | ForEach-Object { $_.path } | Sort-Object -Unique)
    foreach ($relative in $paths) {
        $target = Join-Path $GameRoot $relative
        $hash = Get-Sky2FileHash $target
        if ($hash -and (Test-Sky2KnownHash $Package $layout.Type $relative $hash)) { $plan += [PSCustomObject]@{ Target=$target; Hash=$hash } }
    }
    $receiptPath = Join-Path $GameRoot $layout.Receipt
    $receiptHash = Get-Sky2FileHash $receiptPath
    if ($receiptHash) {
        # 动态收据无法逐字节预登记，只接受固定旧位置、完整旧格式以及已发布入口
        # 指纹的组合。它仅授权删除这份收据，从不授权覆盖/删除收据声称的 DLL。
        try {
            $record = Read-Sky2Json $receiptPath
            $binaryHash = $record.sha256
            if ($layout.Type -eq 'standalone-proxy') { $binaryHash = $record.dll_sha256 }
            $valid = $record.product -ceq $layout.Product -and -not [string]::IsNullOrWhiteSpace($record.version) -and $binaryHash -match '^[0-9a-fA-F]{64}$' -and (Test-Sky2KnownHash $Package $layout.Type $layout.Binary $binaryHash)
            if ($record.exe_sha256 -and $record.exe_sha256 -ne $script:Sky2SupportedExeHash) { $valid = $false }
            if ($layout.Type -ne 'standalone-proxy') { $valid = $valid -and $record.schema -eq 1 -and $record.type -ceq $layout.Type -and $record.path -ceq $layout.Binary }
            if ($valid) { $plan += [PSCustomObject]@{ Target=$receiptPath; Hash=$receiptHash } }
        } catch { Write-Warning "无法确认旧记录归属，保留：$receiptPath" }
    }
    return $plan
}
function Assert-Sky2ReturnRecord {
    param([string]$Path)
    # 这里只识别固定返程容器，不替代运行时地图合法性判断。迁移要求魔数、版本、
    # 游戏指纹、长度和 CRC 全部匹配；历史文件名还必须对应记录自己的票据编号。
    Assert-Sky2PlainPath $Path
    if ((Get-Item -LiteralPath $Path -Force).Length -ne 144) { throw "旧返程数据长度无法确认，原文件保留：$Path" }
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ne 144 -or [Text.Encoding]::ASCII.GetString($bytes,0,8) -cne 'SKY2RET1' -or
        [BitConverter]::ToUInt32($bytes,8) -ne 1 -or [BitConverter]::ToUInt32($bytes,12) -ne 144 -or
        [BitConverter]::ToString($bytes,16,32).Replace('-','').ToLowerInvariant() -ne $script:Sky2SupportedExeHash) {
        throw "旧返程数据格式无法确认，原文件保留：$Path"
    }
    [uint32]$crc = [uint32]::MaxValue
    for ($index=0; $index -lt 140; $index++) {
        $crc = $crc -bxor [uint32]$bytes[$index]
        for ($bit=0; $bit -lt 8; $bit++) {
            if ($crc -band 1) { $crc = ($crc -shr 1) -bxor [uint32]3988292384 }
            else { $crc = $crc -shr 1 }
        }
    }
    $crc = $crc -bxor [uint32]::MaxValue
    $ticket = [BitConverter]::ToUInt64($bytes,56)
    $name = [IO.Path]::GetFileName($Path)
    if ($crc -ne [BitConverter]::ToUInt32($bytes,140) -or $ticket -eq 0 -or
        ($name -cne 'revisit-return.dat' -and $name -ine ($ticket.ToString('x16') + '.dat'))) {
        throw "旧返程数据校验失败，原文件保留：$Path"
    }
}
function Get-Sky2MigrationPlan {
    param($Package, [string]$GameRoot)
    if ($Package.Layout.Type -ne 'asi-plugin') { return @() }
    $old = Join-Path $GameRoot 'Sky2Mods/Sky2ChestTracker'
    $new = Join-Path $GameRoot 'plugins/Sky2ChestTracker'
    Assert-Sky2PlainPath $old
    $binaryHash = Get-Sky2FileHash (Join-Path $GameRoot $Package.Layout.Binary)
    # 没有已识别的插件入口时，不认领遗留数据目录；独立版数据从不参与迁移。
    if (-not $binaryHash -or -not (Test-Sky2KnownHash $Package 'asi-plugin' $Package.Layout.Binary $binaryHash)) { return @() }
    $relatives = @('tracker.log', 'revisit-return.dat')
    $history = Join-Path $old 'revisit-history'
    Assert-Sky2PlainPath $history
    if (Test-Path -LiteralPath $history -PathType Leaf) { throw '旧返程历史目录被文件占用。' }
    if (Test-Path -LiteralPath $history) {
        foreach ($item in Get-ChildItem -LiteralPath $history -Force) {
            Assert-Sky2PlainPath $item.FullName
            if (-not $item.PSIsContainer -and $item.Name -cmatch '^[0-9a-fA-F]{16}\.dat$') { $relatives += 'revisit-history/' + $item.Name }
        }
    }
    $plan = @()
    foreach ($relative in $relatives) {
        $source = Join-Path $old $relative
        $hash = Get-Sky2FileHash $source
        if (-not $hash) { continue }
        if ($relative -ne 'tracker.log') { Assert-Sky2ReturnRecord $source }
        $target = Join-Path $new $relative
        $destinationHash = Get-Sky2FileHash $target
        if ($destinationHash -and $destinationHash -ne $hash) { throw "新旧 ASI 数据冲突，未覆盖。请先选择保留哪份数据：$relative" }
        $plan += [PSCustomObject]@{ Source=$source; Target=$target; Hash=$hash; OldHash=$destinationHash }
    }
    return $plan
}
function Enter-Sky2Mutex {
    param([string]$GameRoot)
    # 互斥锁不落盘；同一台机器、同一路径下的三个脚本不能同时修改公共入口。
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $id = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($GameRoot.ToLowerInvariant()))).Replace('-', '') }
    finally { $hasher.Dispose() }
    $mutex = [Threading.Mutex]::new($false, ('Local\Sky2Installer-' + $id))
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (-not $locked) { $mutex.Dispose(); throw '已有安装或卸载正在操作此游戏目录。' }
    return $mutex
}
function Backup-Sky2File {
    param([string]$PackageRoot, [string]$Source, [string]$Hash)
    $backupRoot = Join-Path $PackageRoot 'backups'
    $backup = Join-Path $backupRoot ($Hash.ToLowerInvariant() + [IO.Path]::GetExtension($Source))
    Assert-Sky2PlainPath $backup
    if (Test-Path -LiteralPath $backup) { Assert-Sky2Unchanged $backup $Hash; return }
    Assert-Sky2Unchanged $Source $Hash
    [IO.Directory]::CreateDirectory($backupRoot) | Out-Null
    [IO.File]::Copy($Source, $backup, $false)
    Assert-Sky2Unchanged $backup $Hash
}
function Set-Sky2FileAtomically {
    param([string]$Source, [string]$Target, [string]$Hash, [AllowNull()][string]$OldHash)
    Assert-Sky2Unchanged $Source $Hash
    Assert-Sky2Unchanged $Target $OldHash
    if ($Hash -eq $OldHash) { return }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Target)) | Out-Null
    $temporary = $Target + '.install-' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::Copy($Source, $temporary, $false)
        Assert-Sky2Unchanged $temporary $Hash
        Assert-Sky2Unchanged $Target $OldHash
        # 允许系统跳过 ACL/备用流的元数据合并错误；内容替换仍是单次原子操作，
        # 不忽略只读、占用或文件写入失败，也不使用先删后复制。
        if ($OldHash) { [IO.File]::Replace($temporary, $Target, [NullString]::Value, $true) }
        else { [IO.File]::Move($temporary, $Target) }
        Assert-Sky2Unchanged $Target $Hash
    } finally {
        # 只删除本次生成且内容仍匹配的临时文件；不按通配符清理其他进程的文件。
        if ((Get-Sky2FileHash $temporary) -eq $Hash) { Remove-Item -LiteralPath $temporary }
    }
}
function Remove-Sky2EmptyDirectories {
    param([string]$GameRoot, [string[]]$Relatives)
    foreach ($relative in $Relatives) {
        $target = [IO.Path]::GetFullPath((Join-Path $GameRoot $relative))
        if (-not $target.StartsWith($GameRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw '目录清理越界。' }
        Assert-Sky2PlainPath $target
        if ((Test-Path -LiteralPath $target -PathType Container) -and @(Get-ChildItem -LiteralPath $target -Force).Count -eq 0) { [IO.Directory]::Delete($target, $false) }
    }
}
function Invoke-Sky2Install {
    param($Package, [string]$GameRoot, $Plan, $Cleanup, $Migration, [switch]$NoBackup)
    $mutex = Enter-Sky2Mutex $GameRoot
    try {
        # 拿到锁之后重读门禁与计划所涉及的全部文件，避免预检和确认间有人换入文件。
        if (Get-Process -Name sora_2nd -ErrorAction SilentlyContinue) { throw '请先正常退出游戏。' }
        $null = Get-Sky2InstallPlan $Package $GameRoot
        foreach ($file in @($Plan)) { Assert-Sky2Unchanged $file.Source $file.Hash; Assert-Sky2Unchanged $file.Target $file.OldHash }
        foreach ($file in @($Cleanup)) { Assert-Sky2Unchanged $file.Target $file.Hash }
        foreach ($file in @($Migration)) { Assert-Sky2Unchanged $file.Source $file.Hash; Assert-Sky2Unchanged $file.Target $file.OldHash }
        if (-not $NoBackup) {
            foreach ($file in @($Plan)) { if ($file.OldHash -and $file.OldHash -ne $file.Hash) { Backup-Sky2File $Package.Root $file.Target $file.OldHash } }
        }
        # 先复制迁移数据和许可，再提交二进制。迁移源在二进制校验成功前始终保留；
        # 即使中途复制失败，旧插件仍可使用原始记录，下次执行可识别相同内容重试。
        foreach ($file in @($Migration)) { Set-Sky2FileAtomically $file.Source $file.Target $file.Hash $file.OldHash }
        foreach ($file in @($Plan | Sort-Object Binary)) { Set-Sky2FileAtomically $file.Source $file.Target $file.Hash $file.OldHash }
        foreach ($file in @($Migration)) { Assert-Sky2Unchanged $file.Source $file.Hash; Assert-Sky2Unchanged $file.Target $file.Hash; Remove-Item -LiteralPath $file.Source }
        foreach ($file in @($Cleanup)) { Assert-Sky2Unchanged $file.Target $file.Hash; Remove-Item -LiteralPath $file.Target }
        $legacy = $Package.Layout.Legacy
        Remove-Sky2EmptyDirectories $GameRoot @(($legacy + '/licenses'), ($legacy + '/docs'), ($legacy + '/revisit-history'), $legacy)
        if ($Package.Layout.Type -eq 'asi-plugin') { Remove-Sky2EmptyDirectories $GameRoot @('Sky2Mods') }
    } finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
}
function Assert-Sky2NoAsiPlugins {
    param([string]$GameRoot)
    # 共享 Loader 不能由宝箱卸载间接删除。独立卸载时保守扫描所有子目录；遇到
    # 无法检查的链接同样停止，防止误判其他插件没有依赖公共 Loader。
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($GameRoot)
    while ($pending.Count) {
        $directory = $pending.Pop()
        Assert-Sky2PlainPath $directory
        foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
            Assert-Sky2PlainPath $item.FullName
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
            elseif ($item.Extension -ieq '.asi') { throw "仍有 ASI 插件，未卸载公共 Loader：$($item.FullName)" }
        }
    }
}
function Get-Sky2UninstallPlan {
    param($Package, [string]$GameRoot)
    $layout = $Package.Layout
    $target = Join-Path $GameRoot $layout.Binary
    $hash = Get-Sky2FileHash $target
    if ($hash -and -not (Test-Sky2KnownHash $Package $layout.Type $layout.Binary $hash)) { throw '已有未知同名文件，未删除。' }
    if ($layout.Type -eq 'asi-loader') { Assert-Sky2NoAsiPlugins $GameRoot }
    $plan = @()
    if ($hash) { $plan += [PSCustomObject]@{ Target=$target; Hash=$hash } }
    $license = Join-Path $GameRoot $layout.License
    $licenseHash = Get-Sky2FileHash $license
    if ($licenseHash -and (Test-Sky2KnownHash $Package $layout.Type $layout.License $licenseHash)) { $plan += [PSCustomObject]@{ Target=$license; Hash=$licenseHash } }
    $plan += @(Get-Sky2CleanupPlan $Package $GameRoot)
    return $plan
}
function Invoke-Sky2Uninstall {
    param($Package, [string]$GameRoot, $Plan)
    $mutex = Enter-Sky2Mutex $GameRoot
    try {
        if (Get-Process -Name sora_2nd -ErrorAction SilentlyContinue) { throw '请先正常退出游戏。' }
        $null = Get-Sky2UninstallPlan $Package $GameRoot
        foreach ($file in @($Plan)) { Assert-Sky2Unchanged $file.Target $file.Hash }
        foreach ($file in @($Plan)) { Assert-Sky2Unchanged $file.Target $file.Hash; Remove-Item -LiteralPath $file.Target }
        $legacy = $Package.Layout.Legacy
        Remove-Sky2EmptyDirectories $GameRoot @(($legacy + '/licenses'), ($legacy + '/docs'), ($legacy + '/revisit-history'), $legacy)
        if ($Package.Layout.Type -eq 'asi-plugin') { Remove-Sky2EmptyDirectories $GameRoot @('Sky2Mods') }
        if ($Package.Layout.Type -ne 'standalone-proxy') { Remove-Sky2EmptyDirectories $GameRoot @('plugins/Sky2ChestTracker', 'plugins') }
    } finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
}
