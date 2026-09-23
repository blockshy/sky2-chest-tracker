// 旧地图回访的纯数据与确认状态机。这里不读取游戏内存，也不执行传送；
// 最终准入必须由原生地图线程重新验证，界面中的“可用”仅表示最近一次快照。
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "revisit_return_point.h"
#include "revisit_forest_rules.h"

namespace tracker {
struct RevisitDestination {
    uint32_t id;
    const char* name;
    const char* scene;
    unsigned chests;
    // 分组仅用于列表展示；剧情门槛与原生表字段必须由游戏线程独立核对。
    const char* group;
};
// 原生点使用正常 t_mapjump 编号；迷途之森是明确隔离的Mod自定义加载入口，
// 使用已核对的剧情结束站立位置，必须在专用保护可用后才能前往。箱数按整个场景统计，
// 同一场景的入口与深处共享箱数，不能把这些行相加作为地区总数。
// 精确返程哨兵不是游戏目的地，必须留在最后；15仅供第8章旧版回访存档应急。
inline constexpr RevisitDestination kRevisitDestinations[] = {
    {97, "训练场", "mp6010", 0, "序章"},
    {98, "训练场小屋", "mp6010_01", 0, "序章"},
    {99, "巴鲁斯塔尔水道 / 入口", "mp6011", 14, "序章"},
    {100, "巴鲁斯塔尔水道 / 最深处", "mp6011", 14, "序章"},
    {101, "桑德克洛瓦森林 / 入口", "mp6012", 9, "序章"},
    {102, "桑德克洛瓦森林 / 野营地", "mp6012", 9, "序章"},
    {103, "古利姆泽尔小要塞 / 外部入口", "mp6013", 0, "序章"},
    {105, "古利姆泽尔小要塞 / 中间地带", "mp6013_01", 6, "序章"},
    {104, "古利姆泽尔小要塞 / 最深处", "mp6013_01", 6, "序章"},
    {120, "入口", "mp6100_01", 18, "研究所"},
    {121, "1F", "mp6100_01", 18, "研究所"},
    {122, "2F", "mp6100_01", 18, "研究所"},
    {151, "3F", "mp6100_01", 18, "研究所"},
    {123, "4F", "mp6100_01", 18, "研究所"},
    {165, "前部 / 各层", "mp8500_01", 17, "荣耀号"},
    {166, "后部 / 各层", "mp8500_02", 30, "荣耀号"},
    // 完整清单按实际大地图分组，不能依赖旧的“四塔异空间”分组来补全名称。
    // 地点自身保留“异空间”及落点层级，避免与同地区的普通塔楼入口混淆。
    {136, "翡翠之塔·异空间 / 入口", "mp0054_01", 22, "四塔异空间"},
    {137, "翡翠之塔·异空间 / 最深处", "mp0054_01", 22, "四塔异空间"},
    {138, "琥珀之塔·异空间 / 入口", "mp1073_01", 30, "四塔异空间"},
    {139, "琥珀之塔·异空间 / 最深处", "mp1073_01", 30, "四塔异空间"},
    {140, "绀碧之塔·异空间 / 入口", "mp2084_01", 23, "四塔异空间"},
    {141, "绀碧之塔·异空间 / 最深处", "mp2084_01", 23, "四塔异空间"},
    {142, "红莲之塔·异空间 / 入口", "mp3044_01", 35, "四塔异空间"},
    {143, "红莲之塔·异空间 / 最深处", "mp3044_01", 35, "四塔异空间"},
    {146, "旧校舍地下遗迹 / 入口", "mp2072", 19, "旧校舍"},
    {forest::kTarget, "神秘森林・迷途之森 / 补箱入口", forest::kScene, 1, "洛连特地区"},
    {15, "旧版应急返回柏斯市", "mp1000", 0, "返程"},
    {kRevisitReturnTarget, "返回记录的出发点", "", 0, "返程"}
};
inline constexpr size_t kRevisitDestinationCount =
    sizeof(kRevisitDestinations) / sizeof(kRevisitDestinations[0]);

inline bool IsRevisitScene(const char* scene) noexcept {
    if (!scene) return false;
    // 这里只识别实体场景，不能单凭结果判断当前是否处于补箱回访。序章、研究所、
    // 四塔等场景也用于正常主线；是否要求先恢复出发点须由协调层结合章节决定。
    // 这些室外过渡场景没有独立清单落点，但可以从回访入口正常走到；重启后不能
    // 把它们误认成普通地区并覆盖最初出发点。
    if (std::strcmp(scene, "mp6100") == 0 || std::strcmp(scene, "mp8500") == 0) return true;
    for (const auto& destination : kRevisitDestinations)
        if (destination.id != 15 && destination.id != kRevisitReturnTarget &&
            std::strcmp(scene, destination.scene) == 0) return true;
    return false;
}

// 第一次按键只展示确认，第二次新按下沿才提交。选择变化、离开地图、读档导致
// 上下文变化或超时，均由调用方清除确认，避免积压的按键在新场景中触发传送。
class RevisitConfirmation {
    uint32_t target_ = 0;
    uint64_t deadline_ = 0;
public:
    void Cancel() noexcept { target_ = 0; deadline_ = 0; }
    bool Armed(uint32_t target, uint64_t now) const noexcept {
        return target_ == target && now < deadline_;
    }
    bool Press(uint32_t target, uint64_t now) noexcept {
        if (Armed(target, now)) { Cancel(); return true; }
        target_ = target;
        deadline_ = now + 8000;
        return false;
    }
};
}
