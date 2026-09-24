// 单文件日志使用 Windows 区域锁协调线程和重复模块，避免“分别检查后同时追加”超限。
#include "runtime_files.h"
#include <Windows.h>

namespace tracker {
namespace {
bool EnsurePlainDirectory(const std::wstring& path) noexcept {
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}
}

bool PrepareRuntimeDataDirectory(const std::wstring& folder, bool pluginMode) noexcept {
    try {
        if (folder.empty()) return false;
        if (pluginMode) {
            const auto separator = folder.find_last_of(L"\\/");
            if (separator == std::wstring::npos || !EnsurePlainDirectory(folder.substr(0, separator))) return false;
        }
        return EnsurePlainDirectory(folder);
    } catch (...) { return false; }
}

bool AppendBoundedRuntimeLog(const std::wstring& path, std::string_view line) noexcept {
    if (path.empty() || line.empty() || line.size() > kRuntimeLogMaxBytes) return false;
    // 允许其它模块同时打开同一个日志；写入范围由 LockFileEx 保护，不依赖各 DLL
    // 私有的 C++ mutex。打开重解析点本身而不跟随链接，避免截断指向外部的文件。
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
        information.nNumberOfLinks != 1) {
        // 硬链接也可能指向其它文件的内容；仅拒绝，不修改或删除它。
        CloseHandle(file);
        return false;
    }
    OVERLAPPED lock{};
    // 锁覆盖整个文件，包括当前文件尾以后的区域。正常的并发日志只需短暂等待；
    // 外部工具若长时间持有文件锁，最多等待 50 ms 后跳过该条诊断，不能阻塞游戏。
    const ULONGLONG deadline = GetTickCount64() + 50;
    while (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &lock)) {
        if (GetLastError() != ERROR_LOCK_VIOLATION || GetTickCount64() >= deadline) {
            CloseHandle(file);
            return false;
        }
        Sleep(1);
    }
    LARGE_INTEGER size{};
    bool success = GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0;
    if (success) {
        LARGE_INTEGER zero{};
        // 使用减法判断剩余空间，避免旧日志异常巨大时整数加法溢出。恰好填满上限
        // 仍可保留；下一条才重置。旧日志大于上限时也只在成功取得锁后进行截断。
        const bool reset = static_cast<unsigned long long>(size.QuadPart) > kRuntimeLogMaxBytes - line.size();
        success = SetFilePointerEx(file, zero, nullptr, reset ? FILE_BEGIN : FILE_END) != FALSE;
        if (success && reset) success = SetEndOfFile(file) != FALSE;
        if (success) {
            DWORD written = 0;
            success = WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) != FALSE &&
                written == line.size();
        }
    }
    UnlockFileEx(file, 0, MAXDWORD, MAXDWORD, &lock);
    CloseHandle(file);
    return success;
}
}
