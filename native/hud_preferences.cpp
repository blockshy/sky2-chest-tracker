// 配置解析和坐标规则与 ImGui/Win32 分离，公开测试可使用合成文本验证边界。
#include "hud_preferences.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>

namespace tracker {
namespace {
std::string_view Trim(std::string_view value) noexcept {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
float Unit(float value) noexcept { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f; }
float Extent(float value) noexcept { return std::isfinite(value) ? std::max(0.0f, value) : 0.0f; }
struct Axis { float origin = 0, span = 0; };
Axis Bounds(float viewport, float box, float margin) noexcept {
    viewport = Extent(viewport); box = Extent(box); margin = Extent(margin);
    const float origin = std::min(margin, std::max(0.0f, viewport - box) * 0.5f);
    return {origin, std::max(0.0f, viewport - box - origin * 2.0f)};
}
}
bool DecodeHudPreferences(std::string_view text, HudPreferences& result) noexcept {
    if (text.empty() || text.size() > 4096) return false;
    HudPreferences value;
    bool section = false, version = false;
    unsigned seen = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = Trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line == "[Hud]") { if (section) return false; section = true; continue; }
        if (!section) return false;
        const auto equal = line.find('=');
        if (equal == std::string_view::npos) return false;
        const auto key = Trim(line.substr(0, equal)), data = Trim(line.substr(equal + 1));
        if (key == "Version") { if (version || data != "1") return false; version = true; continue; }
        constexpr std::string_view keys[]{"Enabled", "Title", "Status", "AreaName", "AreaProgress", "TotalProgress", "X", "Y"};
        unsigned index = 0;
        for (; index < 8 && keys[index] != key; ++index) {}
        if (index == 8) continue; // 未来展示字段可忽略，但既有字段不能重复或被损坏值覆盖。
        if (seen & (1u << index)) return false;
        seen |= 1u << index;
        if (index < 6) {
            if (data != "0" && data != "1") return false;
            bool* targets[]{&value.enabled, &value.title, &value.status, &value.areaName, &value.areaProgress, &value.totalProgress};
            *targets[index] = data == "1";
        } else {
            float parsed = 0;
            const auto read = std::from_chars(data.data(), data.data() + data.size(), parsed);
            if (read.ec != std::errc{} || read.ptr != data.data() + data.size() ||
                !std::isfinite(parsed) || parsed < 0 || parsed > 1) return false;
            (index == 6 ? value.x : value.y) = parsed;
        }
    }
    if (!section || !version) return false;
    result = value; return true;
}
std::string EncodeHudPreferences(const HudPreferences& value) {
    std::ostringstream text;
    text.imbue(std::locale::classic()); // 配置小数点不随玩家地区设置变化。
    text.precision(9);
    text << "[Hud]\nVersion=1\nEnabled=" << value.enabled << "\nTitle=" << value.title
        << "\nStatus=" << value.status << "\nAreaName=" << value.areaName
        << "\nAreaProgress=" << value.areaProgress << "\nTotalProgress=" << value.totalProgress
        << "\nX=" << Unit(value.x) << "\nY=" << Unit(value.y) << '\n';
    return text.str();
}
HudPosition PlaceHud(const HudPreferences& value, float width, float height, float boxWidth, float boxHeight, float margin) noexcept {
    const auto horizontal = Bounds(width, boxWidth, margin), vertical = Bounds(height, boxHeight, margin);
    return {horizontal.origin + Unit(value.x) * horizontal.span, vertical.origin + Unit(value.y) * vertical.span};
}
void RememberHudPosition(HudPreferences& value, HudPosition position, float width, float height,
                         float boxWidth, float boxHeight, float margin) noexcept {
    const auto horizontal = Bounds(width, boxWidth, margin), vertical = Bounds(height, boxHeight, margin);
    // 极小窗口没有可移动范围时保持原锚点，放大后仍能回到玩家选择的位置。
    if (horizontal.span > 0 && std::isfinite(position.x)) value.x = Unit((position.x - horizontal.origin) / horizontal.span);
    if (vertical.span > 0 && std::isfinite(position.y)) value.y = Unit((position.y - vertical.origin) / vertical.span);
}
}
