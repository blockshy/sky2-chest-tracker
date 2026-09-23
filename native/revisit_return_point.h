// 返程点只描述原生换图所需的位置，不包含队伍、物品、任务或整份存档。
// 该结构按值跨线程传递；文件编码须逐字段处理，不能直接写入带编译器填充的内存。
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace tracker {
// 不属于原生 t_mapjump ID；只在本 Mod 拥有的菜单交接中使用，由最终消费者拦截。
inline constexpr uint32_t kRevisitReturnTarget = 0xFFFFFFFEu;
// 当前游戏 t_chapter 的 ChapterParam 以 0 表示序章、1～8 表示各章、9 表示终章。
// 二周目仍复用同一章节编号；合法范围不取决于宝箱继承记录或通关次数。
inline constexpr bool ValidRevisitChapter(uint32_t chapter) noexcept { return chapter <= 9; }
struct RevisitReturnPoint {
    char scene[32]{};
    uint32_t region = 0;
    uint32_t chapter = 0;
    uint32_t place = 0;    // 场景根节点的默认地点 ID（原生 field+108 的 +808 数据）。
    uint32_t mapPlace = 0; // 当前站位地形对应的地点 ID（原生 field+648 指向的 MapData）。
    // 仅保存当前站立地形 MapData+0x90，兼容既有文件字段；根地点有独立的标识，
    // 不能要求两者相等。实际返程加载只使用场景、坐标和朝向，不把此值传给游戏。
    uint32_t variant = 0;
    float xyz[3]{};
    float yawRadians = 0;
    // 仅保留出发时的剧情诊断摘要，不要求长期返程时相等：正常开箱、机关和设备
    // 观察可改变旗标。短期排队另由 RevisitNativeContext 摘要检验；两者都不是
    // 同一玩家/同一存档栏位的唯一标识，恢复候选仍须玩家核对地点和时间。
    uint64_t progressSignature = 0;
};

inline bool ValidRevisitReturnPoint(const RevisitReturnPoint& point) noexcept {
    const auto* end = static_cast<const char*>(std::memchr(point.scene, 0, sizeof(point.scene)));
    if (!end || end - point.scene < 6 || end - point.scene > 20 ||
        point.scene[0] != 'm' || point.scene[1] != 'p') return false;
    for (const char* p = point.scene + 2; p != end; ++p)
        if ((*p < '0' || *p > '9') && *p != '_') return false;
    if (!ValidRevisitChapter(point.chapter) || point.region < 1 || point.region > 9 ||
        !point.place || point.place > 9999999 || !point.mapPlace || point.mapPlace > 9999999 ||
        point.variant > 255 || !point.progressSignature || !std::isfinite(point.yawRadians) ||
        std::fabs(point.yawRadians) > 6.284f) return false;
    for (float coordinate : point.xyz)
        if (!std::isfinite(coordinate) || std::fabs(coordinate) >= 100000.0f) return false;
    return true;
}
}
