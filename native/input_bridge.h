// Windows 输入接入层：游戏调用链读取手柄，窗口消息观察真实键鼠活动。
#pragma once
#include <Windows.h>
#include <Xinput.h>
#include <cstdint>
#include "controller_logic.h"
#include "window_input_geometry.h"
namespace tracker {
bool InstallInputBridge(uintptr_t base) noexcept;
using InputWindowMessage = void (*)(HWND, UINT, WPARAM, LPARAM) noexcept;
// 每个 HWND 保留自己的原窗口过程，跨屏重建窗口后可重新接入，旧合作链只透传。
bool AttachInputWindow(HWND window, InputWindowMessage callback) noexcept;
// 仅在当前调用范围采用目标窗口的 DPI 语境，离开后恢复调用线程；不改变游戏进程策略。
// 新 API 动态解析，较旧的 Windows 环境仍按其原坐标语境运行。
using InputWindowDpiScope = sky2window::DpiScope;
// 每帧及鼠标按键消息前读取当前客户区坐标，避免跨屏后依赖失效的鼠标跟踪消息。
bool ReadInputMousePosition(HWND window, POINT& point) noexcept;
// 仅渲染线程调用：真实坐标变化恢复鼠标操作身份，避免无 move 消息时仍隐藏指针。
void NotifyInputMousePosition(HWND window, const POINT& point) noexcept;
// 后端重建前仅补齐本 Mod 实际接收的按下，防止旧 HWND 保留拖动捕获。
void ReleaseInputMouseButtons() noexcept;
// 渲染线程发布编辑保护状态；IAT 线程只读取原子值，不访问 ImGui 上下文。
void SetInputModeShortcutEditing(bool editing) noexcept;
bool InputModeShortcutAllowed() noexcept;
// 渲染线程驱动窗口所有权与健康心跳；输入回调独立检查超时，渲染停止时自动放行。
void PumpInputKeyboard() noexcept;
void SynchronizeInputPanel() noexcept;
void SetInputFrameHealth(bool healthy) noexcept;
bool InputPanelInteractive() noexcept;
bool ConsumeInputReset() noexcept;
bool ReadInputPad(XINPUT_GAMEPAD& output) noexcept;
uint32_t TakeInputActions() noexcept;
bool UsingController() noexcept;
bool InputBridgeReady() noexcept;
bool ControllerModifierHeld() noexcept;
}
