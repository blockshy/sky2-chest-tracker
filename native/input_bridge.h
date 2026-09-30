// Windows 输入接入层：游戏调用链读取手柄，窗口消息观察真实键鼠活动。
#pragma once
#include <Windows.h>
#include <Xinput.h>
#include <cstdint>
#include "controller_logic.h"
namespace tracker {
bool InstallInputBridge(uintptr_t base) noexcept;
using InputWindowMessage = void (*)(HWND, UINT, WPARAM, LPARAM) noexcept;
void AttachInputWindow(HWND window, InputWindowMessage callback) noexcept;
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
