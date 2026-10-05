// 宝箱页面的最小接口。绘制表由独立窗口显式绑定；所有页面状态只在绘制线程使用。
// 原生地图及传送修改继续通过原有线程安全请求队列，不从 UI 直接调用游戏函数。
#pragma once
#include "sky2_ui.h"
namespace tracker {
void SetPanelUi(const Sky2UiApi* ui) noexcept;
const Sky2UiApi& PanelUi() noexcept;
bool PanelUiReady() noexcept;
void ApplyPanelActions(uint32_t pending) noexcept;
void TickPanel(const Sky2Frame* frame);
void DrawPanelHeader(const Sky2Frame* frame);
void DrawPanelPage(const Sky2Frame* frame);
void PanelVisibilityChanged(int32_t active) noexcept;
// 页索引只选择用途，不接触传送目标和确认令牌；切页统一撤销旧确认。
int StandalonePageIndex() noexcept;
void StandaloneChoosePage(int page) noexcept;
struct Counts;
// HUD 只在同一绘制线程借用已刷新快照，不再次读取游戏或改变统计口径。
const Counts& StandaloneCountsSnapshot() noexcept;
bool StandaloneHudVisible() noexcept;
void SetStandaloneHudVisible(bool value) noexcept;
enum class Mode : unsigned;
// 模式入口统一重置清单页码，避免切换口径后停留在不存在的最后一页。
void SetStandaloneDisplayMode(Mode mode) noexcept;
}
