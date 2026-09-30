// 独立 ASI/代理 DLL 的窗口适配：复用业务页面，外壳与输入仍由本模块拥有。
#pragma once
#include "sky2_ui.h"
#include <cstdint>

namespace tracker {
bool InitializeStandalonePanel() noexcept;
// 只在 Present 线程消费输入动作，避免 WndProc 或游戏输入线程访问页面状态。
void ApplyStandaloneActions(uint32_t actions) noexcept;
void DrawStandalonePanel(const Sky2Frame& frame);
}
