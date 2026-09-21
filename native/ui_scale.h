#pragma once
#include <algorithm>

namespace tracker {
// 以 1080p 为逻辑画布；超宽屏不额外放大，窄屏按宽度限制以免裁切。
inline float UiScale(float width, float height) {
    if (width <= 0 || height <= 0) return 1.0f;
    return std::min(width / 1920.0f, height / 1080.0f);
}
}
