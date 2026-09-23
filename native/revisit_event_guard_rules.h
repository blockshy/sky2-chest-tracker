// 特殊宝箱的纯规则。运行时桥和公开测试共用，避免根据物品或笼统参数猜测旧剧情。
#pragma once
#include "revisit_policy.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tracker::revisit_eventguard {
// 只承认已经过完整 EXE 校验的原生 TBoxProcess 唯一调用点和整数类型标记。
inline constexpr uintptr_t kTBoxStartReturnRva = 0x2E176C;
inline constexpr uint32_t kIntegerTag = 0x40000000;
inline constexpr uint32_t kEighthChapter = kIntegerTag | 8u;
inline constexpr uint32_t kNinthChapter = kIntegerTag | 9u;

// 这些旗标覆盖入场、旧剧情战和要塞必要机关。16044 是森林再战分支，正常首次
// 获胜时可能不设置，故以其后续 16045 收尾作为门槛，不要求可选分支全部执行。
inline constexpr uint16_t kRequiredStoryFlags[] = {
    16021, 16042, 16045, 16049, 16050, 16051, 16052, 16053, 16150, 16151, 16152
};

inline bool PrologueScene(std::string_view scene) noexcept {
    return scene == "mp6010" || scene == "mp6010_01" || scene == "mp6011" ||
        scene == "mp6012" || scene == "mp6013" || scene == "mp6013_01";
}

// 研究所的 22040/22041 是可选设备观察，正常通关可以保持未完成，不应把它们当作
// 回访门槛。其余主线收尾位关闭旧自动事件，并保留照明、战后和撤离后的原生状态。
inline constexpr uint16_t kRequiredResearchStoryFlags[] = {
    22031, 22032, 22033, 22034, 22035, 22036, 22037, 22038, 22039,
    22042, 22043, 22044, 22045, 22046, 22047
};

// 荣耀号同一场景同时承载第六章和终章。首次扩展只接纳终章已完成舰内主线的
// 存档，尤其要求 25040，避免把原本负责推进主线的 1440 宝箱提前当普通箱处理。
// 25156 属于可选观察事件，不纳入门槛；保留未完成时的原生交互。
inline constexpr uint16_t kRequiredGloriousStoryFlags[] = {
    22051, 22052, 22053, 22054, 22058, 22059, 22060, 22061,
    22062, 22063, 22064, 22068, 22070, 22080,
    25029, 25030, 25039, 25040, 25041, 25042, 25043, 25049
};

// 场景名与地区号必须同时匹配；只列入已审查的实体场景，包括正常出口可到的户外。
inline uint32_t SceneRegion(std::string_view scene) noexcept {
    if (PrologueScene(scene)) return 6;
    if (scene == "mp6100" || scene == "mp6100_01") return 9;
    if (scene == "mp8500" || scene == "mp8500_01" || scene == "mp8500_02") return 8;
    return 0;
}

inline bool SupportedScene(std::string_view scene) noexcept { return SceneRegion(scene) != 0; }

inline bool SupportedChapter(uint32_t chapter, std::string_view scene) noexcept {
    const auto region = SceneRegion(scene);
    if (region == 8) return chapter == kNinthChapter;
    return region != 0 && (chapter == kEighthChapter || chapter == kNinthChapter);
}

// t_place 的当前地形可以使用地区 0，而同场景的主地点仍保存实际大地图地区。
// 荣耀号还存在第六章地区 8 与终章地区 7 的同 ID 记录，不能固定比较地区 8。
// 本函数仅归一已经逐项核对过的场景关系；调用方必须先证明当前/主地点的
// ID、场景、variant、原始地区均存在于真实 t_place 表，且主地点对象有效。
// 返回 0 表示拒绝，不表示“任意地区都可用”；剧情完成位仍由后续门槛独立检查。
inline uint32_t ResolveKnownSceneRegion(std::string_view scene, uint32_t rawRegion,
                                        uint32_t rootRegion) noexcept {
    const auto expected = SceneRegion(scene);
    if (!expected) return 0;
    if (expected == 8) {
        return (rootRegion == 7 || rootRegion == 8) &&
            (rawRegion == 0 || rawRegion == 7 || rawRegion == 8) ? 8u : 0u;
    }
    return rootRegion == expected && (rawRegion == 0 || rawRegion == expected) ? expected : 0u;
}

// 标准模式在纯地理关系之外继续保留原章节门槛；实验调用者可以复用上面的地理
// 校验，但地理归一结果本身绝不授予跳过剧情脚本或修改宝箱的权限。
inline uint32_t ResolveRevisitStoryRegion(uint32_t chapter, std::string_view scene,
                                          uint32_t rawRegion, uint32_t rootRegion) noexcept {
    return SupportedChapter(chapter, scene) ? ResolveKnownSceneRegion(scene, rawRegion, rootRegion) : 0u;
}

// 只有实验构建且明确存在活动实验行程时才能跨章保护；仍拒绝非整数类型及未知章节。
// 此纯函数不读取全局状态，便于用相同用例验证剧情限制策略与全传送策略的差异。
inline bool ExperimentalTripChapter(uint32_t chapter, bool experimentalTrip) noexcept {
    return revisit_policy::kUnrestricted && experimentalTrip &&
        chapter >= kIntegerTag && chapter <= (kIntegerTag | 9u);
}

template<size_t N>
inline bool AllFlags(const uint8_t* flags, size_t size, const uint16_t (&required)[N]) noexcept {
    if (!flags || size != 4096) return false;
    for (const auto flag : required)
        if ((flags[flag / 8] & (1u << (flag % 8))) == 0) return false;
    return true;
}

inline bool RequiredStoryComplete(const uint8_t* flags, size_t size) noexcept {
    return AllFlags(flags, size, kRequiredStoryFlags);
}

// 供传送准入与开箱桥共用的只读门槛；不补设剧情位、不回退章节，也不改变机关位。
// 返回 true 只表示已审查的旧主线前提满足，不代表所有可选对话都被屏蔽。
inline bool RequiredRevisitStoryComplete(uint32_t chapter, uint32_t region,
                                        std::string_view scene, const uint8_t* flags,
                                        size_t size) noexcept {
    if (!SupportedChapter(chapter, scene) || region != SceneRegion(scene)) return false;
    if (region == 6) return RequiredStoryComplete(flags, size);
    if (region == 9) return AllFlags(flags, size, kRequiredResearchStoryFlags);
    if (region == 8) return AllFlags(flags, size, kRequiredGloriousStoryFlags);
    return false;
}

struct SpecialChest {
    uint32_t row;
    std::string_view scene;
    std::string_view name;
    uint32_t scriptParameter;
};

// 行号是 t_tbox 原始 571 行中的编号。把带剧情的参数归零只走原生普通开箱收尾：
// 保留物品、已开标志、淡入淡出和自动保存；并非跳过整个脚本或拦截所有 EVENT_NEXT。
inline constexpr SpecialChest kSpecialChests[] = {
    {19, "mp6012", "SaintCroixForest_Treasure00_r", 2520},
    {20, "mp6012", "SaintCroixForest_Treasure01_r", 2520},
    {21, "mp6012", "SaintCroixForest_Treasure02_r", 2520},
    {23, "mp6012", "SaintCroixForest_Treasure04_r", 2520},
    {32, "mp6013_01", "GrimselFortress_Treasure04", 2200},
    {33, "mp6013_01", "GrimselFortress_Treasure99", 151},
    {509, "mp8500_01", "mp8500_01_Treasure19", 1440}
};

// 纯规则不读取游戏地址。调用方须先证明行地址来自合法表范围、当前场景两处记录
// 一致，以及参数确实指向原生调用方的临时栈槽；任何不一致都不得进行宽泛替换。
inline bool ShouldNormalize(uintptr_t callerRva, uint32_t chapter,
                            bool storyComplete, std::string_view currentScene,
                            uint32_t row, std::string_view tableScene,
                            std::string_view chestName, uint32_t tableParameter,
                            std::string_view function, uint32_t argumentCount,
                            uint32_t argument, bool experimentalTrip = false) noexcept {
    const bool authorized = (SupportedChapter(chapter, currentScene) && storyComplete) ||
        ExperimentalTripChapter(chapter, experimentalTrip);
    if (callerRva != kTBoxStartReturnRva || !authorized ||
        !SupportedScene(currentScene) || currentScene != tableScene ||
        function != "TBoxProcess" || argumentCount != 1 ||
        argument != (kIntegerTag | tableParameter)) return false;
    for (const auto& chest : kSpecialChests) {
        if (row == chest.row && tableScene == chest.scene && chestName == chest.name &&
            tableParameter == chest.scriptParameter) return true;
    }
    return false;
}
}
