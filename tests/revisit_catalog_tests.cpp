// 全目录与旧地区分类必须分离，且每个显示ID和返程操作只出现一次。
#include "revisit_catalog.h"
#include <cstdio>
#include <set>
using namespace tracker;
int main() {
    unsigned failures=0;
    const auto check=[&](bool ok,const char* text) {if(!ok){++failures;std::printf("FAIL %s\n",text);}};
    std::set<uint32_t> ids;
    const auto count=RevisitDestinationCount();
    check(count>1 && count<=1002,"bounded nonempty display catalog");
    for(size_t i=0;i<count;++i) {
        const auto& row=RevisitDestinationAt(i);
        check(ids.insert(row.id).second,"duplicate native ID never creates conflicting confirmation rows");
        check(row.name && row.name[0] && row.group && row.group[0],"complete display labels");
        const auto forward=RevisitPageSelection(i,true),backward=RevisitPageSelection(i,false);
        const auto page=i/kRevisitRowsPerPage,lastPage=(count-1)/kRevisitRowsPerPage;
        check(forward<count && backward<count,"page navigation stays in catalog");
        check(forward/kRevisitRowsPerPage==(page==lastPage ? page : page+1),
              "each next-page input advances exactly one page regardless of region size");
        check(backward/kRevisitRowsPerPage==(page ? page-1 : page),
              "each previous-page input retreats exactly one page regardless of region size");
        if (page<lastPage && forward<count-1)
            check(forward%kRevisitRowsPerPage==i%kRevisitRowsPerPage,"full-page turn preserves selected row");
    }
    check(RevisitDestinationAt(count-1).id==kRevisitReturnTarget,"exact return always at end");
    check(RevisitDestinationListed(15),"ordinary Bose destination remains available to classify");
    check(!IsRevisitScene("mp1000") && !IsRevisitScene("mp5000"),"full directory does not turn ordinary towns into recovery-only maps");
#if SKY2_HAS_FULL_TRAVEL_CATALOG
    std::set<uint32_t> nativeIds;
    std::set<uint32_t> displayIds;
    for(const auto& row:kTravelCatalog) {
        nativeIds.insert(row.id);
        if (DisplayableTravelRow(row)) displayIds.insert(row.id);
    }
    check(count==displayIds.size()+2,"player destinations plus distinct forest entry and exact return displayed");
    // 回归用户反馈的原生内部入口；荣耀号等已适配例外不能随内部行一起删除。
    check(!RevisitDestinationListed(157) && !RevisitDestinationListed(164),
          "internal Malga mine and Elmo source entry-only records stay out of player list");
    check(RevisitDestinationListed(165) && RevisitDestinationListed(166) &&
          RevisitDestinationListed(97) && RevisitDestinationListed(120),
          "reviewed Glorious prologue and laboratory exceptions survive display cleanup");
    check(!RevisitDestinationListed(167) && !RevisitDestinationListed(168),
          "unadapted duplicate internal Glorious entrances stay hidden");
    check(RevisitDestinationListed(35) && RevisitDestinationListed(92),
          "normally named special maps are not erased just because preview is absent");
    for (size_t i=0;i<count;++i)
        check(std::strstr(RevisitDestinationAt(i).name,"◆")==nullptr &&
              std::strstr(RevisitDestinationAt(i).name,"入口用")==nullptr,
              "player-facing names never expose internal entry-only labels");
    check(nativeIds.count(forest::kTarget)==0 && RevisitDestinationListed(forest::kTarget),
          "forest Mod target never pretends to be a native destination ID");
    check(IsRevisitScene(forest::kScene),"forest reload requires existing return history");
    check(nativeIds.count(108)!=0,"Cradle native point present for city hall route");
    check(RevisitPageSelection(count-1,true)==count-1,"last page does not wrap across many pages");
    check(RevisitPageSelection(0,false)==0,"first page does not wrap across many pages");
    check(RevisitPageSelection(count+123,true)==count-1,"stale out-of-range selection is clamped");
#endif
    std::printf("%zu displayed destinations, %zu pages; %u travel catalog failure(s)\n",
                count,(count+kRevisitRowsPerPage-1)/kRevisitRowsPerPage,failures);
    return failures?1:0;
}
