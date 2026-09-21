// Windows 输入接入层：游戏调用链读取手柄，窗口消息观察真实键鼠活动。
#pragma once
#include <Windows.h>
#include <cstdint>
#include "controller_logic.h"
namespace tracker {
bool InstallInputBridge(uintptr_t base) noexcept;
void AttachInputWindow(HWND window) noexcept;
uint32_t TakeInputActions() noexcept;
bool UsingController() noexcept;
bool InputBridgeReady() noexcept;
bool ControllerModifierHeld() noexcept;
}
