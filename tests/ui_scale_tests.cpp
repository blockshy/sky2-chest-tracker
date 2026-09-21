#include "ui_scale.h"
#include <cmath>
#include <cstdio>

int main() {
    unsigned failures = 0;
    const auto check = [&](bool ok) { if (!ok) ++failures; };
    // 同比例分辨率的字体占屏比例与逻辑布局完全一致，包括切换回 1080p。
    for (float height : {720.0f, 1080.0f, 1440.0f, 2160.0f, 4320.0f, 1080.0f}) {
        const float width = height * 16.0f / 9.0f;
        const float scale = tracker::UiScale(width, height);
        check(std::abs(height / scale - 1080.0f) < 0.01f);
        check(std::abs(20.0f * scale / height - 20.0f / 1080.0f) < 0.00001f);
    }
    check(std::abs(tracker::UiScale(3440, 1440) - 1440.0f / 1080) < 0.0001f);
    check(tracker::UiScale(1280, 1024) == 1280.0f / 1920);
    check(tracker::UiScale(0, 0) == 1);
    std::printf("UI scale: %u failures\n", failures);
    return failures ? 1 : 0;
}
