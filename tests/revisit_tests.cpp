// 回访确认不会直接操作游戏；验证过期、换目标与取消等容易造成意外跳转的边界。
#include "revisit_logic.h"
#include "revisit_phase_rules.h"
#include <array>
#include <cstdio>
using namespace tracker;
int main() {
    unsigned failures = 0;
    const auto check = [&](bool value, const char* description) {
        if (!value) { std::printf("FAIL: %s\n", description); ++failures; }
    };
    RevisitConfirmation confirmation;
    check(!confirmation.Press(99, 100), "第一次按下只进入确认");
    check(confirmation.Armed(99, 101), "确认仅绑定所选目的地");
    check(!confirmation.Press(101, 102), "更换目标不会沿用旧确认");
    check(confirmation.Press(101, 103), "同一目标第二次按下才提交");
    check(!confirmation.Armed(101, 104), "提交后立即解除确认");
    check(!confirmation.Press(15, 1000), "返程同样需要二次确认");
    check(!confirmation.Press(15, 9000), "到达超时边界时重新确认");
    confirmation.Cancel();
    check(!confirmation.Press(15, 9001), "关闭地图或读档取消旧确认");
    check(IsRevisitScene("mp6013_01") && IsRevisitScene("mp6010_01"), "序章内部场景均被识别");
    check(!IsRevisitScene("mp1000") && !IsRevisitScene("mp6013_02") && !IsRevisitScene(nullptr),
          "当前正常地区、相似名称和空指针不冒充回访场景");
    check(IsRevisitScene("mp6100_01") && IsRevisitScene("mp8500_01") &&
          IsRevisitScene("mp8500_02"), "研究所和荣耀号内部可识别为回访场景");
    check(IsRevisitScene("mp0054_01") && IsRevisitScene("mp1073_01") &&
          IsRevisitScene("mp2084_01") && IsRevisitScene("mp3044_01") &&
          IsRevisitScene("mp2072"), "四塔异空间及旧校舍使用准确场景名");
    check(IsRevisitScene("mp0081") && !IsRevisitScene("mp0081_01"),
          "迷途之森使用准确场景名并参与读档返程保护");
    check(!IsRevisitScene("") && !IsRevisitScene("mp2081_01"),
          "返程哨兵与普通塔不会被误认为旧地图回访目标");
    check(kRevisitDestinations[kRevisitDestinationCount - 1].id == kRevisitReturnTarget,
          "精确返程哨兵必须位于目录最后");
    unsigned emergencyReturns = 0, exactReturns = 0;
    for (size_t i = 0; i < kRevisitDestinationCount; ++i) {
        const auto& destination = kRevisitDestinations[i];
        check(destination.group && destination.group[0] && destination.name && destination.name[0],
              "每个目录条目有可展示的分组和名称");
        emergencyReturns += destination.id == 15;
        exactReturns += destination.id == kRevisitReturnTarget;
        for (size_t j = 0; j < i; ++j)
            check(destination.id != kRevisitDestinations[j].id,
                  "目的地编号不可重复，防止确认指向另一个条目");
    }
    check(emergencyReturns == 1 && exactReturns == 1, "旧版应急返程与准确返程分别唯一");
    check(!confirmation.Press(kRevisitReturnTarget, 12000), "准确返程第一次按下只确认");
    check(!confirmation.Press(15, 12001), "切换到应急返程不能继承准确返程确认");
    check(!confirmation.Press(kRevisitReturnTarget, 12002), "切回准确返程也需要重新确认");
    check(confirmation.Press(kRevisitReturnTarget, 12003), "准确返程仅在同目标再次按下后提交");

    // 不依赖游戏资源的公开回归：覆盖曾导致整章误禁用、把逻辑与误读为或、
    // 以及低编号旗标遗漏的关键场景。这里只构造最小剧情前提，不复写规则实现。
    std::array<uint8_t,4096> flags{};
    const auto setFlag=[&](uint32_t id) { flags[id/8]|=static_cast<uint8_t>(1u<<(id%8)); };
    const auto blocked=[&](uint32_t chapter,uint32_t target) {
        return revisit_phase::BeforeScriptBlocked(chapter,target,flags.data(),flags.size());
    };
    setFlag(17016);
    check(blocked(1,53) && !blocked(1,37) && !blocked(1,43),
        "第一章目标53的前置剧情不能连带禁用卢安其他普通点");
    setFlag(17017);
    check(!blocked(1,53),"第一章前置完成后解除对应目标限制");

    flags.fill(0);setFlag(16019);
    check(blocked(0,99) && blocked(0,97),
        "序章训练由剧情接管水道目标并拒绝其他地点，不可直接绕过");
    setFlag(16021);
    check(!blocked(0,99) && !blocked(0,97),"序章训练完成后普通前置限制解除");

    flags.fill(0);setFlag(20027);
    check(!blocked(4,12) && !blocked(4,13) && !blocked(4,14) && blocked(4,11),
        "第四章同时保留12、13、14三个原生豁免，其他目标仍受剧情接管");
    setFlag(20028);
    check(!blocked(4,11),"第四章完成旗标终止该前置分支");

    flags.fill(0);setFlag(1073);
    check(blocked(3,79) && blocked(3,80),"第1073号低编号旗标仍可接管第三章各目标");
    setFlag(19072);
    check(!blocked(3,79) && !blocked(3,80),"第三章对应完成旗标解除低编号前置限制");
    check(revisit_phase::BeforeScriptBlocked(1,37,nullptr,flags.size()) &&
        revisit_phase::BeforeScriptBlocked(1,37,flags.data(),flags.size()-1) &&
        revisit_phase::BeforeScriptBlocked(10,37,flags.data(),flags.size()),
        "缺失或不完整旗标及未知章节不能被当作无前置剧情");
    std::printf("%u revisit failure(s)\n", failures);
    return failures ? 1 : 0;
}
