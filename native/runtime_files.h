// 运行数据布局与日志边界：纯路径规则可跨平台测试，文件操作由 Windows 实现提供。
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace tracker {
// 日志只保留一个固定文件。超限时清空旧内容再记录当前行，不生成轮转副本。
inline constexpr std::size_t kRuntimeLogMaxBytes = 1024 * 1024;

// 输入必须是 GetModuleFileNameW 返回的完整 EXE 路径，不接受只有文件名或目录的值。
// ASI 与独立版始终使用不同目录；这里只选择路径，不迁移或读取另一种分发的数据。
inline std::wstring RuntimeDataDirectory(std::wstring_view executable, bool pluginMode) {
    const auto separator = executable.find_last_of(L"\\/");
    if (separator == std::wstring_view::npos || separator + 1 == executable.size()) return {};
    std::wstring result(executable.substr(0, separator + 1));
    if (pluginMode) result += L"plugins\\";
    result += L"Sky2ChestTracker";
    return result;
}

// 仅创建本分发所需的数据目录；已有同名文件或目录联接均拒绝使用。
// 函数不隐式迁移旧版 Sky2Mods 数据，安装迁移由独立安装器负责。
bool PrepareRuntimeDataDirectory(const std::wstring& folder, bool pluginMode) noexcept;

// 写入一条完整日志，并在同一文件锁内完成大小检查、必要截断和追加。
// 返回 false 只表示本条诊断未落盘，不应影响游戏；空行或单行超过上限也会拒绝。
bool AppendBoundedRuntimeLog(const std::wstring& path, std::string_view line) noexcept;
}
