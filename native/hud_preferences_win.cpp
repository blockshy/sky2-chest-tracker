// HUD 偏好只读写固定的 Mod 文件。先写临时文件再原子替换，防止退出或断电时
// 将完整配置截断；拒绝目录链接、文件链接和硬链接，不跟随它们修改其它位置。
#include "hud_preferences.h"
#include <Windows.h>
#include <cstdio>

namespace tracker {
namespace {
bool PlainFolder(const std::wstring& folder) noexcept {
    const auto flags = GetFileAttributesW(folder.c_str());
    return flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_DIRECTORY) && !(flags & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool PlainFile(HANDLE file, BY_HANDLE_FILE_INFORMATION& info) noexcept {
    return GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1 &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}
}
HudReadResult ReadHudPreferences(const std::wstring& folder, HudPreferences& result) noexcept {
    try {
        if (folder.empty() || !PlainFolder(folder)) return HudReadResult::IoError;
        const auto path = folder + L"\\hud.ini";
        const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_FILE_NOT_FOUND ? HudReadResult::Missing : HudReadResult::IoError;
        BY_HANDLE_FILE_INFORMATION info{}; char bytes[4096]{}; DWORD read = 0;
        const bool plain = PlainFile(file, info) && !info.nFileSizeHigh && info.nFileSizeLow <= sizeof(bytes);
        const bool complete = plain && ReadFile(file, bytes, info.nFileSizeLow, &read, nullptr) && read == info.nFileSizeLow;
        CloseHandle(file);
        if (!plain) return HudReadResult::Invalid;
        if (!complete) return HudReadResult::IoError;
        return DecodeHudPreferences({bytes, read}, result) ? HudReadResult::Valid : HudReadResult::Invalid;
    } catch (...) { return HudReadResult::IoError; }
}
bool WriteHudPreferences(const std::wstring& folder, const HudPreferences& value) noexcept {
    try {
        if (folder.empty() || !PlainFolder(folder)) return false;
        const auto target = folder + L"\\hud.ini";
        const auto current = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (current != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{}; const bool plain = PlainFile(current, info); CloseHandle(current);
            if (!plain) return false;
        } else if (GetLastError() != ERROR_FILE_NOT_FOUND) return false;
        const auto text = EncodeHudPreferences(value);
        wchar_t suffix[96]{};
        swprintf_s(suffix, L"\\hud-%lu-%llu.tmp", GetCurrentProcessId(), GetTickCount64());
        const auto temporary = folder + suffix;
        const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const bool complete = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
            written == text.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!complete || !PlainFolder(folder) ||
            !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str()); return false; // 仅清理本次 CREATE_NEW 成功的临时文件。
        }
        return true;
    } catch (...) { return false; }
}
}
