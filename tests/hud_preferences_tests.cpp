// 使用合成文本和屏幕尺寸验证 HUD 偏好；不链接游戏、ImGui 或文件系统。
#include "hud_preferences.h"
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    using namespace tracker;
    unsigned failures = 0;
    const auto check = [&](bool value, const char* label) { if (!value) { ++failures; std::printf("FAIL %s\n", label); } };
    HudPreferences expected; expected.x = .37f; expected.y = .82f; expected.title = false;
    expected.areaName = expected.totalProgress = true;
    HudPreferences restored;
    check(DecodeHudPreferences(EncodeHudPreferences(expected), restored) &&
        std::abs(restored.x - expected.x) < .000001f && restored.y == expected.y && !restored.title && restored.areaName && restored.totalProgress,
        "content and normalized position round trip");
    const auto valid = EncodeHudPreferences(expected);
    for (const auto* bad : {"[Hud]\nVersion=2\n", "[Hud]\nVersion=1\nX=nan\n", "[Hud]\nVersion=1\nY=-1\n",
            "[Hud]\nVersion=1\nX=1.1\n", "[Hud]\nVersion=1\nEnabled=2\n", "[Hud]\nVersion=1\nX=0\nX=1\n"}) {
        check(!DecodeHudPreferences(bad, restored) && EncodeHudPreferences(restored) == valid, "invalid text preserves prior preferences");
    }
    check(!DecodeHudPreferences(std::string(4097, 'x'), restored), "bounded configuration read");
    check(DecodeHudPreferences("[Hud]\r\nVersion=1\r\nUnknown=7\r\n", restored) && restored.x == 0 && restored.y == 1,
        "defaults preserve original bottom-left anchor");
    auto point = PlaceHud(restored, 1280, 720, 320, 60, 18);
    check(point.x == 18 && point.y == 642, "default HUD stays inside bottom left");
    RememberHudPosition(restored, {900, 100}, 1280, 720, 320, 60, 18);
    point = PlaceHud(restored, 1280, 720, 320, 60, 18);
    check(std::abs(point.x - 900) < .01f && std::abs(point.y - 100) < .01f, "drag stores reversible viewport position");
    point = PlaceHud(restored, 640, 360, 320, 60, 18);
    check(point.x >= 18 && point.x <= 302 && point.y >= 18 && point.y <= 282, "resolution change clamps complete HUD");
    const auto oldX = restored.x;
    RememberHudPosition(restored, {-100, 9000}, 100, 100, 320, 200, 18);
    check(restored.x == oldX, "tiny viewport preserves selected anchor");
    restored.x = std::numeric_limits<float>::infinity();
    point = PlaceHud(restored, 100, 100, 320, 200, 18);
    check(std::isfinite(point.x) && std::isfinite(point.y) && point.x == 0 && point.y == 0, "invalid geometry remains finite");
    return failures ? 1 : 0;
}
