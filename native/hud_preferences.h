// 独立 HUD 的纯展示偏好。这里只保存布局与可见项，不包含存档标志、探索开关
// 或传送记录；归一化坐标让分辨率变化后仍能保持相同的屏幕相对位置。
#pragma once
#include <string>
#include <string_view>

namespace tracker {
struct HudPreferences {
    bool enabled = true;
    bool title = true, status = true, areaName = false, areaProgress = true, totalProgress = false;
    float x = 0.0f, y = 1.0f;
};
struct HudPosition { float x = 0, y = 0; };

bool DecodeHudPreferences(std::string_view text, HudPreferences& result) noexcept;
std::string EncodeHudPreferences(const HudPreferences& preferences);
HudPosition PlaceHud(const HudPreferences&, float width, float height, float boxWidth, float boxHeight, float margin) noexcept;
void RememberHudPosition(HudPreferences&, HudPosition, float width, float height, float boxWidth, float boxHeight, float margin) noexcept;

enum class HudReadResult { Missing, Valid, Invalid, IoError };
// 文件实现只访问调用方已验证的数据目录内固定 hud.ini；失败不修改传入偏好。
HudReadResult ReadHudPreferences(const std::wstring& folder, HudPreferences&) noexcept;
bool WriteHudPreferences(const std::wstring& folder, const HudPreferences&) noexcept;
}
