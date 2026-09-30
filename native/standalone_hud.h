// 独立两版的 HUD 展示入口；内容与位置偏好只保存到 Mod 配置。
#pragma once
#include "sky2_ui.h"
#include "hud_preferences.h"
#include <string>

namespace tracker {
void InitializeStandaloneHud(const std::wstring& folder) noexcept;
const char* StandaloneHudSettingsLabel() noexcept;
void DrawStandaloneHudSettings(const Sky2Frame& frame);
void DrawStandaloneHud(const Sky2Frame& frame, bool settingsPage);
const HudPreferences& StandaloneHudPreferences() noexcept;
}
