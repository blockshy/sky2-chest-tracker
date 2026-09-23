// 仅适配已经核对 SHA-256 的游戏 1.03.2。回访通过原生菜单退出结果交接，
// 不在渲染线程调用换图，也不写角色坐标、章节、到访记录或存档。
#include "revisit_native.h"
#include "revisit_catalog.h"
#include "revisit_event_guard_rules.h"
#include "revisit_event_guard.h"
#include "revisit_forest.h"
#include "revisit_forest_rules.h"
#include "revisit_phase_rules.h"
#include "revisit_policy.h"
#include "tracker.h"
#include <MinHook.h>
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <mutex>

extern "C" {
void Sky2RevisitUpdateShim();
void Sky2RevisitJumpShim();
void* Sky2NextRevisitUpdate = nullptr;
void* Sky2NextRevisitJump = nullptr;
}

namespace tracker {
namespace {
uintptr_t base = 0;
std::atomic<bool> available{false};
std::atomic<RevisitNativeAuthorize> authorize{nullptr};
std::mutex stateMutex;
RevisitNativeContext published{};
RevisitNativeStatus status{};
RevisitNativeContext expectedContext{};
RevisitReturnPoint publishedPoint{}, requestedReturn{}, arrivalPoint{};
bool publishedPointValid = false, arrivalPointValid = false, sawLoadTransition = false;
ULONGLONG dispatchedAt = 0;
unsigned arrivalFrames = 0;
struct LoadDescriptor { char scene[32]{};float xyz[3]{},yaw=0;bool valid=false; };
LoadDescriptor beforeLoadDescriptor{};
RevisitReturnPoint beforeLoadPosition{};
bool beforeLoadPositionValid=false;
// 此入口只在最终原生菜单消费者线程调用。测试可以替换为自己的记录器，
// 正式构建由安装时绑定到已验证的 29CF90，不暴露给外部模块。
using NativeLoad = void (*)(uintptr_t, const char*, const char*, const float*, float, uint32_t);
NativeLoad nativeLoad = nullptr;
ULONGLONG requestedAt = 0;
ULONGLONG publishedAt = 0;
// 已写入原生退出结果时，单独保留交接身份。即使状态改为过期/拒绝，也必须先
// 清除该结果或在原生消费者处拦截，不能忘记它而让原生下一帧自行传送。
bool dispatchArmed = false;
// 这些地址数值仅作身份比较；每次访问对象均重新从当前原生调用/全局关系读取。
// 不通过缓存地址访问对象，防止关图后分配器复用地址时误操作另一张菜单。
uintptr_t handoffMenuIdentity = 0, handoffMinimapIdentity = 0;
// 诊断只记录状态变化，避免每帧打印或泄露原始地址；最多每秒一条、每次进程128条。
enum class CaptureIssue : uint32_t { None, Field, Scene, PlaceTable, Region, Story, Minimap, Changed };
CaptureIssue captureIssue=CaptureIssue::None;
uint32_t pointIssue=0;
int32_t diagnosticMenuState=-1;
uint64_t diagnosticKey=~0ull;
ULONGLONG diagnosticAt=0;
unsigned diagnosticCount=0;
struct RuleProof {
    bool valid=false;
    uintptr_t manager=0;
    uint32_t chapter=0,region=0;
    uint64_t signature=0;
    char scene[32]{};
};
RuleProof ruleProof{};
enum NativeRule : uint8_t { RuleUnknown,RuleAllowed,RuleUnregistered,RuleBlocked,RuleAreaBlocked,
                           RuleInternal,RuleBeforeScript,RuleInconsistent };
struct RuleSpot { uint32_t id,area,region;uint8_t visible,blocked,registered,reserved; };
struct RuleArea { uint32_t id,region;uint8_t visible,blocked,reserved[2]; };
static_assert(sizeof(RuleSpot)==16 && sizeof(RuleArea)==12);

// 下列完成旗标来自序章 t_evtable；16044 是可选重赛，不属于正常必过条件。
// 特殊宝箱附带事件另由专门保护层处理，不会全局拦截场景事件；
// 此处只证明玩家确实已经完成首次序章流程，机关、出口和战斗仍须游戏内实测。
constexpr uint32_t completedFlags[] = {
    16010,16011,16013,16014,16015,16016,16017,16018,16019,
    16021,16022,16023,16024,16025,16026,16027,16028,16029,16030,
    16031,16032,16033,16034,16035,16036,16037,16038,16039,16040,
    16041,16042,16043,16045,16046,16047,16048,16049,16050,16051,
    16052,16053,16150,16151,16152
};
struct Destination { uint32_t id, region, place; const char* scene; };
constexpr Destination destinations[] = {
    {15,2,1101000,"mp1000"}, {97,6,1601000,"mp6010"},
    {98,6,1601001,"mp6010_01"}, {99,6,1601100,"mp6011"},
    {100,6,1601100,"mp6011"}, {101,6,1601200,"mp6012"},
    {102,6,1601200,"mp6012"}, {103,6,1601300,"mp6013"},
    {104,6,1601301,"mp6013_01"}, {105,6,1601302,"mp6013_01"},
    {120,9,1610001,"mp6100_01"}, {121,9,1610001,"mp6100_01"},
    {122,9,1610001,"mp6100_01"}, {151,9,1610001,"mp6100_01"}, {123,9,1610001,"mp6100_01"},
    {165,8,1850001,"mp8500_01"}, {166,8,1850002,"mp8500_02"},
    {136,1,1005401,"mp0054_01"}, {137,1,1005401,"mp0054_01"},
    {138,2,1107301,"mp1073_01"}, {139,2,1107301,"mp1073_01"},
    {140,3,1208401,"mp2084_01"}, {141,3,1208401,"mp2084_01"},
    {142,4,1304401,"mp3044_01"}, {143,4,1304401,"mp3044_01"},
    {146,3,1207200,"mp2072"}, {108,7,1530000,"mp5000"}
};
static_assert(std::size(destinations)<=32);

// SEH 只保护本模块额外的原始内存访问，不吞掉原生函数本身的异常。
bool ReadBytes(uintptr_t address, void* output, size_t size) noexcept {
    if (address < 0x10000) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(uintptr_t address, T& value) noexcept {
    return ReadBytes(address, &value, sizeof(value));
}
template<class T> bool Write(uintptr_t address, const T& value) noexcept {
    if (address < 0x10000) return false;
    __try { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<size_t N> bool Matches(uintptr_t address, const unsigned char (&bytes)[N]) noexcept {
    unsigned char actual[N]{};
    return ReadBytes(address, actual, N) && std::memcmp(actual, bytes, N) == 0;
}
bool ReadScene(uintptr_t address, char (&result)[32]) noexcept {
    // 表内字符串只保证以NUL结尾，不保证后面还有32字节可读。逐字节有界读取，
    // 防止合法短字符串位于页末时因读过终止符而误判场景丢失。
    std::memset(result,0,sizeof(result));
    size_t length=0;
    for (;length<21;++length) {
        if (!Read(address+length,result[length])) return false;
        if (!result[length]) break;
    }
    if (length==21) { result[21]=0;return false; }
    const size_t size = std::strlen(result);
    if (size < 6 || size > 20 || result[0] != 'm' || result[1] != 'p') return false;
    for (size_t i = 2; i < size; ++i)
        if ((result[i] < '0' || result[i] > '9') && result[i] != '_') return false;
    return true;
}

struct PlaceIdentity {
    uint32_t id=0,region=0;
    uint8_t variant=0;
    char scene[32]{};
};
bool ReadPlaceIdentity(uintptr_t address, PlaceIdentity& result) noexcept {
    uintptr_t name=0;
    return Read(address,result.id) && result.id && Read(address+8,name) && ReadScene(name,result.scene) &&
        Read(address+0x90,result.variant) && Read(address+0x98,result.region) && result.region<=9;
}
bool ReadPlaceRows(uintptr_t& rows,uint32_t& count) noexcept {
    uintptr_t owner=0,holder=0,file=0,buffer=0,headers=0;
    uint32_t index=0,offset=0,stride=0;
    if (!Read(base+0xC5D778,owner) || !Read(owner+0x60,holder) || !Read(holder+8,file) ||
        !Read(file+0x10,buffer) || !Read(file+0x20,headers) || !Read(file+0x2C,index) || index>1024) return false;
    const uintptr_t header=headers+static_cast<uintptr_t>(index)*0x50;
    if (!Read(header+0x44,offset) || offset>0x1000000 || !Read(header+0x48,stride) || stride!=0xA8 ||
        !Read(header+0x4C,count) || !count || count>4096) return false;
    rows=buffer+offset;return true;
}
bool MatchPlaceIdentity(const PlaceIdentity& a,const PlaceIdentity& b) noexcept {
    return a.id==b.id && a.region==b.region && a.variant==b.variant && std::strcmp(a.scene,b.scene)==0;
}
bool ValidateCurrentPlaces(const PlaceIdentity& root,const PlaceIdentity& current) noexcept {
    uintptr_t rows=0;uint32_t count=0;bool foundRoot=false,foundCurrent=false;
    if (!ReadPlaceRows(rows,count)) return false;
    for (uint32_t i=0;i<count;++i) {
        const auto row=rows+static_cast<uintptr_t>(i)*0xA8;
        uint32_t id=0;if (!Read(row,id)) return false;
        if (id!=root.id && id!=current.id) continue;
        PlaceIdentity actual{};if (!ReadPlaceIdentity(row,actual)) return false;
        foundRoot|=MatchPlaceIdentity(actual,root);foundCurrent|=MatchPlaceIdentity(actual,current);
    }
    return foundRoot && foundCurrent;
}
uint32_t ResolveSceneRegion(uint32_t chapter,const char* scene,uint32_t current,uint32_t root) noexcept {
    // 迷途之森只有1008100这一条region0记录。调用者仍须核对准确地点/变体，
    // 此处只完成展示归属转换；保护失效时不妨碍已在该场景的玩家安全返程离开。
    if (std::strcmp(scene,forest::kScene)==0)
        return (revisit_policy::kUnrestricted || chapter==8 || chapter==9) && current==0 && root==0 ? forest::kRegion : 0;
    if (const auto expected=revisit_eventguard::SceneRegion(scene)) {
        if constexpr (revisit_policy::kUnrestricted) {
            return revisit_eventguard::ResolveKnownSceneRegion(scene,current,root);
        } else {
        // 前期玩家可在正常剧情中进入序章/研究所/荣耀号。此处只核对其真实地理
        // 身份，不套用“晚期补箱”的完成门槛，也不会启用晚期特殊宝箱保护。
        if (chapter<8) return root==expected && (current==0 || current==expected)?expected:0;
        return revisit_eventguard::ResolveRevisitStoryRegion(0x40000000u|chapter,scene,current,root);
        }
    }
    // 普通地图的子地形可以region0，但不允许未知的非零地区冲突。调用者还必须
    // 证明默认/当前两条地形记录都与静态t_place的ID、scene、variant、region一致。
    return root>=1 && root<=9 && (current==0 || current==root)?root:0;
}
const Destination* FindDestination(uint32_t id) noexcept {
    for (const auto& item : destinations) if (item.id == id) return &item;
    return nullptr;
}
bool ReturnScenePhaseAllowed(const RevisitNativeContext& context,const char* scene,
                             uint32_t mapPlace,uint32_t region) noexcept {
    if (!ValidRevisitChapter(context.chapter) || !scene || !scene[0]) return false;
    if constexpr (revisit_policy::kUnrestricted) {
        return true;
    } else {
    // 晚期已验证的精确返程维持原有语义。本保护只约束新开放的早期剧情阶段。
    if (context.chapter>=8) return true;
    bool active=false;
    for (const auto blocked:context.beforeScriptBlocked) active|=blocked!=0;
    if (!active) return true;
    // 坐标返程没有Spot ID，不猜最近传送点。按真实scene+当前地形mapPlace
    // 收集全部可解析ID，包含变体和内部记录；任一活跃就拒绝。整座大地图可能
    // 共用同一个scene，因此仅按scene会把一处关所的剧情误用到整座城市。
    // 跨地区入口的row.region/place有时属于来源地图而非实际落地地形；这种
    // 别名无法用目标place消歧，同scene且跨地区的活跃分支仍保守拒绝整张图。
    bool mapped=false;
    for (const auto& row:kTravelCatalog) {
        if (!row.id || row.id>=context.beforeScriptBlocked.size() || !row.scene ||
            std::strcmp(row.scene,scene)) continue;
        const bool samePlace=row.place==mapPlace;
        mapped|=samePlace;
        if ((samePlace || row.region!=region) && context.beforeScriptBlocked[row.id]) return false;
    }
    // 道路可能有真实t_place地形，却没有独立Spot。没有匹配ID时以纯规则的
    // 目标0检查默认分支：仅对另一处==ID/正向范围生效的前置剧情不应禁用
    // 整条道路；不限目标及!=ID豁免类仍拒绝。0绝不成为实际换图目标。
    // 此函数仅审核剧情：当前地形已由Capture验证；磁盘返程记录仍必须在
    // 最终原生提交前通过ValidateReturnTable，不能借默认分支伪造地点身份。
    return mapped || !context.beforeScriptBlocked[revisit_phase::kUnmatchedDestination];
    }
}
bool SupportedSource(const RevisitNativeContext& context) noexcept {
    // 原生普通地图被剧情灰化不等于加载接口不可用；是否允许离开由稳定浏览、
    // 完成旗标、实际站位捕获及上层准入共同决定，不再只允许柏斯市出发。
    if (!context.valid || context.chapter>9 || context.region<1 || context.region>9) return false;
    // 实验策略只解除剧情阶段限制。context的实际scene、两种地形身份及地区
    // 已由Capture核对；稳定地图、换图繁忙和坐标捕获仍在后续入口单独检查。
    if constexpr (revisit_policy::kUnrestricted) {
        return true;
    } else {
    if (context.region==6 || context.region==8 || context.region==9) {
        // 包括正常出口可到达但没有独立传送点的研究所户外和荣耀号甲板；
        // 不把特殊地区里尚未审核的任意 scene 当作普通自由行动场景。
        if (revisit_eventguard::SceneRegion(context.scene)!=context.region) return false;
        // 早期真实所在场景可使用原生菜单规则；不要要求尚未发生的后续剧情完成。
        // 晚期回访仍须通过原有完成门槛，避免此放宽变成旧图剧情许可旁路。
        if (context.chapter<8) return true;
        const uint32_t representative=context.region==6?97u:context.region==8?165u:120u;
        for (size_t i=0;i<std::size(destinations);++i)
            if (destinations[i].id==representative) return (context.destinationMask&(1u<<i))!=0;
        return false;
    }
    return true;
    }
}
bool SameContext(const RevisitNativeContext& a, const RevisitNativeContext& b) noexcept {
    return a.valid && b.valid && a.chapter == b.chapter && a.region == b.region &&
        a.progressSignature == b.progressSignature && std::strcmp(a.scene, b.scene) == 0;
}

const TravelCatalogRow* CatalogDestination(uint32_t id,uint8_t variant) noexcept {
    const TravelCatalogRow* first=nullptr;
    for (const auto& row:kTravelCatalog) {
        if (row.id!=id) continue;
        if (!first) first=&row;
        if (row.variant==variant) return &row;
    }
    return first;
}
bool PublicEntryVariant(const TravelCatalogRow& canonical,const TravelCatalogRow& selected) noexcept {
    // 城市公开ID的南口/北口可能以◆命名，但它们不是独立调试目的地：原生会
    // 依据当前地形类型选择同一ID的入口。只承认公开默认行下完全相同的目的地
    // 家族，不把“存在一个同ID的普通名字”扩大成任意scene/place都可使用。
    return canonical.id && canonical.kind==0 && canonical.variant==0 && !(canonical.flags&8) &&
        selected.kind==1 && selected.variant!=canonical.variant && !(selected.flags&8) &&
        canonical.id==selected.id && canonical.region==selected.region && canonical.area==selected.area &&
        canonical.place==selected.place && canonical.scene && canonical.scene[0] && selected.scene &&
        std::strcmp(canonical.scene,selected.scene)==0;
}
bool OrdinaryCatalogDestination(const TravelCatalogRow& selected) noexcept {
    if ((selected.flags&8) || !selected.scene || !selected.scene[0]) return false;
    if (selected.kind==0) return true;
    if (selected.kind!=1) return false;
    const auto* canonical=CatalogDestination(selected.id,0);
    return canonical && PublicEntryVariant(*canonical,selected);
}
void CaptureRules(uintptr_t manager,RevisitNativeContext& context,uint8_t variant) noexcept {
    if (!context.valid || !ruleProof.valid || manager!=ruleProof.manager ||
        context.chapter!=ruleProof.chapter || context.region!=ruleProof.region ||
        context.progressSignature!=ruleProof.signature || std::strcmp(context.scene,ruleProof.scene)) return;
    uintptr_t spotData=0,areaData=0;uint64_t spotCount=0,areaCount=0;
    std::array<RuleSpot,1001> spots{};std::array<RuleArea,64> areas{};
    if (!Read(manager+0xE0,spotData) || !Read(manager+0xE8,spotCount) || !spotCount || spotCount>spots.size() ||
        !Read(manager+0xC8,areaData) || !Read(manager+0xD0,areaCount) || areaCount>areas.size() ||
        !ReadBytes(spotData,spots.data(),static_cast<size_t>(spotCount)*sizeof(RuleSpot)) ||
        (areaCount && !ReadBytes(areaData,areas.data(),static_cast<size_t>(areaCount)*sizeof(RuleArea)))) return;
    std::array<uint8_t,1001> states{};
    for (uint64_t i=0;i<areaCount;++i) {
        if (!areas[i].id || !areas[i].region || areas[i].region>9 || areas[i].blocked>1 || areas[i].visible>1) return;
        for (uint64_t j=0;j<i;++j) if (areas[j].id==areas[i].id) return;
    }
    for (uint64_t i=0;i<spotCount;++i) {
        const auto& spot=spots[i];
        if (!spot.id || spot.id>=states.size() || states[spot.id] || !spot.region || spot.region>9 ||
            spot.visible>1 || spot.blocked>1 || spot.registered>1) return;
        const auto* native=CatalogDestination(spot.id,variant);
        uint8_t state=RuleAllowed;
        if (!native || native->region!=spot.region || native->area!=spot.area) state=RuleInconsistent;
        // 公开城市ID的内部命名分岐仍属同一原生目标，由统一家族谓词核对。
        // 已审核的序章/研究所点没有展示地图而归类kind2，但在其原章节本来
        // 就属于正常菜单目标。只放行这些已核对的kind2记录，仍要求下面的
        // 原生registered/blocked和前置剧情；只有内部记录的ID不因此获得许可。
        else if ((!OrdinaryCatalogDestination(*native) && !(context.chapter<8 && native->kind==2 && FindDestination(spot.id))) ||
                 (native->flags&8)!=0 || !native->scene || !native->scene[0]) state=RuleInternal;
        else if (context.beforeScriptBlocked[spot.id]) state=RuleBeforeScript;
        else if (!spot.registered) state=RuleUnregistered;
        else if (spot.blocked) state=RuleBlocked;
        else if (spot.area) {
            bool found=false;
            for (uint64_t j=0;j<areaCount;++j) if (areas[j].id==spot.area) {
                found=true;
                if (areas[j].region!=spot.region) state=RuleInconsistent;
                else if (areas[j].blocked) state=RuleAreaBlocked;
                break;
            }
            if (!found) state=RuleInconsistent;
        }
        states[spot.id]=state;
    }
    // 复制后复核原生容器身份；不持有、修改或重用其地址，不把上次存档的数组当规则。
    uintptr_t again=0;uint64_t countAgain=0;
    if (!Read(manager+0xE0,again) || again!=spotData || !Read(manager+0xE8,countAgain) || countAgain!=spotCount ||
        !Read(manager+0xC8,again) || again!=areaData || !Read(manager+0xD0,countAgain) || countAgain!=areaCount) return;
    context.nativeRuleStatus=states;
}

// 与已有地图刷新共用相同浏览栈约束；确认/过场/退出/世界地图不允许消费新请求。
bool StableBrowse(uintptr_t menu, uintptr_t minimap, int32_t& depth, int32_t& current) noexcept {
    uintptr_t reverse = 0, owner = 0, self = 0, leave = 0;
    int32_t updateDepth = 0, kind = -1, frames[12]{};
    uint8_t active = 0, eventTravel = 1, worldMenu = 1, worldManager = 1;
    if (!menu || !Read(menu+8, owner) || owner != minimap ||
        !Read(minimap+0x28, reverse) || reverse != menu ||
        !Read(menu+0x18, self) || self != menu || !Read(menu+0xF0, leave) || leave ||
        !Read(menu+0xE8, depth) || depth < 0 || depth > 2 ||
        !Read(menu+0xEC, updateDepth) || updateDepth != depth ||
        !ReadBytes(menu+0xB8, frames, sizeof(frames)) ||
        !Read(menu+0x279, active) || active != 1 || !Read(menu+0x27A, eventTravel) || eventTravel ||
        !Read(menu+0x27B, worldMenu) || worldMenu || !Read(menu+0x3C8, kind) || kind ||
        !Read(minimap+0x30A, worldManager) || worldManager) return false;
    current = frames[depth*3];
    return frames[0] == 3 && frames[1] == 3 && current >= 3 && current <= 5 &&
        frames[depth*3+1] == current && frames[depth*3+2] > 0 &&
        (current != 3 || depth == 0) && (current != 4 || depth == 1) &&
        (current != 5 || depth >= 1) && (depth != 2 || (frames[3] == 4 && frames[4] == 4));
}

RevisitNativeContext Capture(uintptr_t expectedMinimap = 0) noexcept {
    RevisitNativeContext result{};
    result.available = available.load(std::memory_order_relaxed);
    captureIssue=CaptureIssue::None;
    diagnosticMenuState=-1;
    const auto fail=[&](CaptureIssue issue) { captureIssue=issue;return result; };
    uintptr_t field = 0, scene = 0, root = 0, savedata = 0, minimap = 0, menu = 0;
    uint32_t chapter = 0, pending = 0;
    uint8_t specialEvent = 0,rootValid=0;
    std::array<uint8_t, 4096> storyFlags{};
    PlaceIdentity rootIdentity{},currentIdentity{};
    if (!base || !Read(base+0xC60E08,field) || !Read(field+0x1BC8,pending)) return fail(CaptureIssue::Field);
    result.transitionActive=pending!=0;
    if (!ReadScene(field+0x170,result.scene) || !Read(field+0x648,scene) ||
        !Read(field+0x108,root) || !Read(root+0xE77,rootValid) || rootValid!=1 ||
        !ReadPlaceIdentity(root+0x808,rootIdentity) || !ReadPlaceIdentity(scene,currentIdentity) ||
        std::strcmp(rootIdentity.scene,result.scene) || std::strcmp(currentIdentity.scene,result.scene)) return fail(CaptureIssue::Scene);
    result.place=rootIdentity.id;result.mapPlace=currentIdentity.id;
    result.sceneRegion=rootIdentity.region;result.nativeRegion=currentIdentity.region;
    if (!ValidateCurrentPlaces(rootIdentity,currentIdentity)) return fail(CaptureIssue::PlaceTable);
    if (!std::strcmp(result.scene,forest::kScene) &&
        (!forest::MatchesPlace(rootIdentity.id,rootIdentity.region,rootIdentity.variant,rootIdentity.scene) ||
         !forest::MatchesPlace(currentIdentity.id,currentIdentity.region,currentIdentity.variant,currentIdentity.scene)))
        return fail(CaptureIssue::PlaceTable);
    if (!Read(base+0xC60E58, savedata) || !Read(savedata+0x10B, specialEvent) ||
        !Read(savedata+0x11100+12*4, chapter) ||
        (chapter >> 30) != 1 || !ReadBytes(savedata+0x100, storyFlags.data(), storyFlags.size())) return fail(CaptureIssue::Story);
    result.chapter = chapter & 0x3FFFFFFFu;
    // 每次普通场景捕获也退休其他章节的实验行程，不能依赖回访窗口或特殊箱
    // 恰好触发回调。这里只读取/清理既有令牌，读回旧章节绝不自动恢复保护。
    if constexpr (revisit_policy::kUnrestricted)
        if (ValidRevisitChapter(result.chapter)) (void)ExperimentalRevisitTripActive(result.chapter);
    result.forestStoryComplete=forest::StoryComplete(result.chapter,storyFlags.data(),storyFlags.size());
    result.region=ResolveSceneRegion(result.chapter,result.scene,currentIdentity.region,rootIdentity.region);
    if (!result.region) return fail(CaptureIssue::Region);
    // 29CF90在旗标93为真时使用事件专用换图参数；首版只接受普通自由行动分支。
    result.busy = pending != 0 || (specialEvent & 0x20) != 0;
    if (!Read(field+0x730,minimap) || (expectedMinimap && minimap!=expectedMinimap) ||
        !Read(minimap+0x28,menu)) return fail(CaptureIssue::Minimap);
    result.prologueCompleted = true;
    for (uint32_t flag : completedFlags) {
        if (!(storyFlags[flag/8] & (1u << (flag%8)))) result.prologueCompleted = false;
    }
    constexpr uint16_t towerFlags[]={23070};
    constexpr uint16_t jadeFlags[]={23070,23014,23015};
    constexpr uint16_t azureFlags[]={23070,23043,23045,23047};
    constexpr uint16_t schoolFlags[]={17052,17053,17054,17055,17056,17151};
    constexpr uint16_t cityhallFlags[]={25022,25023,25051,25084};
    for (uint32_t id=0;id<result.beforeScriptBlocked.size();++id)
        result.beforeScriptBlocked[id]=revisit_phase::BeforeScriptBlocked(
            result.chapter,id,storyFlags.data(),storyFlags.size())?1u:0u;
    result.beforeScriptReturnBlocked=!ReturnScenePhaseAllowed(
        result,result.scene,result.mapPlace,result.region);
    for (size_t i=0;i<std::size(destinations);++i) {
        const auto& item=destinations[i];bool permitted=false;
        using namespace revisit_eventguard;
        if (result.chapter==8 || result.chapter==9) {
            if (item.id==15) permitted=true;
            else if (item.id==136 || item.id==137) permitted=AllFlags(storyFlags.data(),storyFlags.size(),jadeFlags);
            else if (item.id==140 || item.id==141) permitted=AllFlags(storyFlags.data(),storyFlags.size(),azureFlags);
            else if (item.id>=138 && item.id<=143) permitted=AllFlags(storyFlags.data(),storyFlags.size(),towerFlags);
            else if (item.id==146) permitted=AllFlags(storyFlags.data(),storyFlags.size(),schoolFlags);
            else if (item.id==108) permitted=result.chapter==9 && AllFlags(storyFlags.data(),storyFlags.size(),cityhallFlags);
            else permitted=RequiredRevisitStoryComplete(chapter,item.region,item.scene,storyFlags.data(),storyFlags.size());
        }
        if (permitted && result.prologueCompleted) result.destinationMask|=1u<<i;
    }
    // 只摘要剧情区间；宝箱/真实到访等可自然改变的记录不作为请求身份。
    result.progressSignature = 14695981039346656037ull;
    for (size_t i=16000/8;i<16000/8+1200;++i) {
        const uint8_t value=storyFlags[i];
        result.progressSignature ^= value;
        result.progressSignature *= 1099511628211ull;
    }
    // 第一批剧情范围之外的1073会触发第3章统一传送接管。只加入这一位，
    // 不把整片普通到访/宝箱记录纳入身份，避免无关收集变化取消有效请求。
    result.progressSignature^=(storyFlags[1073/8]>>(1073%8))&1u;
    result.progressSignature*=1099511628211ull;
    int32_t depth = 0, current = 0;
    result.browsing = !result.busy && StableBrowse(menu, minimap, depth, current);
    if (menu && Read(menu+0xE8,depth) && depth>=0 && depth<=2)
        Read(menu+0xB8+static_cast<uintptr_t>(depth)*12,diagnosticMenuState);
    if (menu) {
        // 可逆整数混合用于隐藏原始地址；仅比较本次菜单身份，不由此值重建/访问地址。
        uint64_t identity=static_cast<uint64_t>(menu)^0x58D93E2BC715A06Full;
        identity=(identity^(identity>>30))*0xBF58476D1CE4E5B9ull;
        identity=(identity^(identity>>27))*0x94D049BB133111EBull;
        result.browseIdentity=identity^(identity>>31);
    }
    uintptr_t fieldAgain = 0, savedataAgain = 0, sceneAgain = 0;
    uint32_t chapterAgain = 0;
    if (!Read(base+0xC60E08, fieldAgain) || fieldAgain != field ||
        !Read(base+0xC60E58, savedataAgain) || savedataAgain != savedata ||
        !Read(field+0x648, sceneAgain) || sceneAgain != scene ||
        !Read(savedata+0x11100+12*4, chapterAgain) || chapterAgain != chapter) return fail(CaptureIssue::Changed);
    result.valid = result.region >= 1 && result.region <= 9 && result.chapter <= 9;
    CaptureRules(minimap,result,currentIdentity.variant);
    return result;
}

// t_place 的运行时布局与当前/默认 MapData 同源。明确核对两个地点 ID，
// 不因场景字符串看似合法就接受磁盘记录中的任意 scene 或 variant。
bool ValidateReturnTable(const RevisitReturnPoint& point) noexcept {
    if (!ValidRevisitReturnPoint(point) || point.yawRadians<0 || point.yawRadians>=6.2831854820251464844f) return false;
    uintptr_t rows=0;uint32_t count=0,rootRegions=0,currentRegions=0;
    // 记录中的variant只来自当前地形。大地图默认根地点可能使用另一种类型，
    // 例如卢安根1200000为1、阿伊纳街道1208000为2；不能要求两者相等。
    // 根身份改为按已保存的ID+scene独立核对，每个原始地区仍须只有一种类型。
    // -1表示未找到，不把合法variant0当作缺失；同地区不同类型视为歧义。
    std::array<int16_t,10> rootVariants{};rootVariants.fill(-1);
    std::array<bool,10> ambiguousRoot{};
    if (!ReadPlaceRows(rows,count)) return false;
    for (uint32_t i=0;i<count;++i) {
        const uintptr_t row=rows+static_cast<uintptr_t>(i)*0xA8;
        uint32_t id=0;
        if (!Read(row,id)) return false;
        if (id!=point.place && id!=point.mapPlace) continue;
        PlaceIdentity identity{};if (!ReadPlaceIdentity(row,identity)) return false;
        if (std::strcmp(identity.scene,point.scene)) continue;
        if (id==point.place) {
            rootRegions|=1u<<identity.region;
            auto& expected=rootVariants[identity.region];
            if (expected<0) expected=identity.variant;
            else if (expected!=identity.variant) ambiguousRoot[identity.region]=true;
        }
        // 当前地形必须继续精确匹配原来保存的类型，不能把根地点的独立核对
        // 扩大成对所有variant不加区分。坐标加载之后的到达确认也保留该字段。
        if (id==point.mapPlace && identity.variant==point.variant) currentRegions|=1u<<identity.region;
    }
    // 此唯一region0地图必须严格命中相同地点和变体；不能把“任一表中有region0”
    // 当作通用放宽。这里只核对身份，进入该场景的请求另要求剧情和保护均就绪。
    if (!std::strcmp(point.scene,forest::kScene))
        return point.place==forest::kPlace && point.mapPlace==forest::kPlace &&
            point.variant==forest::kVariant && point.region==forest::kRegion &&
            rootRegions==1u && currentRegions==1u && !ambiguousRoot[0] && rootVariants[0]==forest::kVariant;
    // 默认地点和子地形可以各有类型/地区，但两者必须来自同scene的真实表记录，
    // 并满足明确地区归一规则。荣耀号7/8属于已经审核的跨地区别名，不因它们
    // 是两个不同地区而误认歧义；同一候选地区内类型冲突仍必须拒绝。
    bool resolved=false;
    for (uint32_t root=1;root<=9;++root) if (rootRegions&(1u<<root))
        for (uint32_t current=0;current<=9;++current) if (currentRegions&(1u<<current))
            if (ResolveSceneRegion(point.chapter,point.scene,current,root)==point.region) {
                if (ambiguousRoot[root]) return false;
                resolved=true;
            }
    return resolved;
}

bool ForestEntryAllowed(const RevisitNativeContext& context) noexcept {
    if constexpr (revisit_policy::kUnrestricted) {
        return ValidRevisitChapter(context.chapter) && nativeLoad && ForestRevisitGuardReady();
    } else {
        return (context.chapter==8 || context.chapter==9) && context.forestStoryComplete &&
            nativeLoad && ForestRevisitGuardReady();
    }
}
bool SafeReturnDestination(const RevisitReturnPoint& point,const RevisitNativeContext& context) noexcept {
    // 从迷途之森离开不依赖可选保护模块；但磁盘返程记录若指向迷途之森，也不能
    // 绕过首次进入的门槛。记录只承载位置，不允许它充当剧情许可。
    return nativeLoad && RevisitNativeReturnPhaseAllowed(point,context) && ValidateReturnTable(point) &&
        (std::strcmp(point.scene,forest::kScene)!=0 || ForestEntryAllowed(context));
}
bool ValidateForestDestination(const RevisitNativeContext& context,RevisitReturnPoint* arrival=nullptr) noexcept {
    if (!ForestEntryAllowed(context)) return false;
    RevisitReturnPoint point{};
    std::memcpy(point.scene,forest::kScene,sizeof(forest::kScene));
    point.chapter=context.chapter;point.region=forest::kRegion;
    point.place=point.mapPlace=forest::kPlace;point.variant=forest::kVariant;
    point.progressSignature=context.progressSignature;
    // 原生EV_04_32_01_END把自由行动角色放在(0,0,0)、朝向0度后EVENT_END。
    // 不调用这段旧事件，直接复用完整普通加载入口；谜题保护必须提前就绪。
    if (!ValidateReturnTable(point)) return false;
    if (arrival) *arrival=point;
    return true;
}

bool QuaternionYaw(const float (&q)[4], float& yaw) noexcept {
    float norm=0;
    for (float component:q) { if (!std::isfinite(component)) return false; norm+=component*component; }
    if (!std::isfinite(norm) || std::fabs(norm-1.0f)>0.01f) return false;
    // 原生保存函数 4392AB 使用 61E10 旋转单位前向 (0,0,1)，随后 atan2(X,Z)。
    // 四元数布局是 x,y,z,w；这里复现该乘法，不把 y 当欧拉角，也不调用游戏函数。
    const float forwardX=2.0f*(q[0]*q[2]+q[1]*q[3]);
    const float forwardZ=q[3]*q[3]+q[2]*q[2]-q[0]*q[0]-q[1]*q[1];
    if (forwardX*forwardX+forwardZ*forwardZ<0.0001f) return false;
    constexpr float tau=6.2831854820251464844f;
    yaw=std::atan2(forwardX,forwardZ);
    if (yaw<0) yaw+=tau;
    if (yaw>=tau) yaw-=tau;
    return std::isfinite(yaw);
}

bool CapturePoint(const RevisitNativeContext& context, RevisitReturnPoint& output) noexcept {
    output={};
    pointIssue=1;
    if (!context.valid || context.busy) return false;
    uintptr_t field=0,mapData=0,root=0,player=0,actor=0,vtable=0;
    uint8_t rootValid=0,variant=0; uint32_t pending=1;
    RevisitReturnPoint point{}; float quaternion[4]{};
    pointIssue=2;
    if (!Read(base+0xC60E08,field) || !Read(field+0x648,mapData) || !Read(field+0x108,root) ||
        !Read(root+0xE77,rootValid) || rootValid!=1 || !Read(root+0x808,point.place) ||
        !Read(mapData,point.mapPlace) || !Read(mapData+0x90,variant)) return false;
    pointIssue=3;
    if (!Read(field+0x660,player) || !Read(player,vtable) || vtable!=base+0xB05CB8 ||
        !Read(player+0x60,actor) || !ReadBytes(actor+0xE8,point.xyz,sizeof(point.xyz)) ||
        !ReadBytes(actor+0x108,quaternion,sizeof(quaternion))) return false;
    pointIssue=4;if (!QuaternionYaw(quaternion,point.yawRadians)) return false;
    point.variant=variant;point.chapter=context.chapter;point.region=context.region;
    point.progressSignature=context.progressSignature;
    std::memcpy(point.scene,context.scene,sizeof(point.scene));
    // 角色/场景关系在同一游戏线程读取；末尾仍复核一次，拒绝换档或对象重建交界。
    uintptr_t fieldAgain=0,actorAgain=0,sceneAgain=0;
    char sceneName[32]{};
    pointIssue=5;
    if (!Read(base+0xC60E08,fieldAgain) || fieldAgain!=field || !Read(field+0x648,sceneAgain) || sceneAgain!=mapData ||
        !Read(player+0x60,actorAgain) || actorAgain!=actor || !Read(field+0x1BC8,pending) || pending ||
        !ReadScene(field+0x170,sceneName) || std::strcmp(sceneName,point.scene)) return false;
    pointIssue=6;if (!ValidateReturnTable(point)) return false;
    output=point;pointIssue=0;return true;
}

bool SamePosition(const RevisitReturnPoint& actual, const RevisitReturnPoint& goal) noexcept {
    if (actual.chapter!=goal.chapter || actual.region!=goal.region || actual.place!=goal.place ||
        actual.mapPlace!=goal.mapPlace || actual.variant!=goal.variant || std::strcmp(actual.scene,goal.scene)) return false;
    // 容差只确认原生落地造成的微小浮点差；不拿地图中心或最近传送点冒充原位置。
    float distance=0;
    for (unsigned i=0;i<3;++i) { const float d=actual.xyz[i]-goal.xyz[i]; distance+=d*d; }
    constexpr float tau=6.2831854820251464844f;
    float angle=std::fabs(actual.yawRadians-goal.yawRadians);
    if (angle>tau/2) angle=tau-angle;
    return distance<=0.25f && angle<=0.04f;
}
bool SamePoseValues(const float* a,float aYaw,const float* b,float bYaw,float tolerance) noexcept {
    float distance=0;
    for (unsigned i=0;i<3;++i) { const float difference=a[i]-b[i];distance+=difference*difference; }
    constexpr float tau=6.2831854820251464844f;
    float angle=std::fmod(std::fabs(aYaw-bYaw),tau);
    if (angle>tau/2) angle=tau-angle;
    return distance<=tolerance*tolerance && angle<=0.04f;
}
LoadDescriptor ReadLoadDescriptor() noexcept {
    LoadDescriptor result{};uintptr_t field=0;
    if (!Read(base+0xC60E08,field) || !ReadScene(field+0x194,result.scene) ||
        !ReadBytes(field+0x1BA8,result.xyz,sizeof(result.xyz)) || !Read(field+0x1BB8,result.yaw) ||
        !std::isfinite(result.yaw) || std::fabs(result.yaw)>6.284f) return result;
    for (float value:result.xyz) if (!std::isfinite(value) || std::fabs(value)>=100000) return result;
    result.valid=true;return result;
}
bool ObservedSameSceneLoad(const RevisitNativeContext& context,const RevisitReturnPoint& point,bool pointValid) noexcept {
    if (!context.valid || context.busy || context.chapter!=expectedContext.chapter ||
        std::strcmp(context.scene,expectedContext.scene) || std::strcmp(context.scene,arrivalPoint.scene)) return false;
    const auto current=ReadLoadDescriptor();
    const bool requested=current.valid && !std::strcmp(current.scene,arrivalPoint.scene) &&
        SamePoseValues(current.xyz,current.yaw,arrivalPoint.xyz,arrivalPoint.yawRadians,0.01f);
    if (requested && (!beforeLoadDescriptor.valid || std::strcmp(current.scene,beforeLoadDescriptor.scene) ||
        !SamePoseValues(current.xyz,current.yaw,beforeLoadDescriptor.xyz,beforeLoadDescriptor.yaw,0.001f))) return true;
    // 原生待加载描述可能仍等于上次同一点的目标；此时要求实际位置从不同站位
    // 到达本目标，而不能仅凭scene相同认定换图。无法捕获姿态时此分支不会放宽。
    return pointValid && beforeLoadPositionValid && requested &&
        !SamePoseValues(beforeLoadPosition.xyz,beforeLoadPosition.yawRadians,arrivalPoint.xyz,arrivalPoint.yawRadians,0.5f) &&
        SamePoseValues(point.xyz,point.yawRadians,arrivalPoint.xyz,arrivalPoint.yawRadians,0.5f);
}

// 某些原生Spot的place只用于菜单归属或代表入口内侧，真实加载却只读取scene
// 和XYZ。实验模式允许此类已核对的原生行时，不能把菜单place强塞成落地地形。
// 用目标scene中submap为空的真实默认根确认地区；重复行须同ID/类型，地区别名
// 仅按既有纯地理规则归一。缺根、根身份冲突或地区矛盾仍拒绝，不猜地图中心。
bool ResolveDestinationRoot(uint32_t chapter,const char* scene,uintptr_t rows,uint32_t count,
                            uint32_t& resolved) noexcept {
    uint32_t rootId=0;int rootVariant=-1;resolved=0;
    for(uint32_t i=0;i<count;++i) {
        const auto row=rows+static_cast<uintptr_t>(i)*0xA8;
        uint32_t id=0;if (!Read(row,id)) return false;
        if (!id) continue;
        uintptr_t name=0,submap=0;char prefix[2]{},nameText[32]{},submapFirst=1;
        if (!Read(row+8,name) || !ReadBytes(name,prefix,sizeof(prefix))) return false;
        // t_place还包含a开头的非地图资源；不把它们误当格式损坏或目标地图根。
        if (prefix[0]!='m' || prefix[1]!='p') continue;
        if (!ReadScene(name,nameText)) return false;
        if (std::strcmp(nameText,scene)) continue;
        if (!Read(row+0x10,submap) || !Read(submap,submapFirst)) return false;
        if (submapFirst) continue;
        PlaceIdentity identity{};if (!ReadPlaceIdentity(row,identity)) return false;
        const auto region=ResolveSceneRegion(chapter,scene,identity.region,identity.region);
        if (!region || (resolved && resolved!=region) || (rootId && rootId!=identity.id) ||
            (rootVariant>=0 && rootVariant!=identity.variant)) return false;
        resolved=region;rootId=identity.id;rootVariant=identity.variant;
    }
    return resolved!=0;
}

// 模仿 295220 的查找顺序：收集同 ID，优先当前场景 variant，否则选首条。
// 这里不调用会分配游戏内存的查找器，只核对最终会被原生消费者采用的记录。
bool ValidateDestination(uint32_t target, RevisitReturnPoint* arrival = nullptr) noexcept {
    uintptr_t owner=0, holder=0, file=0, buffer=0, headers=0, field=0, scene=0;
    uint32_t index=0, offset=0, stride=0, count=0;
    uint8_t variant=0;
    if (!Read(base+0xC5D778, owner) || !Read(owner+0xF0, holder) || !Read(holder+8, file) ||
        !Read(file+0x10, buffer) || !Read(file+0x20, headers) || !Read(file+0x2C, index) || index>1024 ||
        !Read(base+0xC60E08, field) || !Read(field+0x648, scene) || !Read(scene+0x90, variant)) return false;
    const uintptr_t header=headers+static_cast<uintptr_t>(index)*0x50;
    if (!Read(header+0x44, offset) || offset>0x1000000 || !Read(header+0x48, stride) || stride!=0x98 ||
        !Read(header+0x4C, count) || !count || count>1024) return false;
    uintptr_t selected=0;
    for (uint32_t i=0; i<count; ++i) {
        const uintptr_t row=buffer+offset+static_cast<uintptr_t>(i)*stride;
        uint32_t id=0; uint8_t rowVariant=0;
        if (!Read(row,id)) return false;
        if (id!=target) continue;
        if (!selected) selected=row;
        if (!Read(row+0x60,rowVariant)) return false;
        if (rowVariant==variant) { selected=row; break; }
    }
    // 新目录保留所有变体；与原生同样“同ID优先variant，否则首条”，不能用UI去重行。
    const auto* catalog=CatalogDestination(target,variant);
    const auto* fallback=FindDestination(target);
    if (!catalog && (!fallback || SKY2_HAS_FULL_TRAVEL_CATALOG)) return false;
    const uint32_t expectedRegion=catalog?catalog->region:fallback->region;
    const uint32_t expectedPlace=catalog?catalog->place:fallback->place;
    const char* expectedScene=catalog?catalog->scene:fallback->scene;
    uintptr_t destinationName=0;
    uint32_t region=0, place=0;
    uint32_t area=0;uint8_t selectedVariant=0,flags=0;
    float position[4]{};
    char name[32]{};
    bool valid=selected && Read(selected+4,region) && region==expectedRegion &&
        Read(selected+0x40,place) && place==expectedPlace &&
        Read(selected+0x38,destinationName) && ReadScene(destinationName,name) &&
        std::strcmp(name,expectedScene)==0 && ReadBytes(selected+0x44,position,sizeof(position)) &&
        std::isfinite(position[0]) && std::isfinite(position[1]) &&
        std::isfinite(position[2]) && std::isfinite(position[3]) &&
        std::fabs(position[0])<100000 && std::fabs(position[1])<100000 &&
        std::fabs(position[2])<100000 && std::fabs(position[3])<=360;
    if (valid && catalog) valid=Read(selected+8,area) && area==catalog->area &&
        Read(selected+0x58,flags) && flags==catalog->flags &&
        Read(selected+0x60,selectedVariant) && selectedVariant==catalog->variant;
    if (valid && arrival) {
        *arrival={};arrival->mapPlace=place;
        // 普通跨地区入口126–135的row.region是菜单显示归属，落地却在另一地区。
        // 到达归属取真实地点表，并应用同一严格scene地区规则，不沿用显示分组号。
        uintptr_t savedata=0,rows=0;uint32_t chapter=0,placeCount=0,resolved=0;
        if (!Read(base+0xC60E58,savedata) || !Read(savedata+0x11100+12*4,chapter) ||
            (chapter>>30)!=1 || !ReadPlaceRows(rows,placeCount)) return false;
        for (uint32_t i=0;i<placeCount;++i) {
            uint32_t id=0;const auto row=rows+static_cast<uintptr_t>(i)*0xA8;
            if (!Read(row,id)) return false;
            if (id!=place) continue;
            PlaceIdentity identity{};if (!ReadPlaceIdentity(row,identity)) return false;
            if (std::strcmp(identity.scene,name) || !identity.region) continue;
            const auto candidate=ResolveSceneRegion(chapter&0x3FFFFFFFu,name,identity.region,identity.region);
            if (!candidate || (resolved && resolved!=candidate)) return false;
            resolved=candidate;
        }
        if (!resolved) {
            if constexpr (revisit_policy::kUnrestricted) {
                if (!ResolveDestinationRoot(chapter&0x3FFFFFFFu,name,rows,placeCount,resolved)) return false;
            } else return false;
        }
        arrival->region=resolved;
        std::memcpy(arrival->scene,name,sizeof(name));
        std::memcpy(arrival->xyz,position,sizeof(arrival->xyz));
        arrival->yawRadians=position[3]*0.01745329238474369f;
        constexpr float tau=6.2831854820251464844f;
        if (arrival->yawRadians<0) arrival->yawRadians+=tau;
        if (arrival->yawRadians>=tau) arrival->yawRadians-=tau;
    }
    return valid;
}

void SetPhase(uint64_t token, RevisitNativePhase phase) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (status.token==token) status.phase=phase;
}
void Publish(const RevisitNativeContext& context, const RevisitReturnPoint& point, bool pointValid) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex);
    published=context;
    publishedPoint=point;publishedPointValid=pointValid;
    publishedAt=GetTickCount64();
    if (status.phase==RevisitNativePhase::Queued || status.phase==RevisitNativePhase::ClosingMap) {
        if (GetTickCount64()-requestedAt > 5000) status.phase=RevisitNativePhase::Expired;
        else if (!SameContext(context,expectedContext) ||
                 (status.phase==RevisitNativePhase::Queued &&
                  (!context.browsing || context.browseIdentity!=expectedContext.browseIdentity)))
            status.phase=RevisitNativePhase::Rejected;
    }
    if (status.phase==RevisitNativePhase::Dispatched) {
        if (context.transitionActive || (context.valid && std::strcmp(context.scene,expectedContext.scene))) sawLoadTransition=true;
        if (!sawLoadTransition && arrivalPointValid && ObservedSameSceneLoad(context,point,pointValid)) sawLoadTransition=true;
        bool arrived=false;
        if (arrivalPointValid && context.valid && sawLoadTransition && !context.busy &&
            context.chapter==expectedContext.chapter) {
            if (status.target==kRevisitReturnTarget || status.target==forest::kTarget)
                arrived=pointValid && SamePosition(point,arrivalPoint);
            else {
                // 普通原生点可能位于两块地形边界，具体 place 由原生落地射线确定。
                // 出发只要求实际场景/地区与原生点一致；返程才要求保存的全部地点字段。
                arrived=context.region==arrivalPoint.region && std::strcmp(context.scene,arrivalPoint.scene)==0;
            }
        }
        arrivalFrames=arrived?arrivalFrames+1:0;
        if (arrivalFrames>=3) status.phase=RevisitNativePhase::Arrived;
        else if (GetTickCount64()-dispatchedAt>60000) status.phase=RevisitNativePhase::ArrivalUnconfirmed;
    }
}
void LogNativeState(const RevisitNativeContext& context,bool pointValid) noexcept {
    const auto snapshot=ReadRevisitNativeStatus();
    uint64_t key=14695981039346656037ull;
    const uint32_t fields[]={static_cast<uint32_t>(captureIssue),pointIssue,context.region,context.sceneRegion,
        context.nativeRegion,context.place,context.mapPlace,static_cast<uint32_t>(snapshot.phase),snapshot.target,
        context.valid?1u:0u,context.busy?1u:0u,context.browsing?1u:0u,pointValid?1u:0u};
    for (uint32_t value:fields) { key^=value;key*=1099511628211ull; }
    for (char c:context.scene) { key^=static_cast<unsigned char>(c);key*=1099511628211ull;if (!c) break; }
    const auto now=GetTickCount64();
    if (key==diagnosticKey || diagnosticCount>=128 || (diagnosticCount && now-diagnosticAt<1000)) return;
    diagnosticKey=key;diagnosticAt=now;++diagnosticCount;
    char line[384]{};
    std::snprintf(line,sizeof(line),
        "Revisit state: scene=%s chapter=%u region=%u root=%u/%u terrain=%u/%u menu=%d valid=%u busy=%u browse=%u pose=%u captureIssue=%u poseIssue=%u phase=%u target=%u token=%llu.",
        context.scene[0]?context.scene:"(none)",context.chapter,context.region,context.place,context.sceneRegion,
        context.mapPlace,context.nativeRegion,diagnosticMenuState,context.valid?1u:0u,context.busy?1u:0u,
        context.browsing?1u:0u,pointValid?1u:0u,static_cast<uint32_t>(captureIssue),pointIssue,
        static_cast<uint32_t>(snapshot.phase),snapshot.target,static_cast<unsigned long long>(snapshot.token));
    Log(line);
}
void ClearCancelledHandoff(uintptr_t minimap) noexcept {
    RevisitNativeStatus pending{};
    uintptr_t menuIdentity=0, minimapIdentity=0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!dispatchArmed || status.phase==RevisitNativePhase::ClosingMap) return;
        pending=status;
        menuIdentity=handoffMenuIdentity; minimapIdentity=handoffMinimapIdentity;
    }
    uintptr_t menu=0;
    uint64_t result=0;
    const uint64_t ours=static_cast<uint64_t>(pending.target)|(1ull<<32);
    const uint64_t cancelled=0;
    bool cleared=false;
    if (minimap!=minimapIdentity) cleared=true;
    else if (Read(minimap+0x28,menu)) {
        if (menu && menu!=menuIdentity) {
            // 新菜单不属于本请求，绝不按相同target数值去清除它的普通传送结果。
            cleared=true;
        }
        else if (menu) {
            int32_t depth=0,current=0,next=0;
            if (Read(menu+0x3C0,result) && result!=ours) {
                uint64_t copied=0;
                cleared=Read(minimap+0x310,copied) &&
                    (copied!=ours || Write(minimap+0x310,cancelled));
            }
            else if (result==ours && Read(menu+0xE8,depth) && depth>=0 && depth<=2 &&
                Read(menu+0xB8+static_cast<uintptr_t>(depth)*12,current) &&
                Read(menu+0xBC+static_cast<uintptr_t>(depth)*12,next) &&
                (current==17 || current==18 || next==17 || next==18)) {
                // 只撤回自己已提交且正在关闭的结果；保留原生退出动画继续收尾。
                cleared=Write(menu+0x3C0,cancelled);
                uint64_t copied=0;
                if (Read(minimap+0x310,copied) && copied==ours)
                    cleared=Write(minimap+0x310,cancelled) && cleared;
            }
        } else if (Read(minimap+0x310,result)) {
            cleared=result!=ours || Write(minimap+0x310,cancelled);
        }
    }
    if (cleared) {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (status.token==pending.token) dispatchArmed=false;
    }
}

// 仅观察来自原生菜单消费者的请求。普通传送和脚本直接调用不受本模块影响。
// 失败时清除本次临时结果，避免原生消费者下一帧不断重试已经失效的回访请求。
bool BeforeJump(uintptr_t field, uint32_t target, uintptr_t caller) noexcept {
    RevisitNativeStatus pending{};
    RevisitNativeContext expected{};
    RevisitReturnPoint returnPoint{},goal{};
    ULONGLONG startedAt=0;
    bool armed=false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        pending=status; expected=expectedContext; startedAt=requestedAt; armed=dispatchArmed;
        returnPoint=requestedReturn;
    }
    const bool consumer=caller==base+0x299674 || caller==base+0x29968F;
    if (!consumer || !armed || pending.target!=target) return false;
    const auto context=Capture();
    RevisitReturnPoint dispatchPosition{};
    const bool dispatchPositionValid=CapturePoint(context,dispatchPosition);
    const auto loadDescriptor=ReadLoadDescriptor();
    uintptr_t realField=0, minimap=0, menu=1;
    uint32_t resultId=0, resultKind=0;
    const auto permission=authorize.load();
    const bool returning=target==kRevisitReturnTarget;
    const bool forestEntry=target==forest::kTarget;
    const bool destinationValid=returning ?
        SafeReturnDestination(returnPoint,context) : forestEntry ?
        ValidateForestDestination(context,&goal) : ValidateDestination(target,&goal);
    if (returning) goal=returnPoint;
    bool safe=pending.phase==RevisitNativePhase::ClosingMap && GetTickCount64()-startedAt<=5000 &&
        context.available && !context.busy &&
        SameContext(context,expected) && RevisitNativeTargetAvailable(target,context) && destinationValid &&
        Read(base+0xC60E08,realField) && realField==field && Read(field+0x730,minimap) &&
        Read(minimap+0x28,menu) && !menu && Read(minimap+0x310,resultId) && resultId==target &&
        Read(minimap+0x314,resultKind) && resultKind==1 && permission && permission(target,context,pending.token);
    if (safe) {
        // 最后准入回调之外仍可能发生Cancel。以此锁内比较作为传送的唯一提交点：
        // 提交之前取消必定拒绝；提交之后已经交给原生，不能再假装可以撤回。
        std::lock_guard<std::mutex> lock(stateMutex);
        safe=status.token==pending.token && status.phase==RevisitNativePhase::ClosingMap && dispatchArmed;
        if (safe) {
            status.phase=RevisitNativePhase::Dispatched; dispatchArmed=false;
            arrivalPoint=goal;arrivalPointValid=true;sawLoadTransition=false;arrivalFrames=0;
            beforeLoadDescriptor=loadDescriptor;beforeLoadPosition=dispatchPosition;beforeLoadPositionValid=dispatchPositionValid;
            dispatchedAt=GetTickCount64();
        }
    }
    if (!safe) {
        // 即使前面的短路条件已经失败，也重新读取当前消费者持有的关系来清理，
        // 不能因minimap未赋值而把旧result=1留给原生下一帧无限重试。
        uintptr_t liveMinimap=0, liveMenu=1;
        uint32_t liveId=0, liveKind=0;
        if (Read(field+0x730,liveMinimap) && liveMinimap==handoffMinimapIdentity &&
            Read(liveMinimap+0x28,liveMenu) && !liveMenu &&
            Read(liveMinimap+0x310,liveId) && liveId==target &&
            Read(liveMinimap+0x314,liveKind) && liveKind==1) {
            minimap=liveMinimap; menu=liveMenu; resultId=liveId; resultKind=liveKind;
        }
        const uint32_t cancelled=0;
        if (minimap && !menu && resultId==target && resultKind==1) Write(minimap+0x314,cancelled);
        SetPhase(pending.token,RevisitNativePhase::Rejected);
        Log("Revisit: request rejected before native map-jump dispatch.");
        // 清理结果失败时仍保留armed身份，让后续重复消费者继续被拦截。
        uint32_t remaining=1;
        if (minimap && Read(minimap+0x314,remaining) && remaining==0) {
            std::lock_guard<std::mutex> lock(stateMutex); dispatchArmed=false;
        }
        return true;
    }
    if (returning || forestEntry) {
        // 准入回调之后再次检查可选保护。返回普通地图不受此限制；任何指向迷途
        // 之森的自定义加载都不能在保护失效后继续执行，哨兵也不能落入原生查表。
        if (!std::strcmp(goal.scene,forest::kScene) && !ForestEntryAllowed(context)) {
            const uint32_t consumed=0;
            Write(minimap+0x314,consumed);
            SetPhase(pending.token,RevisitNativePhase::Rejected);
            return true;
        }
        // 本 helper 已由 MASM 保存原生消费者的全部易失寄存器与标志，且处于
        // 原生关图已完成的同一游戏线程。只调用完整加载入口，不写 actor 坐标。
        // 4001 保留普通落地和 MapJumpAfterCallScript；同场景使用原生 20 分支。
        const uint32_t loadFlags=0x4001u|(std::strcmp(context.scene,goal.scene)==0?0x20u:0u);
        const int32_t noSpot=-1;
        if (!Write(field+0x1BD4,noSpot)) {
            SetPhase(pending.token,RevisitNativePhase::Rejected);
        } else {
            const bool previousTrip=ExperimentalRevisitTripActive(context.chapter);
            if constexpr (revisit_policy::kUnrestricted)
                SetExperimentalRevisitTripActive(!returning,context.chapter);
            nativeLoad(field,goal.scene,nullptr,goal.xyz,goal.yawRadians,loadFlags);
            uint32_t busy=0;
            // 29CF90 是 void，不能把“函数返回”当作加载成功。它同步设置繁忙位；
            // 没有观察到此交接时保留返程记录，由玩家重试，绝不立即确认到达。
            const bool accepted=Read(field+0x1BC8,busy) && busy!=0;
            if constexpr (revisit_policy::kUnrestricted)
                if (!accepted) SetExperimentalRevisitTripActive(previousTrip,context.chapter);
            std::lock_guard<std::mutex> lock(stateMutex);
            if (status.token==pending.token) {
                sawLoadTransition=accepted;
                if (!accepted) status.phase=RevisitNativePhase::Rejected;
            }
        }
        const uint32_t consumed=0;
        Write(minimap+0x314,consumed);
        Log(forestEntry ? "Revisit: guarded Lost Woods entry handed to native loader; awaiting arrival." :
            "Revisit: exact return handed to the native full scene loader; awaiting arrival.");
        // 29966F/29968A 的两个调用者均不使用 AL。true 要求 shim 跳过原生 spot
        // 查表，否则保留哨兵会触发无意义的第二次查表。其他寄存器/栈保持原样。
        return true;
    }
    // MASM随后恢复原寄存器/原栈并尾跳原生函数；此状态表示已交给原生执行，并不
    // 等同于加载已经结束。界面仍需观察真实场景变化，不能立即声称成功到达。
    if constexpr (revisit_policy::kUnrestricted) SetExperimentalRevisitTripActive(true,context.chapter);
    Log("Revisit: handoff accepted by the original map-jump consumer.");
    return false;
}
}

extern "C" void Sky2BeforeRevisitUpdate(uintptr_t minimap) noexcept {
    auto context=Capture(minimap);
    RevisitReturnPoint point{};
    const bool pointValid=CapturePoint(context,point);
    // 必须在真实捕获之后赋值，读取失败时清除上帧就绪状态。此值仅用于首次
    // 出发预检/提示；Publish仍独立保存pointValid，后续读取和派发继续复核。
    context.returnPointReady=pointValid;
    Publish(context,point,pointValid);
    LogNativeState(context,pointValid);
    ClearCancelledHandoff(minimap);
}
extern "C" bool Sky2BeforeRevisitJump(uintptr_t field, uint32_t target, uintptr_t caller) noexcept {
    return BeforeJump(field,target,caller);
}
void ObserveRevisitNativeRules(uintptr_t manager) noexcept {
    const auto context=Capture(manager);
    ruleProof={};
    if (!context.valid || context.busy) return;
    ruleProof.valid=true;ruleProof.manager=manager;ruleProof.chapter=context.chapter;
    ruleProof.region=context.region;ruleProof.signature=context.progressSignature;
    std::memcpy(ruleProof.scene,context.scene,sizeof(ruleProof.scene));
}

void SetRevisitNativeAuthorize(RevisitNativeAuthorize callback) noexcept { authorize.store(callback); }
RevisitNativeContext ReadRevisitNativeContext() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex); return published;
}
RevisitNativeStatus ReadRevisitNativeStatus() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex);
    // 场景卸载时更新钩子也可能暂时停调用；超时是本模块状态，不需要触碰游戏对象。
    // 因此UI读取同样能结束无期限等待，并保留“曾经派发”的独立终态。
    if (status.phase==RevisitNativePhase::Dispatched && GetTickCount64()-dispatchedAt>60000)
        status.phase=RevisitNativePhase::ArrivalUnconfirmed;
    return status;
}
bool RevisitNativeTargetAvailable(uint32_t target, const RevisitNativeContext& context) noexcept {
    if (!SupportedSource(context)) return false;
    // 实际返程记录由协调层持有，必须在那里及Queue/最终消费者按具体scene审核；
    // 当前场景的首次出发预检不能误用于另一个已经保存的出发点。
    if (target==kRevisitReturnTarget) return true;
    if (target==forest::kTarget) return ForestEntryAllowed(context);
    if constexpr (revisit_policy::kUnrestricted) {
        return RevisitNativeOrdinaryTargetAvailable(target,context);
    } else {
    if (target==15 && context.chapter==8 && context.region==6) return true;
    for (size_t i=0;i<std::size(destinations);++i)
        if (destinations[i].id==target && target!=15) {
            if (context.destinationMask&(1u<<i)) return true;
            // 新市政府例外未满足时仍可依照原生菜单正常开放；原旧地图则不能借
            // 一般registered结果绕过已经审查的剧情门槛，尤其荣耀号特殊宝箱。
            if (target!=108 && context.chapter>=8) return false;
        }
    return RevisitNativeOrdinaryTargetAvailable(target,context);
    }
}
bool RevisitNativeOrdinaryTargetAvailable(uint32_t target,const RevisitNativeContext& context) noexcept {
    if constexpr (revisit_policy::kUnrestricted) {
        // 仅开放玩家清单里的155个真实原生ID。未登记/灰态/分组/前置剧情不再
        // 构成实验许可，但隐藏内部ID和自定义哨兵不能借这条分支交给原生查表。
        const auto* row=CatalogDestination(target,0);
        return SupportedSource(context) && target>0 && target<1001 && RevisitDestinationListed(target) &&
            row && row->scene && row->scene[0] && row->place;
    } else {
        return SupportedSource(context) && target<context.nativeRuleStatus.size() && context.nativeRuleStatus[target]==RuleAllowed;
    }
}
bool RevisitNativeReturnPhaseAllowed(const RevisitReturnPoint& point,const RevisitNativeContext& context) noexcept {
    return context.valid && point.chapter==context.chapter && ValidRevisitReturnPoint(point) &&
        ReturnScenePhaseAllowed(context,point.scene,point.mapPlace,point.region);
}
const char* RevisitNativeTargetReason(uint32_t target,const RevisitNativeContext& context) noexcept {
    // 柏斯的旧版应急许可最终由协调层结合“有无返程记录”决定。此函数只在界面
    // 已判定不可用时提供原因，因此不能因底层应急候选而把普通点禁用原因清空。
    if (target==15 ? RevisitNativeOrdinaryTargetAvailable(target,context) :
        RevisitNativeTargetAvailable(target,context)) return "";
    if (!context.valid) return "等待游戏场景数据";
    if constexpr (revisit_policy::kUnrestricted) {
        // 实验策略不再以剧情旗标灰化目的地；失败原因必须指向实际加载条件，
        // 避免把保护未安装或未知目录误报成“原剧情尚未完成”。
        if (!SupportedSource(context)) return "当前场景身份尚未通过核对";
        if (target==forest::kTarget) return ForestRevisitGuardReady() ?
            "场景加载接口尚未就绪" : "迷途之森剧情保护尚未就绪";
        return "目的地原生数据尚未通过核对";
    } else {
    if (!SupportedSource(context)) return "当前场景或剧情阶段尚未支持";
    if (target==forest::kTarget) return ForestRevisitGuardReady() ?
        "迷途之森原剧情尚未完成" : "迷途之森剧情保护尚未就绪";
    if (context.chapter>=8 && target!=15 && target!=108 && FindDestination(target)) return "旧地图剧情完成条件尚未满足";
    const auto state=target<context.nativeRuleStatus.size()?context.nativeRuleStatus[target]:RuleUnknown;
    switch (state) {
    case RuleUnregistered:return "当前剧情尚未登记此地点";
    case RuleBlocked:return "游戏当前剧情禁用此地点";
    case RuleAreaBlocked:return "游戏当前剧情禁用此分组";
    case RuleInternal:return "内部或特殊入口，尚未适配";
    case RuleBeforeScript:return "请先完成游戏原生传送剧情";
    case RuleInconsistent:return "原生传送数据未通过核对";
    default:return "请打开区域地图，等待原生传送规则更新";
    }
    }
}
bool ReadRevisitNativeReturnPoint(const RevisitNativeContext& expected, RevisitReturnPoint& output) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex);
    output={};
    if (!publishedPointValid || !SameContext(expected,published) || !published.browsing ||
        expected.busy || !expected.browseIdentity || expected.browseIdentity!=published.browseIdentity ||
        GetTickCount64()-publishedAt>1000) return false;
    output=publishedPoint;return true;
}
void CancelRevisitNativeTravel() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (status.phase==RevisitNativePhase::Queued || status.phase==RevisitNativePhase::ClosingMap)
        status.phase=RevisitNativePhase::Rejected;
    // 退出已提交后仍保留armed身份，由更新线程撤回结果或由消费者拒绝该请求。
}
bool QueueRevisitNativeTravel(uint32_t target, uint64_t token,
                              const RevisitNativeContext& expected) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!token || !available.load() || !authorize.load() ||
        !RevisitNativeTargetAvailable(target,expected) || !expected.browsing || expected.busy ||
        !SameContext(expected,published) || !published.browsing || GetTickCount64()-publishedAt>1000 ||
        !expected.browseIdentity || expected.browseIdentity!=published.browseIdentity ||
        dispatchArmed ||
        status.phase==RevisitNativePhase::Queued || status.phase==RevisitNativePhase::ClosingMap ||
        status.phase==RevisitNativePhase::Dispatched) return false;
    status={token,target,RevisitNativePhase::Queued};
    expectedContext=expected;
    requestedAt=GetTickCount64();
    requestedReturn={};arrivalPointValid=false;
    return true;
}

bool QueueRevisitNativeReturn(const RevisitReturnPoint& point, uint64_t token,
                              const RevisitNativeContext& expected) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!token || !available.load() || !authorize.load() || !nativeLoad ||
        !ValidRevisitReturnPoint(point) || point.yawRadians<0 || point.yawRadians>=6.2831854820251464844f ||
        point.chapter!=expected.chapter || !SupportedSource(expected) || !RevisitNativeReturnPhaseAllowed(point,expected) ||
        !expected.browsing || expected.busy || !SameContext(expected,published) || !published.browsing ||
        GetTickCount64()-publishedAt>1000 || !expected.browseIdentity ||
        expected.browseIdentity!=published.browseIdentity || dispatchArmed ||
        status.phase==RevisitNativePhase::Queued || status.phase==RevisitNativePhase::ClosingMap ||
        status.phase==RevisitNativePhase::Dispatched) return false;
    // 只复制数据：静态地点表最终验证及游戏加载调用均留给游戏线程。
    status={token,kRevisitReturnTarget,RevisitNativePhase::Queued};
    expectedContext=expected;requestedReturn=point;requestedAt=GetTickCount64();arrivalPointValid=false;
    return true;
}

bool BeforeRevisitNativeBrowse(uintptr_t menu) noexcept {
    RevisitNativeStatus pending{};
    RevisitNativeContext expected{};
    RevisitReturnPoint returnPoint{};
    ULONGLONG startedAt=0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        pending=status; expected=expectedContext; startedAt=requestedAt;returnPoint=requestedReturn;
    }
    if (pending.phase!=RevisitNativePhase::Queued) return false;
    uintptr_t minimap=0, realMenu=0;
    int32_t depth=0,current=0;
    const auto context=Capture();
    const auto permission=authorize.load();
    const bool destinationValid=pending.target==kRevisitReturnTarget ?
        SafeReturnDestination(returnPoint,context) : pending.target==forest::kTarget ?
        ValidateForestDestination(context) : ValidateDestination(pending.target);
    if (GetTickCount64()-startedAt>5000) { SetPhase(pending.token,RevisitNativePhase::Expired); return true; }
    if (!context.available || !SameContext(context,expected) || !RevisitNativeTargetAvailable(pending.target,context) ||
        !context.browsing || context.browseIdentity!=expected.browseIdentity || !destinationValid ||
        !Read(menu+8,minimap) || !Read(minimap+0x28,realMenu) || realMenu!=menu ||
        !StableBrowse(menu,minimap,depth,current) || !permission || !permission(pending.target,context,pending.token)) {
        SetPhase(pending.token,RevisitNativePhase::Rejected); return true;
    }
    uintptr_t callback=0;
    uint64_t oldResult=0;
    if (!Read(menu+0xF8,callback) || !Read(menu+0x3C0,oldResult) || oldResult) {
        SetPhase(pending.token,RevisitNativePhase::Rejected); return true;
    }
    // 默认策略已经只读排除该目标活跃的MapJumpCallScript分支，并对早期精确
    // 返程审核目标场景。实验策略明确略过这层剧情许可，仍保留上述运行条件和
    // 真实目标数据核对；两种策略都不会调用旧传送剧情来伪造剧情完成状态。
    // 直接提交普通结果并正常关闭，不伪造display对象、不倒退章节。状态8略过的
    // MapJumpVoice只涉及跨海拔淡出/语音，实际换图淡出仍由29CF90完整状态机执行。
    const uint64_t result=static_cast<uint64_t>(pending.target)|(1ull<<32);
    const int32_t closeState=17;
    // 原有选择仍指向有效原生显示数组；退出状态不再读取它。保留选择可以确保任何
    // 结果写入失败都不会留下“仍在浏览但选择为空”的不完整菜单。
    {
        // 先持锁确立交接身份，再写原生结果。渲染线程在此后Cancel只会将它标记
        // 为拒绝，不能被本函数尾部再次改回ClosingMap而复活已经取消的请求。
        std::lock_guard<std::mutex> lock(stateMutex);
        if (status.token!=pending.token || status.phase!=RevisitNativePhase::Queued) return true;
        status.phase=RevisitNativePhase::ClosingMap;
        dispatchArmed=true;
        handoffMenuIdentity=menu; handoffMinimapIdentity=minimap;
    }
    if (!Write(menu+0x3C0,result)) {
        SetPhase(pending.token,RevisitNativePhase::Rejected); return true;
    }
    if (callback) reinterpret_cast<void (*)(uintptr_t,int32_t,int32_t)>(callback)(menu,current,closeState);
    if (!Write(menu+0xBC+static_cast<uintptr_t>(depth)*12,closeState)) {
        Write(menu+0x3C0,oldResult);
        SetPhase(pending.token,RevisitNativePhase::Rejected); return true;
    }
    Log("Revisit: native map-close result submitted; waiting for original consumer.");
    return true;
}

void InstallRevisitNative(uintptr_t gameBase) noexcept {
    base=gameBase;
    // 不只核对挂钩入口：结果复制、最终消费者和换图函数同时验证，防止版本/其它
    // 补丁改变交接约定。本适配失败时只禁用回访，现有宝箱/探索功能仍可使用。
    const unsigned char update[]={0x40,0x57,0x48,0x83,0xEC,0x30,0x80,0xB9,0x0B,0x03,0,0,0,0x48,0x8B,0xF9};
    const unsigned char jump[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x40};
    const unsigned char load[]={0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
    const unsigned char consume[]={0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x63,0x81,0xEC,0,0,0,0x48,0x8B,0xF9};
    const unsigned char copy[]={0x48,0x8B,0x82,0xC0,0x03,0,0,0x48,0x89,0x87,0x10,0x03,0,0};
    const unsigned char close[]={0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x63,0x81,0xEC,0,0,0,0x48,0x8B,0xD9};
    const unsigned char actorGetter[]={0x48,0x8B,0x41,0x60,0xC3};
    const unsigned char savePosition[]={0x8B,0x88,0xE8,0,0,0,0x89,0x8F,0x2C,0x14,0x05,0,
        0x8B,0x88,0xEC,0,0,0,0x89,0x8F,0x30,0x14,0x05,0,0x8B,0x80,0xF0,0,0,0,0x89,0x87,0x34,0x14,0x05,0};
    const unsigned char saveYaw[]={0x48,0x8D,0x90,0x08,0x01,0,0,0x4C,0x8D,0x45,0xA7,
        0x48,0x8D,0x4D,0x97,0xE8,0x51,0x8B,0xC2,0xFF,0xF3,0x0F,0x10,0x4D,0x9F,
        0xF3,0x0F,0x10,0x45,0x97,0xE8,0x4A,0x6D,0x46,0};
    const unsigned char loadPosition[]={0xF3,0x0F,0x10,0x96,0xA8,0x1B,0,0,0xF3,0x0F,0x11,0x90,0xD8,0,0,0};
    const unsigned char resolvePlace[]={0xE8,0xCA,0x60,0,0,0x48,0x89,0x86,0x48,0x06,0,0};
    if (!Matches(base+0x3D68F0,update) || !Matches(base+0x29CE70,jump) ||
        !Matches(base+0x29CF90,load) || !Matches(base+0x299590,consume) ||
        !Matches(base+0x3D69C9,copy) || !Matches(base+0x3E86B0,close) ||
        !Matches(base+0x2E0880,actorGetter) || !Matches(base+0x43926F,savePosition) ||
        !Matches(base+0x4392AB,saveYaw) || !Matches(base+0x29F056,loadPosition) || !Matches(base+0x29F251,resolvePlace)) {
        Log("Revisit: native adapter validation failed."); return;
    }
    void* updateTarget=reinterpret_cast<void*>(base+0x3D68F0);
    void* jumpTarget=reinterpret_cast<void*>(base+0x29CE70);
    if (MH_CreateHook(updateTarget,reinterpret_cast<void*>(&Sky2RevisitUpdateShim),&Sky2NextRevisitUpdate)!=MH_OK) return;
    if (MH_CreateHook(jumpTarget,reinterpret_cast<void*>(&Sky2RevisitJumpShim),&Sky2NextRevisitJump)!=MH_OK) {
        MH_RemoveHook(updateTarget); return;
    }
    if (MH_EnableHook(jumpTarget)!=MH_OK || MH_EnableHook(updateTarget)!=MH_OK) {
        MH_DisableHook(updateTarget); MH_DisableHook(jumpTarget);
        MH_RemoveHook(updateTarget); MH_RemoveHook(jumpTarget); return;
    }
    nativeLoad=reinterpret_cast<NativeLoad>(base+0x29CF90);
    available.store(true);
    Log("Revisit: chapter 0-9 native map-result and guarded exact-position return adapter ready.");
}
}
