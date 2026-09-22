// 验证探索辅助只放开显示边界，不越过脚本禁用或无效区块，也不破坏楼层淡入淡出。
#include "exploration_logic.h"
#include <cstdio>
#include <initializer_list>
#include <limits>

int main() {
    unsigned failures = 0;
    const auto check = [&](bool value, const char* label) {
        if (!value) { std::printf("FAIL: %s\n", label); ++failures; }
    };
    using namespace tracker;
    // 候选由原生静态记录与本次状态组成；测试编号刻意不对应旧商业点白名单。
    // 到访旗标不属于谓词输入，防止实现重新引入真实入口锚点或跨点链式扩展。
    const NativeTravelCandidate valid{731, 3, 47, 0, 0, 1, 3, 47, 0, true, 3, 0, 0};
    check(CanRevealNativeTravel(valid, 3), "原生登记且非灰的任意静态点可补显未到访分组");
    const auto reject = [&](const char* label, auto alter) {
        auto candidate = valid;
        alter(candidate);
        check(!CanRevealNativeTravel(candidate, 3), label);
    };
    for (uint32_t id : {1u, 33u, 60u, 113u, 155u, 731u, 999u, 1000u}) {
        auto candidate = valid; candidate.id = id;
        check(CanRevealNativeTravel(candidate, 3), "编号类别不再决定是否支持，统一规则覆盖表内任意合法点");
    }
    for (uint32_t region = 1; region <= 9; ++region) {
        auto candidate = valid;
        candidate.region = candidate.nativeRegion = candidate.areaRegion = region;
        check(CanRevealNativeTravel(candidate, region), "九个适配地区均按相同原生规则判断");
    }
    for (uint32_t region : {0u, 2u, 10u, std::numeric_limits<uint32_t>::max()})
        check(!CanRevealNativeTravel(valid, region), "非当前地区或未知地区拒绝补显");
    reject("零编号拒绝", [](auto& c) { c.id = 0; });
    reject("超出原生运行时编号界限拒绝", [](auto& c) { c.id = 1001; });
    reject("运行时地区与当前地区不一致拒绝", [](auto& c) { c.region = 4; });
    reject("静态地区与运行时地区不一致拒绝", [](auto& c) { c.nativeRegion = 4; });
    reject("静态分组与运行时分组不一致拒绝", [](auto& c) { c.nativeArea = 48; });
    reject("已显示项无需再次修改", [](auto& c) { c.visible = 1; });
    reject("异常可见值不作为未到访", [](auto& c) { c.visible = 2; });
    for (uint8_t value : {uint8_t{1}, uint8_t{2}, uint8_t{255}})
        reject("最终灰态或异常灰态始终拒绝", [&](auto& c) { c.blocked = value; });
    for (uint8_t value : {uint8_t{0}, uint8_t{2}, uint8_t{255}})
        reject("未登记或异常登记值拒绝", [&](auto& c) { c.registered = value; });
    for (uint8_t flags : {uint8_t{8}, uint8_t{9}, uint8_t{255}})
        reject("静态隐藏位 bit3 始终优先", [&](auto& c) { c.nativeFlags = flags; });
    for (uint8_t flags : {uint8_t{1}, uint8_t{4}, uint8_t{0x80}, uint8_t{0xF7}}) {
        auto candidate = valid; candidate.nativeFlags = flags;
        check(CanRevealNativeTravel(candidate, 3), "其他原生显示标志不被误当隐藏位");
    }
    reject("非零分组缺失时拒绝", [](auto& c) { c.areaExists = false; });
    reject("分组属于另一地区时拒绝", [](auto& c) { c.areaRegion = 4; });
    reject("分组最终灰态禁用时拒绝", [](auto& c) { c.areaBlocked = 1; });
    reject("分组异常灰态拒绝", [](auto& c) { c.areaBlocked = 2; });
    reject("分组异常可见值拒绝", [](auto& c) { c.areaVisible = 2; });
    auto visibleArea = valid; visibleArea.areaVisible = 1;
    check(CanRevealNativeTravel(visibleArea, 3), "已显示分组仍可补显其中未到访点");
    auto standalone = valid;
    standalone.area = standalone.nativeArea = 0;
    standalone.areaExists = false;
    standalone.areaRegion = 999; standalone.areaVisible = 255; standalone.areaBlocked = 255;
    check(CanRevealNativeTravel(standalone, 3), "area0独立点不读取或虚构聚合分组");
    standalone.nativeArea = 47;
    check(!CanRevealNativeTravel(standalone, 3), "area0点仍须与静态归属一致");
    check(IsMapChunk(0x10000, 204, 0x10000 + 203 * 0x50), "允许最后一个合法区块");
    check(!IsMapChunk(0x10000, 204, 0x10000 + 204 * 0x50), "拒绝数组尾后地址");
    check(!IsMapChunk(0x10000, 204, 0x10001), "拒绝行内伪指针");
    check(!IsMapChunk(0x10000, 205, 0x10000), "拒绝超出保存格式容量的数组");
    check(!IsMapChunk(0x10000, 0, 0x10000), "拒绝空数组");
    check(!IsMapChunk(0x10000, 1, 0xFFFF), "拒绝起点之前的地址");
    check(!IsMapChunk(0xFFFF, 1, 0xFFFF), "拒绝无效低地址");
    check(RevealedMapAlpha(0, 1, true, true, 203) == 1, "可探索区块全显");
    check(RevealedMapAlpha(0, 0.4f, true, true, 1) == 0.4f, "保留楼层淡入淡出");
    check(RevealedMapAlpha(0, 1, true, false, 1) == 0, "保留脚本禁用区块");
    check(RevealedMapAlpha(0, 1, false, true, 1) == 0, "保留禁用地图");
    check(RevealedMapAlpha(0, 1, true, true, 204) == 0, "拒绝越界区块编号");
    for (float invalid : {0.0f, -1.0f, 1.1f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()})
        check(RevealedMapAlpha(0.25f, invalid, true, true, 1) == 0.25f, "无效楼层透明度保持原值");
    std::printf("%u exploration failure(s)\n", failures);
    return failures ? 1 : 0;
}
