// 不依赖游戏的纯规则测试：保护范围必须只含已审核的四十个事件盒。
#include "../native/revisit_forest_rules.h"
#include <array>
#include <cstdio>
#include <string>
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",label); }
}
int main() {
    using namespace tracker::forest;
    unsigned matched=0;
    for (unsigned i=0;i<100;++i) {
        char name[64]{};
        std::snprintf(name,sizeof(name),"EV_WrongWay_%02u",i);
        Check(ProtectedEventBox(name)==(i<=11),name);
        matched+=ProtectedEventBox(name)?1u:0u;
        std::snprintf(name,sizeof(name),"EV_Hint_%02u",i);
        Check(ProtectedEventBox(name)==(i>=1 && i<=25),name);
        matched+=ProtectedEventBox(name)?1u:0u;
    }
    for (const char* name : {"EV_Start","EV_04_34_00","EV_04_34_01"}) {
        Check(ProtectedEventBox(name),name); ++matched;
    }
    Check(matched==40,"exact forty reviewed names");
    for (const char* name : {"", "EV_WrongWay", "EV_WrongWay_0", "EV_WrongWay_000",
             "EV_WrongWay_-1", "EV_Hint_1", "EV_Hint_25_extra", "EV_Hint_+1",
             "EV_04_32_01", "EV_04_34_02", "EV_Start_extra", "ActiveVoice", "Treasure00",
             "go_mp0000", "ev_Start", "EV_Hint_0a"}) Check(!ProtectedEventBox(name),name);
    std::array<uint8_t,4096> flags{};
    auto set=[&](unsigned flag) { flags[flag/8]|=static_cast<uint8_t>(1u<<(flag%8)); };
    Check(!StoryComplete(8,flags.data(),flags.size()),"incomplete story denied");
    set(20064);
    Check(!StoryComplete(9,flags.data(),flags.size()),"barrier ending required");
    set(20067);
    Check(StoryComplete(8,flags.data(),flags.size()),"chapter eight completed story");
    Check(StoryComplete(9,flags.data(),flags.size()),"chapter nine completed story");
    Check(!StoryComplete(4,flags.data(),flags.size()),"original chapter puzzle unchanged");
    Check(!StoryComplete(0x40000009,flags.data(),flags.size()),"no implicit tagged chapter normalization");
    Check(!StoryComplete(9,nullptr,flags.size()),"null flags denied");
    Check(!StoryComplete(9,flags.data(),flags.size()-1),"truncated flags denied");
    // 不要求成功/失败任选分支都发生；只要求正常原生剧情已经收尾。
    Check(!((flags[20065/8]>>(20065%8))&1) && !((flags[20066/8]>>(20066%8))&1),"optional branch flags absent");
    Check(MatchesPlace(1008100,0,0,"mp0081"),"unique static place");
    Check(!MatchesPlace(1008100,1,0,"mp0081"),"display region not accepted as raw region");
    Check(!MatchesPlace(1008100,0,1,"mp0081"),"unknown variant denied");
    Check(!MatchesPlace(1008101,0,0,"mp0081"),"unknown place denied");
    Check(!MatchesPlace(1008100,0,0,"mp0000"),"overworld forest remains distinct");
    // 未完成的主线森林不能因为构建选项开启就失去谜题；必须同时有明确实验行程。
    flags.fill(0);
    for (unsigned chapter=0;chapter<=9;++chapter) {
        Check(!ProtectionAllowed(chapter,flags.data(),flags.size(),false),"normal unfinished forest retains puzzle");
        Check(ProtectionAllowed(chapter,flags.data(),flags.size(),true)==tracker::revisit_policy::kUnrestricted,
              "only unrestricted active trip expands protection");
    }
    Check(!ProtectionAllowed(10,flags.data(),flags.size(),true),"experimental unknown chapter denied");
    Check(!ProtectionAllowed(4,nullptr,flags.size(),true),"experimental null flags denied");
    Check(!ProtectionAllowed(4,flags.data(),flags.size()-1,true),"experimental partial flags denied");
    std::printf("Forest pure rules: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
