// Win32 窗口坐标工具：窗口消息和渲染线程必须使用同一 DPI 语境。
// 本文件不依赖 ImGui，也不调用全局 DPI、ShowCursor、ClipCursor 或 SetCursorPos。
#pragma once
#include <Windows.h>
#include <cwchar>

namespace sky2window {
namespace detail {
using WindowDpiFn = HANDLE(WINAPI*)(HWND);
using ThreadDpiFn = HANDLE(WINAPI*)(HANDLE);
struct DpiApi {
    WindowDpiFn window = nullptr;
    ThreadDpiFn thread = nullptr;
    DpiApi() noexcept {
        // 动态解析避免提高独立 Mod 的系统导入要求；缺少新 API 时沿用当前语境。
        const auto user = GetModuleHandleW(L"user32.dll");
        if (user) {
            window = reinterpret_cast<WindowDpiFn>(GetProcAddress(user, "GetWindowDpiAwarenessContext"));
            thread = reinterpret_cast<ThreadDpiFn>(GetProcAddress(user, "SetThreadDpiAwarenessContext"));
        }
    }
};
inline const DpiApi& Api() noexcept { static const DpiApi value; return value; }
}

// 仅当前调用栈采用目标窗口的 DPI 语境。渲染线程可能与创建窗口的线程不同，
// 不可根据其默认 DPI 设置解释客户区大小或鼠标位置；析构总会恢复原线程设置。
class DpiScope {
    HANDLE previous_ = nullptr;
public:
    explicit DpiScope(HWND window) noexcept {
        const auto& api = detail::Api();
        if (window && api.window && api.thread)
            if (const auto context = api.window(window)) previous_ = api.thread(context);
    }
    ~DpiScope() noexcept { if (previous_) detail::Api().thread(previous_); }
    DpiScope(const DpiScope&) = delete;
    DpiScope& operator=(const DpiScope&) = delete;
};

// GetCursorPos 的屏幕坐标与 ScreenToClient 在同一 DPI 范围内转换，天然支持
// 左侧/上方显示器的负坐标。失败不覆盖调用方的最后有效位置，也不夹紧到屏幕边缘。
inline bool ReadMousePosition(HWND window, POINT& point) noexcept {
    DpiScope scope(window);
    POINT current{};
    if (!window || !GetCursorPos(&current) || !ScreenToClient(window, &current)) return false;
    point = current;
    return true;
}

// 只把真正替代游戏主窗的交换链接入输入。原窗口仍显示时，不采纳同进程内
// 的视频/工具辅助窗；类名由首次成功绑定时缓存，原 HWND 已销毁也能继续核验。
inline bool IsReplacementWindowEligible(HWND previous, HWND candidate, const wchar_t* expectedClass) noexcept {
    if (!candidate || !expectedClass || !expectedClass[0] || !IsWindowVisible(candidate) ||
        GetForegroundWindow() != candidate || (IsWindow(previous) && IsWindowVisible(previous))) return false;
    DWORD owner = 0;
    wchar_t windowClass[256]{};
    GetWindowThreadProcessId(candidate, &owner);
    GetClassNameW(candidate, windowClass, static_cast<int>(sizeof(windowClass) / sizeof(windowClass[0])));
    if (owner != GetCurrentProcessId() || std::wcscmp(expectedClass, windowClass)) return false;
    DpiScope scope(candidate);
    RECT area{};
    return GetClientRect(candidate, &area) && area.right >= 320 && area.bottom >= 240;
}
}
