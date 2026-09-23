// 证明特殊箱归零严格局限于已授权回访上下文，普通箱和魔兽箱不会误中规则。
#include "revisit_event_guard_rules.h"
#include <array>
#include <cstdio>
#include <initializer_list>

using namespace tracker::revisit_eventguard;
static unsigned failures = 0;
static void Check(bool condition, const char* name) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}

int main() {
    std::array<uint8_t, 4096> flags{};
    Check(!RequiredStoryComplete(flags.data(), flags.size()), "unfinished story rejected");
    for (auto flag : kRequiredStoryFlags) flags[flag / 8] |= static_cast<uint8_t>(1u << (flag % 8));
    Check(RequiredStoryComplete(flags.data(), flags.size()), "required completion admitted");
    Check((flags[16044 / 8] & (1u << (16044 % 8))) == 0, "optional rematch remains unset");
    Check(!RequiredStoryComplete(nullptr, flags.size()), "null flags rejected");
    Check(!RequiredStoryComplete(flags.data(), flags.size() - 1), "partial flags rejected");
    for (auto flag : kRequiredStoryFlags) {
        auto incomplete = flags;
        incomplete[flag / 8] &= static_cast<uint8_t>(~(1u << (flag % 8)));
        Check(!RequiredStoryComplete(incomplete.data(), incomplete.size()), "each required event checked");
    }

    for (const auto& chest : kSpecialChests) {
        const auto validChapter = SceneRegion(chest.scene) == 8 ? kNinthChapter : kEighthChapter;
        const auto admit = [&](uintptr_t caller, uint32_t chapter, bool complete,
                               std::string_view current, uint32_t row, std::string_view scene,
                               std::string_view name, uint32_t param, std::string_view function,
                               uint32_t count, uint32_t argument) {
            return ShouldNormalize(caller, chapter, complete, current, row, scene, name,
                                   param, function, count, argument);
        };
        const auto tagged = kIntegerTag | chest.scriptParameter;
        Check(admit(kTBoxStartReturnRva, validChapter, true, chest.scene, chest.row,
                    chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 1, tagged),
              "known special chest uses ordinary finish");
        Check(!admit(kTBoxStartReturnRva + 1, validChapter, true, chest.scene, chest.row,
                     chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 1, tagged), "different caller rejected");
        for (const auto chapter : {kIntegerTag, kIntegerTag | 7u, 8u, 9u})
            Check(!admit(kTBoxStartReturnRva, chapter, true, chest.scene, chest.row,
                         chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 1, tagged), "other chapter or type rejected");
        Check(!admit(kTBoxStartReturnRva, validChapter, false, chest.scene, chest.row,
                     chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 1, tagged), "unfinished story rejected");
        Check(!admit(kTBoxStartReturnRva, validChapter, true, "mp1000", chest.row,
                     chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 1, tagged), "source scene cannot normalize");
        Check(!admit(kTBoxStartReturnRva, validChapter, true, chest.scene, 570,
                     chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 1, tagged), "same name other row rejected");
        Check(!admit(kTBoxStartReturnRva, validChapter, true, chest.scene, chest.row,
                     "mp6011", chest.name, chest.scriptParameter, "TBoxProcess", 1, tagged), "mismatched row scene rejected");
        Check(!admit(kTBoxStartReturnRva, validChapter, true, chest.scene, chest.row,
                     chest.scene, "other_chest", chest.scriptParameter, "TBoxProcess", 1, tagged), "unknown name rejected");
        Check(!admit(kTBoxStartReturnRva, validChapter, true, chest.scene, chest.row,
                     chest.scene, chest.name, chest.scriptParameter + 1, "TBoxProcess", 1,
                     kIntegerTag | (chest.scriptParameter + 1)), "other event parameter rejected");
        Check(!admit(kTBoxStartReturnRva, validChapter, true, chest.scene, chest.row,
                     chest.scene, chest.name, chest.scriptParameter, "MONSTER_BOX_PROC", 1, tagged), "monster processing unchanged");
        Check(!admit(kTBoxStartReturnRva, validChapter, true, chest.scene, chest.row,
                     chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 2, tagged), "different argument shape rejected");
        Check(!admit(kTBoxStartReturnRva, validChapter, true, chest.scene, chest.row,
                     chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 1, chest.scriptParameter), "untyped argument rejected");
    }
    // 终章序章回访沿用同一套已验证的六个特殊箱，不会因为扩大章节而放宽行匹配。
    const auto& prologueChest = kSpecialChests[0];
    Check(ShouldNormalize(kTBoxStartReturnRva, kNinthChapter, true, prologueChest.scene,
                          prologueChest.row, prologueChest.scene, prologueChest.name,
                          prologueChest.scriptParameter, "TBoxProcess", 1,
                          kIntegerTag | prologueChest.scriptParameter), "chapter nine prologue preserved");
    const auto& gloriousChest = kSpecialChests[6];
    Check(!ShouldNormalize(kTBoxStartReturnRva, kEighthChapter, true, gloriousChest.scene,
                           gloriousChest.row, gloriousChest.scene, gloriousChest.name,
                           gloriousChest.scriptParameter, "TBoxProcess", 1,
                           kIntegerTag | gloriousChest.scriptParameter), "chapter eight glorious rejected");

    // 用空白位图分别建立各地区最小集合，证明不依赖可选观察或先前测试遗留的旗标。
    std::array<uint8_t, 4096> research{}, glorious{};
    for (auto flag : kRequiredResearchStoryFlags) research[flag / 8] |= static_cast<uint8_t>(1u << (flag % 8));
    for (auto flag : kRequiredGloriousStoryFlags) glorious[flag / 8] |= static_cast<uint8_t>(1u << (flag % 8));
    for (auto chapter : {kEighthChapter, kNinthChapter}) {
        // 地形地区 0 只在同场景主地点地区明确时归一；普通未知场景永不借此放行。
        Check(ResolveRevisitStoryRegion(chapter, "mp6012", 0, 6) == 6,
              "prologue terrain resolves through verified root");
        Check(ResolveRevisitStoryRegion(chapter, "mp6100_01", 0, 9) == 9,
              "research terrain resolves through verified root");
        Check(ResolveRevisitStoryRegion(chapter, "mp6100_01", 7, 9) == 0,
              "research cannot borrow glorious final region");
        Check(ResolveRevisitStoryRegion(chapter, "mp6012", 6, 0) == 0,
              "unknown root region remains rejected");
        Check(RequiredRevisitStoryComplete(chapter, 6, "mp6012", flags.data(), flags.size()),
              "prologue admits chapters eight and nine");
        Check(RequiredRevisitStoryComplete(chapter, 9, "mp6100_01", research.data(), research.size()),
              "research admits completed chapter without optional observations");
        Check(!RequiredRevisitStoryComplete(chapter, 8, "mp6100_01", research.data(), research.size()),
              "scene region mismatch rejected");
    }
    for (auto region : {0u, 7u, 8u}) {
        for (auto root : {7u, 8u})
            Check(ResolveRevisitStoryRegion(kNinthChapter, "mp8500_01", region, root) == 8,
                  "glorious chapter nine maps final regions to story group eight");
        Check(ResolveRevisitStoryRegion(kEighthChapter, "mp8500_01", region, 8) == 0,
              "region normalization never bypasses glorious chapter gate");
    }
    Check(ResolveRevisitStoryRegion(kNinthChapter, "mp8500_01", 6, 8) == 0,
          "unrelated glorious terrain region rejected");
    Check(ResolveRevisitStoryRegion(kNinthChapter, "mp8500_01", 0, 6) == 0,
          "unrelated glorious root region rejected");
    Check(ResolveRevisitStoryRegion(kNinthChapter, "mp8500_03", 0, 7) == 0,
          "unknown glorious-like scene rejected");
    Check(RequiredRevisitStoryComplete(kNinthChapter, 8, "mp8500_01", glorious.data(), glorious.size()),
          "completed final chapter glorious admitted");
    Check(!RequiredRevisitStoryComplete(kEighthChapter, 8, "mp8500_01", glorious.data(), glorious.size()),
          "future glorious story never normalized in chapter eight");
    for (auto flag : kRequiredResearchStoryFlags) {
        auto incomplete = research;
        incomplete[flag / 8] &= static_cast<uint8_t>(~(1u << (flag % 8)));
        Check(!RequiredRevisitStoryComplete(kNinthChapter, 9, "mp6100", incomplete.data(), incomplete.size()),
              "each research main story completion checked");
    }
    for (auto flag : kRequiredGloriousStoryFlags) {
        auto incomplete = glorious;
        incomplete[flag / 8] &= static_cast<uint8_t>(~(1u << (flag % 8)));
        Check(!RequiredRevisitStoryComplete(kNinthChapter, 8, "mp8500_02", incomplete.data(), incomplete.size()),
              "each glorious completion including 25040 checked");
    }
    Check(!RequiredRevisitStoryComplete(kNinthChapter, 8, "mp8500_01", nullptr, glorious.size()),
          "null completed late story rejected");
    Check(!RequiredRevisitStoryComplete(kNinthChapter, 8, "mp8500_01", glorious.data(), glorious.size() - 1),
          "partial late story rejected");
    Check(!RequiredRevisitStoryComplete(kNinthChapter, 7, "mp5600_03", glorious.data(), glorious.size()),
          "unreviewed scene excluded from guard");
    Check(!ShouldNormalize(kTBoxStartReturnRva, kEighthChapter, true, "mp6011", 18,
                           "mp6011", "BalstarChannel_Treasure13_m", 0, "TBoxProcess", 1, kIntegerTag),
          "waterway monster chest unchanged");
    Check(!ShouldNormalize(kTBoxStartReturnRva, kEighthChapter, true, "mp6013_01", 30,
                           "mp6013_01", "GrimselFortress_Treasure02_m", 0, "TBoxProcess", 1, kIntegerTag),
          "fortress monster chest unchanged");
    // 地理解析独立于剧情许可：标准 wrapper 仍然拒绝早期荣耀号，纯解析只承认
    // 已审查的场景/地区关系；未知场景与地区不能借实验开关混入。
    Check(ResolveKnownSceneRegion("mp8500_01", 0, 7) == 8, "geography has no chapter gate");
    Check(ResolveKnownSceneRegion("mp8500_03", 0, 7) == 0, "geography still rejects unknown scene");
    Check(ResolveKnownSceneRegion("mp6012", 0, 8) == 0, "geography still rejects wrong root");
    for (const auto& chest : kSpecialChests) {
        auto normalize = [&](uint32_t chapter, bool trip, uint32_t row) {
            return ShouldNormalize(kTBoxStartReturnRva, chapter, false, chest.scene,
                row, chest.scene, chest.name, chest.scriptParameter, "TBoxProcess", 1,
                kIntegerTag | chest.scriptParameter, trip);
        };
        Check(!normalize(kIntegerTag | 1u, false, chest.row), "normal early story never normalized");
        Check(normalize(kIntegerTag | 1u, true, chest.row) == tracker::revisit_policy::kUnrestricted,
              "early experimental trip alone expands precise chest protection");
        Check(!normalize(kIntegerTag | 10u, true, chest.row), "trip rejects unknown chapter");
        Check(!normalize(1u, true, chest.row), "trip rejects untyped chapter");
        Check(!normalize(kIntegerTag | 1u, true, 570u), "trip does not broaden chest identity");
    }
    return failures ? 1 : 0;
}
