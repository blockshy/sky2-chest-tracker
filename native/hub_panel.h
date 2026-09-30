// Hub 页面边界：所有回调只在宿主 UI 线程使用；原生游戏操作仍通过既有请求队列。
#pragma once
#include "sky2_hub.h"
namespace tracker {
bool RegisterHubActions() noexcept;
void HubTick(const Sky2Frame* frame);
void HubDrawHeader(const Sky2Frame* frame);
void HubDrawPage(const Sky2Frame* frame);
void HubDrawOverlay(const Sky2Frame* frame);
void HubVisibilityChanged(int32_t active) noexcept;
// 生命周期由宿主 Present 线程串行调用。异步收尾仍通过 HubTick 观察游戏线程快照。
int32_t HubRequestEnabled(int32_t enabled) noexcept;
int32_t HubActivityState() noexcept;
const char* HubActivityMessage() noexcept;
}
