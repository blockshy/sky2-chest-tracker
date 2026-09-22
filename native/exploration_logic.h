// 不依赖游戏或 Windows 的探索辅助边界检查，供原生挂钩与回归测试共同使用。
#pragma once
#include <cmath>
#include <cstdint>

namespace tracker {
// 候选完整描述来自本次原生状态和游戏静态表，不包含人工点位清单、锚点或剧情旗标。
// 原始表允许同ID的备用入口，native字段必须取与游戏相同的首条匹配记录。
struct NativeTravelCandidate {
    uint32_t id = 0, region = 0, area = 0;
    uint8_t visible = 0, blocked = 0, registered = 0;
    uint32_t nativeRegion = 0, nativeArea = 0;
    uint8_t nativeFlags = 0;
    // area=0表示点位不属于聚合分组；此时忽略下列分组字段，绝不虚构“第0组”。
    bool areaExists = false;
    uint32_t areaRegion = 0;
    uint8_t areaVisible = 0, areaBlocked = 0;
};

// 统一规则仅表示“原生菜单已登记且未灰化，忽略未到访限制”。它不判断道路、门、
// 谜题或剧情事件是否完成。1..9是当前受校验构建的地区范围，不是点位支持清单。
// 注册/灰态严格匹配正常布尔值；未知字段值、静态记录不一致和内部隐藏项均拒绝。
inline bool CanRevealNativeTravel(const NativeTravelCandidate& candidate, uint32_t currentRegion) noexcept {
    if (!candidate.id || candidate.id > 1000 || !currentRegion || currentRegion > 9 ||
        candidate.region != currentRegion || candidate.nativeRegion != candidate.region ||
        candidate.nativeArea != candidate.area || candidate.visible != 0 || candidate.blocked != 0 ||
        candidate.registered != 1 || (candidate.nativeFlags & 8) != 0) return false;
    return !candidate.area || (candidate.areaExists && candidate.areaRegion == candidate.region &&
                               candidate.areaVisible <= 1 && candidate.areaBlocked == 0);
}

// 原生每张地图最多保存 204 个探索区块；地址必须落在本地图连续区块数组的行首。
// 先做减法范围验证，避免对损坏的地址执行 start + count * stride 造成整数溢出。
inline bool IsMapChunk(uintptr_t start, uint64_t count, uintptr_t candidate) noexcept {
    return start >= 0x10000 && count > 0 && count <= 204 && candidate >= start &&
        (candidate - start) % 0x50 == 0 && (candidate - start) / 0x50 < count;
}

// 全显只去掉探索进度带来的透明度；楼层淡入淡出、脚本禁用状态仍由游戏决定。
inline float RevealedMapAlpha(float original, float floorAlpha, bool mapEnabled,
                              bool chunkEnabled, uint32_t chunkId) noexcept {
    return mapEnabled && chunkEnabled && chunkId < 204 && std::isfinite(floorAlpha) &&
        floorAlpha > 0.0f && floorAlpha <= 1.0f ? floorAlpha : original;
}
}
