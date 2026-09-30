// 迷途之森只抑制已经完成的谜题事件盒重新启用，保留地图、箱子及原生脚本生命周期。
#include "revisit_forest.h"
#include "revisit_forest_rules.h"
#include "revisit_event_guard.h"
#include "tracker.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>

extern "C" {
void Sky2ForestEnableShim();
void* Sky2NextForestEnable=nullptr;
}

namespace tracker {
namespace {
uintptr_t g_forestBase=0;
std::atomic<bool> g_forestInstalled{false};
std::atomic<bool> g_forestReported{false};

// 读取失败时仅放弃本次局部过滤；不吞原生异常，也不伪造脚本或地图加载成功。
bool ForestReadBytes(uintptr_t address,void* output,size_t size) noexcept {
    if (address<0x10000 || size>std::numeric_limits<uintptr_t>::max()-address) return false;
    __try { std::memcpy(output,reinterpret_cast<const void*>(address),size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool ForestRead(uintptr_t address,T& output) noexcept {
    return ForestReadBytes(address,&output,sizeof(output));
}
template<size_t N> bool ForestString(uintptr_t address,char (&output)[N]) noexcept {
    // 短字符串逐字节读至 NUL，不能为了读取固定上限而越过合法字符串所在页。
    for (size_t i=0;i<N;++i) {
        if (address>std::numeric_limits<uintptr_t>::max()-i || !ForestRead(address+i,output[i])) return false;
        if (!output[i]) return true;
    }
    return false;
}
template<size_t N> bool ForestMatches(uintptr_t address,const unsigned char (&expected)[N]) noexcept {
    unsigned char actual[N]{};
    return ForestReadBytes(address,actual,N) && !std::memcmp(actual,expected,N);
}

bool ForestContext(uintptr_t& field) noexcept {
    uintptr_t savedata=0;
    uint32_t length=0,taggedChapter=0;
    char scene[32]{};
    std::array<uint8_t,4096> flags{};
    // 此入口会在换图初始化和读档时执行，不能要求 busy==0、有效玩家、mini-map
    // 或当前脚下地点已经就绪。加载器 29A691 先清空旧事件盒数组，29AD16..38
    // 复制目标场景名，29B351 再调用 system.MapReinit；因此此时当前场景名与
    // 当前数组足以确认对象归属。依赖稳态的旧图保护反而会错过第一次 OBJECT_setting。
    // 已审核的四十个剧情盒资源默认 flag=2、type=0；原生装载器 27EA08 读入
    // flag 后又在 27EA20 对 type=0 置 bit2，所以 Reinit 之前它们本就禁用。
    // 此钩子阻止后续显式重新启用，而不是等玩家已碰到机关后再逐帧补救。
    if (!ForestRead(g_forestBase+0xC60E08,field) || field<0x10000 ||
        !ForestRead(field+0x190,length) || length!=6 ||
        !ForestString(field+0x170,scene) || std::strcmp(scene,forest::kScene) ||
        !ForestRead(g_forestBase+0xC60E58,savedata) || savedata<0x10000 ||
        !ForestRead(savedata+0x11100+12*4,taggedChapter) ||
        taggedChapter<forest::kIntegerTag || taggedChapter>(forest::kIntegerTag|9u) ||
        !ForestReadBytes(savedata+0x100,flags.data(),flags.size())) return false;
    // 消费者在原生加载前开启实验行程、精确返程前关闭；正常剧情进入森林时不会
    // 因为安装了实验 DLL 就失去谜题，仍只拦截下方逐个核对的四十个事件盒。
    return forest::ProtectionAllowed(taggedChapter&0x3FFFFFFFu,flags.data(),flags.size(),
                                     ExperimentalRevisitTripActive(taggedChapter&0x3FFFFFFFu));
}

bool ForestContainsEvent(uintptr_t field,const char* name) noexcept {
    uintptr_t array=0;
    uint64_t count=0;
    if (!ForestRead(field+0x260,array) || array<0x10000 ||
        !ForestRead(field+0x268,count) || !count || count>4096 ||
        count>(std::numeric_limits<uintptr_t>::max()-array)/sizeof(uintptr_t)) return false;
    // 与 49E2AB..49E315 原生名称查找一致，只接受当前场景真实登记的同名对象。
    // 不解引用目标对象之外的脚本、成员列表或碰撞结构，不修改 EventBox 自身。
    for (uint64_t i=0;i<count;++i) {
        uintptr_t object=0;
        char actual[64]{};
        if (!ForestRead(array+static_cast<uintptr_t>(i)*sizeof(uintptr_t),object) ||
            object<0x10000 || !ForestString(object+0x10,actual)) return false;
        if (!std::strcmp(name,actual)) return true;
    }
    return false;
}

bool ForestDisableTemporaryArgument(uintptr_t address) noexcept {
    __try {
        auto* value=reinterpret_cast<uint32_t*>(address);
        if (*value!=(forest::kIntegerTag|1u)) return false;
        *value=forest::kIntegerTag;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

// RDX 是 Cmd_map_03 的 VM context。脚本包装函数把两个实参复制到值栈，
// builtin 从栈顶 -4 读取名字、-8 读取 enable；只改后者本次调用的 tagged 1。
// 原生接着自己设置 EventBox.disabled(bit2)、刷新碰撞并正常返回 VM。成功/失败
// 旧剧情、WrongWay 和 Hint 均不会启动，218..222、BP 与物品/宝箱记录没有任何补写。
extern "C" void Sky2ForestBeforeEnable(uintptr_t context) noexcept {
    if (!g_forestInstalled.load(std::memory_order_relaxed)) return;
    uint32_t count=0,enable=0,nameTag=0;
    int32_t top=0;
    uintptr_t stack=0,script=0,scriptBytes=0;
    char name[64]{};
    if (!ForestRead(context+0x70,count) || count!=2 ||
        !ForestRead(context+0x64,top) || top<8 || top>0x400000 || (top&3) ||
        !ForestRead(context+0x58,stack) || stack<0x10000 || (stack&3) ||
        static_cast<uintptr_t>(top)>std::numeric_limits<uintptr_t>::max()-stack) return;
    const uintptr_t argument=stack+static_cast<uintptr_t>(top)-8;
    if (!ForestRead(argument,enable) || enable!=(forest::kIntegerTag|1u) ||
        !ForestRead(argument+4,nameTag) || (nameTag&0xC0000000u)!=0xC0000000u) return;
    const uintptr_t offset=nameTag&0x3FFFFFFFu;
    if (!offset || offset>=0x1000000 ||
        !ForestRead(context+8,script) || script<0x10000 ||
        !ForestRead(script,scriptBytes) || scriptBytes<0x10000 ||
        offset>std::numeric_limits<uintptr_t>::max()-scriptBytes ||
        !ForestString(scriptBytes+offset,name) || !forest::ProtectedEventBox(name)) return;
    uintptr_t field=0;
    if (!ForestContext(field) || !ForestContainsEvent(field,name)) return;
    if (ForestDisableTemporaryArgument(argument) && !g_forestReported.exchange(true))
        Log("Revisit: scoped Mistwald puzzle triggers stay disabled; native map and chest scripts remain active.");
}

bool InstallForestRevisitGuard(uintptr_t gameBase) noexcept {
    if (g_forestInstalled.load()) return g_forestBase==gameBase;
    g_forestBase=gameBase;
    // 验证入口的 VM 参数布局、启用/禁用两条原生尾跳及加载期场景身份提交顺序。
    // 任意字节不符均不安装，调用方必须因 Ready()==false 而禁止迷途之森目标。
    const unsigned char entry[]={
        0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x8B,0x52,0x70,0x85,0xD2,0x74,0x1C,
        0x48,0x63,0x4F,0x64,0x48,0x8B,0x47,0x58,0x8B,0x44,0x01,0xFC,0x8B,0xC8};
    const unsigned char operation[]={
        0x49,0x8B,0x09,0x48,0x85,0xC9,0x74,0x3A,0x8B,0x81,0xE8,0x01,0,0,0x85,0xDB,0x74,0x18,
        0x83,0xE0,0xFD,0x89,0x81,0xE8,0x01,0,0,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,
        0x5F,0xE9,0xF1,0xF6,0xDD,0xFF,0x83,0xC8,0x02,0x89,0x81,0xE8,0x01,0,0,
        0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xE9,0xD9,0xF6,0xDD,0xFF};
    const unsigned char sceneCommit[]={
        0x0F,0x10,0x86,0x94,0x01,0,0,0x0F,0x11,0x86,0x70,0x01,0,0,
        0x0F,0x10,0x8E,0xA4,0x01,0,0,0x0F,0x11,0x8E,0x80,0x01,0,0,
        0x8B,0x86,0xB4,0x01,0,0,0x89,0x86,0x90,0x01,0,0};
    if (!ForestMatches(gameBase+forest::kEnableBuiltinRva,entry) ||
        !ForestMatches(gameBase+0x49E315,operation) || !ForestMatches(gameBase+0x29AD16,sceneCommit)) {
        Log("Revisit: Mistwald enable builtin validation failed; this destination stays unavailable.");
        return false;
    }
    void* target=reinterpret_cast<void*>(gameBase+forest::kEnableBuiltinRva);
    if (MH_CreateHook(target,reinterpret_cast<void*>(&Sky2ForestEnableShim),&Sky2NextForestEnable)!=MH_OK) return false;
    if (MH_EnableHook(target)!=MH_OK) { MH_RemoveHook(target); return false; }
    g_forestInstalled.store(true);
    Log("Revisit: Mistwald puzzle guard is ready, including native load/reload initialization.");
    return true;
}

bool ForestRevisitGuardReady() noexcept { return g_forestInstalled.load(); }
}
