// 用测试进程自行分配的内存调用生产过滤器；不连接游戏、不安装挂钩、不读取存档。
#include "../native/revisit_forest.cpp"
#include <cstdio>
#include <string>
#include <vector>
namespace tracker {
void Log(const char*) noexcept {}
// 此测试只链接森林保护器，用明确的输入替身模拟消费者开关；实际原子实现由
// event_guard_runtime 同源双构建验证，避免把两套 MinHook 桥混入本测试目标。
static bool g_testExperimentalTrip=false;
static uint32_t g_testExperimentalChapter=UINT32_MAX,g_testObservedChapter=UINT32_MAX;
bool ExperimentalRevisitTripActive(uint32_t chapter) noexcept {
    g_testObservedChapter=chapter;
    if (!revisit_policy::kUnrestricted || !g_testExperimentalTrip) return false;
    if (chapter==UINT32_MAX) return true;
    if (chapter>9) return false;
    if (chapter!=g_testExperimentalChapter) { g_testExperimentalTrip=false;return false; }
    return true;
}
}
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",label); }
}
template<class T> static void Put(std::vector<uint8_t>& b,size_t at,T value) {
    std::memcpy(b.data()+at,&value,sizeof(value));
}
static uintptr_t Address(std::vector<uint8_t>& b) { return reinterpret_cast<uintptr_t>(b.data()); }
struct Fixture {
    std::vector<uint8_t> image=std::vector<uint8_t>(0xC70000);
    std::vector<uint8_t> field=std::vector<uint8_t>(0x1C00);
    std::vector<uint8_t> save=std::vector<uint8_t>(0x11200);
    std::vector<uint8_t> context=std::vector<uint8_t>(0x80);
    std::vector<uint8_t> script=std::vector<uint8_t>(0x20);
    std::vector<uint8_t> bytes=std::vector<uint8_t>(0x100);
    std::vector<uint8_t> stack=std::vector<uint8_t>(0x40,0xA5);
    std::vector<uint8_t> objects=std::vector<uint8_t>(2*sizeof(uintptr_t));
    std::vector<uint8_t> event=std::vector<uint8_t>(0x200,0x35);
    std::vector<uint8_t> ordinary=std::vector<uint8_t>(0x200,0x71);
    explicit Fixture(const char* name="EV_Start",uint32_t chapter=9) {
        tracker::g_forestBase=Address(image); tracker::g_forestInstalled.store(true);
        tracker::g_testExperimentalTrip=false;
        tracker::g_testExperimentalChapter=UINT32_MAX;tracker::g_testObservedChapter=UINT32_MAX;
        Put(image,0xC62458,Address(field)); Put(image,0xC624A8,Address(save));
        std::memcpy(field.data()+0x168,"mp0081",7); Put(field,0x188,uint32_t{6});
        // 刻意使用加载期 busy 状态且无玩家、主地图、脚下地点，验证不会漏掉首次 Reinit。
        Put(field,0x1BC0,uint32_t{0x4001});
        Put(field,0x258,Address(objects)); Put(field,0x260,uint64_t{2});
        Put(objects,0,Address(ordinary)); Put(objects,sizeof(uintptr_t),Address(event));
        std::memcpy(event.data()+0x10,name,std::strlen(name)+1);
        std::memcpy(ordinary.data()+0x10,"Treasure00",11);
        Put(save,0x11100+12*4,0x40000000u|chapter);
        for (unsigned flag : {20064u,20067u}) save[0x100+flag/8]|=static_cast<uint8_t>(1u<<(flag%8));
        Put(context,8,Address(script)); Put(script,0,Address(bytes));
        std::memcpy(bytes.data()+0x30,name,std::strlen(name)+1);
        Put(context,0x58,Address(stack)); Put(context,0x64,int32_t{0x20});
        Put(context,0x70,uint32_t{2}); Put(stack,0x18,uint32_t{0x40000001});
        Put(stack,0x1C,uint32_t{0xC0000030});
    }
    uint32_t Argument() const { uint32_t v=0; std::memcpy(&v,stack.data()+0x18,4); return v; }
    void Run() { tracker::Sky2ForestBeforeEnable(Address(context)); }
    void ExpectUnchanged(const char* label) {
        const auto old=stack; Run(); Check(stack==old,label);
    }
};
// 直接调用安装器实际使用的只读验证路径。RVA 和片段来自 Ver.1.05 的独立
// 静态核验，不能用生产常量填充假映像，否则入口常量遗漏升级时测试也会误通过。
static void CheckNativeLayout() {
    std::vector<uint8_t> image(0x500000);
    const uint8_t entry[]={
        0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x8B,0x52,0x70,0x85,0xD2,0x74,0x1C,
        0x48,0x63,0x4F,0x64,0x48,0x8B,0x47,0x58,0x8B,0x44,0x01,0xFC,0x8B,0xC8
    };
    std::memcpy(image.data()+0x49F640,entry,sizeof(entry));
    const uint8_t operation[]={
        0x49,0x8B,0x09,0x48,0x85,0xC9,0x74,0x3A,0x8B,0x81,0xE8,0x01,0x00,0x00,0x85,0xDB,
        0x74,0x18,0x83,0xE0,0xFD,0x89,0x81,0xE8,0x01,0x00,0x00,0x48,0x8B,0x5C,0x24,0x30,
        0x48,0x83,0xC4,0x20,0x5F,0xE9,0x21,0xF4,0xDD,0xFF,0x83,0xC8,0x02,0x89,0x81,0xE8,
        0x01,0x00,0x00,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xE9,0x09,0xF4,
        0xDD,0xFF
    };
    std::memcpy(image.data()+0x49F755,operation,sizeof(operation));
    const uint8_t sceneCommit[]={
        0x0F,0x10,0x86,0x8C,0x01,0x00,0x00,0x0F,0x11,0x86,0x68,0x01,0x00,0x00,0x0F,0x10,
        0x8E,0x9C,0x01,0x00,0x00,0x0F,0x11,0x8E,0x78,0x01,0x00,0x00,0x8B,0x86,0xAC,0x01,
        0x00,0x00,0x89,0x86,0x88,0x01,0x00,0x00
    };
    std::memcpy(image.data()+0x29BEF6,sceneCommit,sizeof(sceneCommit));
    Check(tracker::ValidateForestNativeLayout(Address(image)),"Ver.1.05 forest install signatures accepted");
    // 新入口为空但旧入口有完全相同的片段，也不能按旧地址安装。
    std::memcpy(image.data()+0x49EA80,entry,sizeof(entry));
    image[0x49F640]^=0xFF;
    Check(!tracker::ValidateForestNativeLayout(Address(image)),"previous forest entry never substitutes current entry");
    image[0x49F640]^=0xFF;
    image[0x29BEF6+39]^=0xFF;
    Check(!tracker::ValidateForestNativeLayout(Address(image)),"complete scene commit displacement is validated");
}

int main() {
    CheckNativeLayout();
    using namespace tracker;
    std::vector<std::string> names={"EV_Start","EV_04_34_00","EV_04_34_01"};
    for (unsigned i=0;i<=11;++i) { char n[32]{}; std::snprintf(n,sizeof(n),"EV_WrongWay_%02u",i); names.emplace_back(n); }
    for (unsigned i=1;i<=25;++i) { char n[32]{}; std::snprintf(n,sizeof(n),"EV_Hint_%02u",i); names.emplace_back(n); }
    for (unsigned chapter : {8u,9u}) for (const auto& name : names) {
        Fixture f(name.c_str(),chapter);
        const auto save=f.save,field=f.field,event=f.event,ordinary=f.ordinary,bytes=f.bytes;
        auto expected=f.stack; Put(expected,0x18,uint32_t{0x40000000});
        f.Run();
        Check(f.stack==expected,"only exact four-byte temporary enable changes");
        Check(f.save==save && f.field==field && f.event==event && f.ordinary==ordinary && f.bytes==bytes,
              "save, scene, event objects, boxes and script bytes remain unchanged");
        f.ExpectUnchanged("already disabled call stays unchanged");
    }
    for (const char* name : {"ActiveVoice","Treasure00","go_mp0000","EV_Hint_00","EV_Hint_26","EV_Start_extra"}) {
        Fixture f(name); f.ExpectUnchanged("unreviewed event and ordinary chest unaffected");
    }
    { Fixture f; Put(f.field,0x1BC0,uint32_t{0}); f.Run(); Check(f.Argument()==0x40000000,"post-load reinit equally guarded"); }
    { Fixture f("EV_Start",4); f.ExpectUnchanged("normal chapter-four puzzle untouched"); }
    { Fixture f; g_forestInstalled.store(false); f.ExpectUnchanged("uninstalled helper never writes"); }
    for (unsigned flag : {20064u,20067u}) {
        Fixture f; f.save[0x100+flag/8]&=static_cast<uint8_t>(~(1u<<(flag%8)));
        f.ExpectUnchanged("each completion gate is required");
    }
    { Fixture f; std::memcpy(f.field.data()+0x168,"mp0000",7); f.ExpectUnchanged("same event name in another scene untouched"); }
    { Fixture f; Put(f.field,0x188,uint32_t{7}); f.ExpectUnchanged("inconsistent scene length denied"); }
    { Fixture f; Put(f.context,0x70,uint32_t{3}); f.ExpectUnchanged("unknown builtin arity denied"); }
    { Fixture f; Put(f.context,0x64,int32_t{4}); f.ExpectUnchanged("stack top underflow denied"); }
    { Fixture f; Put(f.context,0x64,int32_t{-4}); f.ExpectUnchanged("negative stack top denied"); }
    { Fixture f; Put(f.context,0x64,int32_t{0x400004}); f.ExpectUnchanged("excessive stack top denied"); }
    { Fixture f; Put(f.context,0x64,int32_t{0x21}); f.ExpectUnchanged("unaligned stack top denied"); }
    { Fixture f; Put(f.context,0x58,Address(f.stack)+1); f.ExpectUnchanged("unaligned stack pointer denied"); }
    { Fixture f; Put(f.stack,0x18,uint32_t{0x40000002}); f.ExpectUnchanged("noncanonical enable integer denied"); }
    { Fixture f; Put(f.stack,0x18,uint32_t{0x8FE00000}); f.ExpectUnchanged("float enable is not rewritten"); }
    { Fixture f; Put(f.stack,0x1C,uint32_t{0x40000030}); f.ExpectUnchanged("integer cannot masquerade as name"); }
    { Fixture f; Put(f.stack,0x1C,uint32_t{0xC0000000}); f.ExpectUnchanged("null name offset denied"); }
    { Fixture f; Put(f.stack,0x1C,uint32_t{0xC1000000}); f.ExpectUnchanged("unbounded name offset denied"); }
    { Fixture f; std::memset(f.bytes.data()+0x30,'x',64); f.ExpectUnchanged("unterminated name denied"); }
    { Fixture f; Put(f.field,0x260,uint64_t{0}); f.ExpectUnchanged("cleared old scene object list denied"); }
    { Fixture f; Put(f.field,0x260,uint64_t{4097}); f.ExpectUnchanged("unbounded object list denied"); }
    { Fixture f; std::memcpy(f.event.data()+0x10,"OtherBox",9); f.ExpectUnchanged("name must identify registered current object"); }
    { Fixture f; Put(f.objects,sizeof(uintptr_t),uintptr_t{0}); f.ExpectUnchanged("null registered object denied"); }
    // 新实例模拟重读旧图存档：没有活动回访会话或上次进程内状态，完成门槛仍能保护初始化。
    { Fixture f; g_forestReported.store(true); f.Run(); Check(f.Argument()==0x40000000,"reload requires no in-memory visit session"); }
    // 实验跨章扩展必须在加载器第一次 Reinit 前生效；原生消费者负责先开启行程。
    // 逐个覆盖四十个名称，确认未完成旗标保持不变，而普通主线和返程恢复谜题。
    for (const auto& name : names) {
        Fixture f(name.c_str(),4);
        std::memset(f.save.data()+0x100,0,4096);
        const auto before=f.stack,save=f.save,event=f.event,field=f.field;
        f.ExpectUnchanged("early forest without explicit trip retains native puzzle");
        g_testExperimentalTrip=true;g_testExperimentalChapter=4;
        f.Run();
        Check(f.Argument()==(revisit_policy::kUnrestricted?0x40000000u:0x40000001u),
              "active experimental trip protects all reviewed forest events during load");
        Check(f.save==save && f.event==event && f.field==field,
              "experimental forest changes no persistent state or scene objects");
        f.stack=before; g_testExperimentalTrip=false;
        f.ExpectUnchanged("disarm before precise return restores normal forest initialization");
    }
    for (const char* name : {"EV_Hint_26","EV_Start_extra","Treasure00","go_mp0000"}) {
        Fixture f(name,4); g_testExperimentalTrip=true;g_testExperimentalChapter=4;
        f.ExpectUnchanged("experimental trip never suppresses unreviewed event or chest");
    }
    for (const uint32_t chapter : {4u,0x4000000Au,0xC0000004u}) {
        Fixture f; g_testExperimentalTrip=true;g_testExperimentalChapter=9;
        Put(f.save,0x11100+12*4,chapter);
        f.ExpectUnchanged("experimental forest rejects invalid chapter encoding");
    }
    {
        Fixture f("EV_Start",4);
        std::memset(f.save.data()+0x100,0,4096);
        g_testExperimentalTrip=true;g_testExperimentalChapter=1;
        f.ExpectUnchanged("early forest chapter switch keeps normal puzzle without UI update");
        Check(g_testObservedChapter==4,"forest protection queries the freshly read untagged chapter");
        Check(!revisit_policy::kUnrestricted || !g_testExperimentalTrip,
              "forest callback retires the previous chapter trip");
        Put(f.save,0x11100+12*4,uint32_t{0x40000001});
        f.ExpectUnchanged("forest reload of previous chapter cannot revive retired trip");
    }
    {
        Fixture f;
        auto* page=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        Check(page!=nullptr,"allocate isolated inaccessible-page fixture");
        if (page) {
            DWORD previous=0;
            VirtualProtect(page,4096,PAGE_NOACCESS,&previous);
            const auto old=f.stack;
            Sky2ForestBeforeEnable(reinterpret_cast<uintptr_t>(page));
            Check(f.stack==old,"unreadable context denied without crashing");
            Put(f.context,0x58,reinterpret_cast<uintptr_t>(page));
            f.ExpectUnchanged("unreadable argument stack denied");
            VirtualFree(page,0,MEM_RELEASE);
        }
    }
    std::printf("Forest runtime: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
