"""读取原生版本权威头，为资源生成器提供一致且默认拒绝未知版本的检查。"""
from pathlib import Path
import hashlib
import re


VERSION_HEADER = Path(__file__).resolve().parents[1] / "native/game_version.h"
_FIELDS = {
    "exe_sha256": ("kSupportedExeSha256", r"[0-9a-f]{64}"),
    "file_version": ("kSupportedFileVersion", r"[0-9]+(?:\.[0-9]+){3}"),
    "steam_build_id": ("kSteamBuildId", r"[0-9]+"),
    "previous_exe_sha256": ("kPreviousExeSha256", r"[0-9a-f]{64}"),
}


def read_game_version(header: Path = VERSION_HEADER) -> dict[str, str]:
    """严格读取唯一声明；字段缺失、重复或格式改变时停止，而不是使用旧默认值。"""
    source = header.read_text(encoding="utf-8-sig")
    result = {}
    for key, (constant, pattern) in _FIELDS.items():
        matches = re.findall(
            rf'^inline constexpr char {constant}\[\] = "([^"]*)";$',
            source, flags=re.MULTILINE)
        if len(matches) != 1 or re.fullmatch(pattern, matches[0]) is None:
            raise ValueError(f"游戏版本配置无效：{constant}")
        result[key] = matches[0]
    return result


def assert_supported_game(game: Path) -> str:
    """先校验完整 EXE 再读资源，返回已验证指纹供目录写入；不支持外部覆盖白名单。"""
    version = read_game_version()
    digest = hashlib.sha256()
    with (game / "sora_2nd.exe").open("rb") as executable:
        for block in iter(lambda: executable.read(1024 * 1024), b""):
            digest.update(block)
    actual = digest.hexdigest()
    if actual != version["exe_sha256"]:
        raise ValueError(
            f"EXE 版本不匹配：仅支持 {version['file_version']} "
            f"(Steam build {version['steam_build_id']})；停止目录生成")
    return actual
