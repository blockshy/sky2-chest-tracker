// 全传送目录只决定显示和静态字段核对，不授予传送权限。
// 玩家本机资源生成的表保留同ID变体，最终落点仍按原生选择顺序验证。
#pragma once
#include "revisit_logic.h"
#include <array>
#include <algorithm>

namespace tracker {
struct TravelCatalogRow {
    uint32_t id, region, area, place;
    const char* scene;
    const char* name;
    const char* group;
    // kind只作展示来源分类：0普通菜单数据，1内部名称，2无展示地图的特殊入口。
    // 后两类仅能经单独审查的回访例外开放，不能据“被列出来”推断可传送。
    uint8_t variant, flags, kind;
};
}

#if __has_include("travel_catalog.h")
#include "travel_catalog.h"
#define SKY2_HAS_FULL_TRAVEL_CATALOG 1
#else
// 无游戏资源的公开CI仅验证协调和确认行为，使用已审查旧地图目录作为测试替身。
#define SKY2_HAS_FULL_TRAVEL_CATALOG 0
namespace tracker { inline constexpr std::array<TravelCatalogRow,0> kTravelCatalog{}; }
#endif

namespace tracker {
struct RevisitDisplayCatalog {
    std::array<RevisitDestination,1002> items{};
    size_t count=0;
};
// 原始表必须完整保留供运行时按variant核对；玩家清单隐藏未适配的内部入口，
// 保留普通地点与已单独适配的旧图入口。缺少展示地图的正常命名地点仍可列为灰项，
// 不能把“没有预览图”直接当成无用记录，也不能误删荣耀号等已验证的内部点例外。
// 同样不能仅将“入口用”调试名称翻译后冒充已适配的玩家点。
inline bool ReviewedNativeTravelTarget(uint32_t id) noexcept {
    if (id==15 || id>=1001) return false;
    for (const auto& known:kRevisitDestinations) if (known.id==id) return true;
    return false;
}
inline bool DisplayableTravelRow(const TravelCatalogRow& row) noexcept {
    return ReviewedNativeTravelTarget(row.id) || (row.kind!=1 && (row.flags&8)==0);
}
inline const RevisitDisplayCatalog& ReadRevisitDisplayCatalog() noexcept {
    static const auto result=[] {
        RevisitDisplayCatalog catalog{};
#if SKY2_HAS_FULL_TRAVEL_CATALOG
        std::array<bool,1001> seen{};
        for (const auto& row:kTravelCatalog) {
            if (!row.id || row.id>=seen.size() || seen[row.id] || !DisplayableTravelRow(row)) continue;
            seen[row.id]=true;
            const char* name=row.name;
            const char* group=row.group;
            // 已审核地点沿用带完整实体身份的名称；这里使用实际大地图分组，
            // 所以别名不能依赖旧分组补全“异空间”“旧校舍”等关键区别。
            // 其余名称来自游戏中文地点表，显示替换不改原生ID、场景或落点。
            for (const auto& known:kRevisitDestinations) {
                if (known.id==row.id && known.id!=15) { name=known.name;break; }
            }
            catalog.items[catalog.count++]={row.id,name,row.scene,0,group};
        }
        // 原生目录没有迷途之森：追加明确的Mod入口，不伪造原生ID或表记录。
        // 它按洛连特分组显示，但准入仍由运行时剧情完成位和专用事件保护决定。
        catalog.items[catalog.count++]={forest::kTarget,"神秘森林・迷途之森 / 补箱入口",
            forest::kScene,1,"洛连特地区"};
        // 按地区号及原生编号排序，避免资源中交错存放的入口变体打乱玩家找路顺序。
        const auto region=[](uint32_t id) {
            if (id==forest::kTarget) return forest::kRegion;
            for (const auto& row:kTravelCatalog) if (row.id==id) return row.region;
            return 0u;
        };
        std::sort(catalog.items.begin(),catalog.items.begin()+catalog.count,[&](const auto& a,const auto& b) {
            const auto ar=region(a.id),br=region(b.id);
            return ar!=br ? ar<br : a.id<b.id;
        });
        catalog.items[catalog.count++]={kRevisitReturnTarget,"返回记录的出发点","",0,"返程"};
#else
        for (const auto& item:kRevisitDestinations) catalog.items[catalog.count++]=item;
#endif
        return catalog;
    }();
    return result;
}
inline size_t RevisitDestinationCount() noexcept { return ReadRevisitDisplayCatalog().count; }
inline const RevisitDestination& RevisitDestinationAt(size_t index) noexcept {
    const auto& catalog=ReadRevisitDisplayCatalog();
    return catalog.items[index<catalog.count ? index : catalog.count-1];
}
inline bool RevisitDestinationListed(uint32_t target) noexcept {
    const auto& catalog=ReadRevisitDisplayCatalog();
    for (size_t i=0;i<catalog.count;++i) if (catalog.items[i].id==target) return true;
    return false;
}
inline constexpr size_t kRevisitRowsPerPage=10;
// 翻页与地区分组解耦：每次只移动相邻一页，尽量保留当前行；末页不足十项时，
// 选中最后一个有效项目。首末页不循环，避免一次操作看起来跨过整张清单。
inline size_t RevisitPageSelection(size_t selected,bool forward) noexcept {
    const size_t count=RevisitDestinationCount();
    if (!count) return 0;
    selected=std::min(selected,count-1);
    const size_t page=selected/kRevisitRowsPerPage;
    const size_t lastPage=(count-1)/kRevisitRowsPerPage;
    if ((!forward && page==0) || (forward && page==lastPage)) return selected;
    const size_t targetPage=forward ? page+1 : page-1;
    return std::min(targetPage*kRevisitRowsPerPage+selected%kRevisitRowsPerPage,count-1);
}
}
