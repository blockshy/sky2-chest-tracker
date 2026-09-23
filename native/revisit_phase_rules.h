// 原生 MapJumpCallScript 的只读前置保护：本模块不调用脚本、不设置旗标，
// 仅识别当前会被剧情接管或产生副作用的分支，避免直接提交换图结果时跳过它们。
#pragma once
#include <cstddef>
#include <cstdint>

namespace tracker::revisit_phase {

// 地点0只用于只读询问“没有命中任何已知正向目标时”的默认分支，绝不交给
// 游戏查表。==53、46..51等分支不会命中；不限目标、!=45等分支仍正常命中。
// 因此可以区分没有独立Spot的合法道路与被统一剧情接管的自由行动阶段。
inline constexpr uint32_t kUnmatchedDestination = 0u;

// 精确坐标返程没有原生 Spot ID。使用独立哨兵询问“是否存在任一活跃分支”，
// 不能把这个值作为普通 ID 参与 !=45、!=160 等比较而偶然获得许可。
inline constexpr uint32_t kAnyDestination = 0xFFFFFFFFu;

inline bool BeforeScriptBlocked(uint32_t chapter, uint32_t destination,
                                const uint8_t* flags, size_t size) noexcept {
    // 捕获失败或未知章节拒绝推导许可；调用方仍须验证真实场景、原生登记和灰态。
    if (chapter > 9 || !flags || size != 4096) return true;
    const auto has = [flags](uint32_t id) { return (flags[id / 8] & (1u << (id % 8))) != 0; };
    const bool any = destination == kAnyDestination;
    const auto is = [=](uint32_t id) { return any || destination == id; };
    const auto between = [=](uint32_t first, uint32_t last) {
        return any || (destination >= first && destination <= last);
    };

    // 按已核对的 1.03.2 脚本逐章对应条件。反编译器将逻辑与/或分别命名为
    // or2/or3；这里使用明确的 C++ &&/||，不把函数名误读成两个逻辑或。
    // 原生预处理 PARAM_0=1 也可能弹对话或写旗标，故不能执行它来试探许可。
    switch (chapter) {
    case 0:
        // 初次水道训练：99 由 EVENT_NEXT(121) 接管，其他目标弹提示后拒绝。
        return has(16019) && !has(16021);
    case 1:
        // 217、220、223、224：包括不限目标的流程，以及对45的明确豁免。
        return (has(17016) && !has(17017) && is(53)) ||
            (has(17019) && !has(17020)) ||
            (has(17022) && !has(17023) && (any || destination != 45)) ||
            (has(17023) && !has(17024) && between(46, 51));
    case 2:
        // 1650 的两个否定条件必须同时成立；62、63..65另有独立剧情接管。
        return (has(18002) && !has(18305) && !has(18006)) ||
            (has(18029) && !has(18263) && is(62)) ||
            (has(18042) && !has(18043) && between(63, 65));
    case 3:
        // 451、465、472不限目标；1073位于通常16000起的剧情摘要区间之外，
        // 运行时必须把它额外纳入请求身份，防止关图期间条件改变后继续派发。
        return (has(19044) && has(19049) && has(19050) && !has(19051) && !has(19264)) ||
            (has(19064) && !has(19065)) ||
            (has(19067) && !has(19068) && is(79)) ||
            (has(1073) && !has(19072)) ||
            (has(19076) && !has(19077) && is(80));
    case 4:
        // 828 对12/13/14豁免；831会依据来源地点补写20278/20279，不能跳过。
        return (has(20027) && !has(20028) &&
                (any || (destination != 12 && destination != 13 && destination != 14))) ||
            (has(20277) && !has(20031) && between(1, 7)) ||
            (has(20061) && !has(20062) && is(13));
    case 6:
        // 1882 除内部目标160外接管所有原生目标。内部记录本身仍由目录规则拒绝。
        return has(22341) && !has(22344) && (any || destination != 160);
    case 8:
        // 保留已核对的两个原生例外：131预处理写24273，55会接管1320。
        return (!has(24014) && is(131)) || (!has(24020) && is(55));
    default:
        // 第5、7、9章没有前置特殊分支；这不豁免原生登记/剧情灰态等其他校验。
        return false;
    }
}
}
