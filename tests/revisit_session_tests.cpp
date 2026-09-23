// 直接运行生产协调层，用替身隔离游戏和磁盘，验证出发点生命周期与授权顺序。
// 真实Windows原子I/O由revisit_journal_io_tests独立覆盖。
#include "../native/revisit.cpp"
#include <cstdio>
namespace tracker {
static RevisitNativeStatus fixtureStatus{};
static RevisitReturnPoint fixturePoint{};
static bool fixtureDisk=true,fixtureCapture=true,fixtureQueue=true,fixtureAvailable=true;
static bool fixtureOrdinaryAvailable=true;
static bool fixtureReturnPhaseAllowed=true;
static RevisitReturnPoint fixturePhaseCheckedPoint{};
static unsigned fixtureWrites=0,fixtureQueues=0;
static RevisitReturnPoint fixtureQueuedReturn{};
void Log(const char*) noexcept {}
ExplorationStatus ReadExplorationStatus() noexcept { ExplorationStatus v{};v.travelAvailable=true;return v; }
bool InstallRevisitEventGuard(uintptr_t) noexcept { return true; }
bool InstallForestRevisitGuard(uintptr_t) noexcept { return true; }
static bool fixtureExperimentalTrip=false;
void SetExperimentalRevisitTripActive(bool active,uint32_t) noexcept { fixtureExperimentalTrip=active; }
bool ExperimentalRevisitTripActive(uint32_t) noexcept { return fixtureExperimentalTrip; }
void SetRevisitEventGuardActive(bool) noexcept {}
void SetRevisitNativeAuthorize(RevisitNativeAuthorize) noexcept {}
void InstallRevisitNative(uintptr_t) noexcept {}
RevisitNativeStatus ReadRevisitNativeStatus() noexcept { return fixtureStatus; }
bool RevisitNativeTargetAvailable(uint32_t,const RevisitNativeContext&) noexcept { return fixtureAvailable; }
bool RevisitNativeOrdinaryTargetAvailable(uint32_t,const RevisitNativeContext&) noexcept { return fixtureOrdinaryAvailable; }
bool RevisitNativeReturnPhaseAllowed(const RevisitReturnPoint& point,const RevisitNativeContext&) noexcept {
    fixturePhaseCheckedPoint=point;return fixtureReturnPhaseAllowed;
}
bool ReadRevisitNativeReturnPoint(const RevisitNativeContext&,RevisitReturnPoint& point) noexcept {
    point=fixturePoint;return fixtureCapture;
}
bool QueueRevisitNativeTravel(uint32_t target,uint64_t token,const RevisitNativeContext&) noexcept {
    ++fixtureQueues;
    if (fixtureQueue) fixtureStatus={token,target,RevisitNativePhase::Queued};
    return fixtureQueue;
}
bool QueueRevisitNativeReturn(const RevisitReturnPoint& point,uint64_t token,const RevisitNativeContext& context) noexcept {
    fixtureQueuedReturn=point;return QueueRevisitNativeTravel(kRevisitReturnTarget,token,context);
}
bool WriteRevisitRecordFile(const std::wstring&,const RevisitReturnRecord&) noexcept { ++fixtureWrites;return fixtureDisk; }
bool ReadRevisitRecordHistory(const std::wstring&,std::vector<RevisitReturnRecord>& values) noexcept { values.clear();return true; }
}
using namespace tracker;
int main() {
    unsigned failures=0;
    const auto check=[&](bool ok,const char* name) { if(!ok) { ++failures;std::printf("FAIL %s\n",name); } };
    InstallRevisit(0,L"unused-test-path");
    RevisitNativeContext source{};source.valid=source.available=source.browsing=true;source.busy=false;
    source.returnPointReady=true;
    source.chapter=9;source.region=7;source.progressSignature=123;
    std::memcpy(source.scene,"mp5600_03",sizeof("mp5600_03"));
    std::memcpy(fixturePoint.scene,source.scene,sizeof(source.scene));
    fixturePoint.chapter=9;fixturePoint.region=7;fixturePoint.place=fixturePoint.mapPlace=1560003;
    fixturePoint.xyz[0]=-528;fixturePoint.xyz[1]=-24;fixturePoint.xyz[2]=-743;fixturePoint.yawRadians=5.74f;
    fixturePoint.progressSignature=123;
    // 场景和目的地均合法，也必须先有可保存的真实出发点。捕获未通过时不排队、
    // 不写历史，避免玩家只看到含糊的“上次请求失败”并不断重复确认。
    source.returnPointReady=false;
    check(!RevisitTargetAllowed(99,source) && !QueueRevisitTravel(99,90,source) &&
        fixtureWrites==0 && fixtureQueues==0,"missing origin readiness blocks first departure before any side effects");
    source.returnPointReady=true;
    fixtureDisk=false;
    check(!QueueRevisitTravel(99,1,source) && fixtureQueues==0,"disk failure never queues game travel");
    fixtureDisk=true;fixtureCapture=false;
    const auto writes=fixtureWrites;
    check(!QueueRevisitTravel(99,2,source) && fixtureWrites==writes,"invalid pose never persists invented coordinates");
    fixtureCapture=true;
    check(QueueRevisitTravel(99,3,source),"first departure submitted");
    const auto first=ReadRevisitReturnStatus(source);
    check(first.active && first.record.point.xyz[0]==-528,"first original position active");
    check(AuthorizeRevisit(99,source,3) && !AuthorizeRevisit(99,source,2) && !AuthorizeRevisit(100,source,3),
        "game thread authorization binds exact request token and target");
    check(!QueueRevisitTravel(101,4,source),"pending departure cannot be replaced");
    fixtureStatus.phase=RevisitNativePhase::Arrived;
    RevisitNativeContext old=source;old.region=6;std::memcpy(old.scene,"mp6011",sizeof("mp6011"));
    ReadRevisitReturnStatus(old);
    fixturePoint.xyz[0]=5000;
    const auto beforeHop=fixtureWrites;
    check(QueueRevisitTravel(120,5,old) && fixtureWrites==beforeHop,"old map hop does not overwrite first departure");
    fixtureStatus.phase=RevisitNativePhase::Arrived;
    check(ReadRevisitReturnStatus(old).record.ticket==first.record.ticket,"hopping retains original ticket");
    auto unavailableOrigin=old;unavailableOrigin.returnPointReady=false;
    check(RevisitTargetAllowed(120,unavailableOrigin) && RevisitTargetAllowed(kRevisitReturnTarget,unavailableOrigin) &&
        ReadRevisitReturnStatus(unavailableOrigin).record.ticket==first.record.ticket,
        "existing trip and explicit return retain the original record when current pose is unavailable");
    // 从四塔等步行离开旧地图后也保持同一出发点，不把普通大地图出口作为新起点。
    check(ReadRevisitReturnStatus(source).active,"walking back to ordinary region preserves trip");
    source.progressSignature=999;
    check(QueueRevisitTravel(kRevisitReturnTarget,6,source) && fixtureQueuedReturn.xyz[0]==-528,
        "legitimate story interaction still returns to initial position");
    fixtureStatus.phase=RevisitNativePhase::Dispatched;
    check(ReadRevisitReturnStatus(source).active,"dispatch alone does not finish trip");
    fixtureStatus.phase=RevisitNativePhase::Arrived;
    fixtureExperimentalTrip=true;
    check(!ReadRevisitReturnStatus(source).active,"confirmed exact arrival ends active trip");
    check(!fixtureExperimentalTrip,"confirmed exact return clears experimental trip protection");
    check(ReadRevisitReturnStatus(source).hasRecord,"returned trip retained for old autosave recovery");
    fixtureQueue=false;
    check(!QueueRevisitTravel(99,7,source) && !ReadRevisitReturnStatus(source).active,
        "queue rejection releases only newly prepared active anchor");
    fixtureQueue=true;fixtureStatus={};
    check(QueueRevisitTravel(99,8,source),"new trip starts after previous return");
    fixtureStatus.phase=RevisitNativePhase::ArrivalUnconfirmed;
    check(ReadRevisitReturnStatus(old).active,"arrival timeout never discards original departure");
    check(QueueRevisitTravel(120,81,old),"stable scene can queue another travel after unconfirmed arrival");
    fixtureStatus.phase=RevisitNativePhase::Arrived;
    ReadRevisitReturnStatus(old);
    check(QueueRevisitTravel(120,9,old),"next hop prepared");
    fixtureStatus.phase=RevisitNativePhase::Rejected;
    check(ReadRevisitReturnStatus(old).active,"failed hop preserves existing active anchor");
    hasActive=false;preparedToken=0;fixtureStatus={};
    const auto recovered=ReadRevisitReturnStatus(old);
    check(recovered.hasRecord && !recovered.active,"restart exposes history as candidate only");
    check(!RevisitTargetAllowed(120,old) && RevisitTargetAllowed(kRevisitReturnTarget,old),
        "old-map recovery must return before capturing another departure");
    for (const char* scene : {"mp6100", "mp8500", "mp0081"}) {
        std::memset(old.scene,0,sizeof(old.scene)); std::memcpy(old.scene,scene,std::strlen(scene)+1);
        check(!RevisitTargetAllowed(120,old) && RevisitTargetAllowed(kRevisitReturnTarget,old),
            "outdoor revisit transitions also require recovery after restart");
    }
    CycleRevisitReturnRecord(old);
    check(ReadRevisitReturnStatus(old).record.ticket!=recovered.record.ticket,"multiple historical anchors remain selectable");
    old.chapter=8;
    old.returnPointReady=true;
    std::memset(old.scene,0,sizeof(old.scene));std::memcpy(old.scene,"mp6011",sizeof("mp6011"));
    fixtureOrdinaryAvailable=false;
    check(!RevisitTargetAllowed(kRevisitReturnTarget,old) &&
        RevisitTargetAllowed(15,old)==!revisit_policy::kUnrestricted,
        "legacy fallback bypass is limited to the standard policy");
    fixtureOrdinaryAvailable=false;
    // 无记录的0.5.0应急出口和已有行程中的普通柏斯必须分开授权。即使底层提供
    // 旧版应急候选，只要有本章节记录，就不能覆盖原生普通点的未登记/禁用结果。
    activeRecord.point.chapter=8;hasActive=true;
    check(!RevisitTargetAllowed(15,old),"active chapter8 trip cannot bypass ordinary Bose restriction");
    hasActive=false;activeRecord.point.chapter=9;
    old.chapter=9;
    check(!RevisitTargetAllowed(15,old),"Bose fallback unavailable in final chapter");
    fixtureStatus={};
    check(!RevisitTargetAllowed(15,source),"ordinary Bose requires its native rule");
    fixtureOrdinaryAvailable=true;
    const auto normalWrites=fixtureWrites;
    check(QueueRevisitTravel(15,10,source) && fixtureWrites==normalWrites+1,
        "ordinary Bose destination records a departure outside legacy fallback");
    fixtureAvailable=false;
    check(!RevisitTargetAllowed(120,source),"native story restriction reflected in UI availability");

    // 各章正常流程可能使用同一实体旧图。协调层允许记录真实站位，具体传送点是否
    // 可用继续交给原生层；只有已验证的第8/9章保持重载回访的手动恢复策略。
    InstallRevisit(0,L"unused-early-test-path");
    fixtureStatus={};fixtureAvailable=fixtureOrdinaryAvailable=fixtureCapture=fixtureQueue=fixtureDisk=true;
    for (uint32_t chapter=0;chapter<=9;++chapter) {
        auto current=source;current.chapter=chapter;
        check(RevisitContextAllowed(current),"every actual chapter is eligible for native checking");
        for (const char* scene : {"mp6011","mp6100","mp8500","mp0054_01","mp0081"}) {
            std::memset(current.scene,0,sizeof(current.scene));
            std::memcpy(current.scene,scene,std::strlen(scene)+1);
            check(RevisitRecoveryRequired(current)==(chapter>=8),
                "normal early-chapter story scenes are distinct from late recovery scenes");
            check(RevisitTargetAllowed(120,current)==(chapter<8),
                "new departure only inherits recovery restrictions in chapters eight and nine");
        }
    }
    for (uint32_t chapter : {10u,0x40000000u,0xFFFFFFFFu}) {
        auto invalid=source;invalid.chapter=chapter;
        check(!RevisitContextAllowed(invalid) && !RevisitTargetAllowed(120,invalid),
            "unknown chapters never start a trip");
    }

    auto early=source;early.chapter=0;early.region=6;early.progressSignature=111;
    std::memset(early.scene,0,sizeof(early.scene));std::memcpy(early.scene,"mp6011",sizeof("mp6011"));
    fixturePoint={};std::memcpy(fixturePoint.scene,early.scene,sizeof(early.scene));
    fixturePoint.chapter=0;fixturePoint.region=6;fixturePoint.place=fixturePoint.mapPlace=1601100;
    fixturePoint.xyz[0]=12.5f;fixturePoint.progressSignature=111;
    early.beforeScriptReturnBlocked=true;
    const auto beforeStoryWrites=fixtureWrites;
    if constexpr (revisit_policy::kUnrestricted) {
        // 仅取消剧情接管准入；这里不真的排队，以便后续共同验证持久化与返程生命周期。
        check(RevisitTargetAllowed(120,early) && fixtureWrites==beforeStoryWrites,
            "experimental departure ignores story phase without inventing an origin");
        early.returnPointReady=false;
        check(!RevisitTargetAllowed(120,early) && !QueueRevisitTravel(120,100,early) && fixtureWrites==beforeStoryWrites,
            "experimental policy retains verified-origin requirement");
        early.returnPointReady=true;
    } else {
        check(!RevisitTargetAllowed(120,early) && !QueueRevisitTravel(120,100,early) && fixtureWrites==beforeStoryWrites,
            "story-controlled return blocks first departure before writing a record");
    }
    early.beforeScriptReturnBlocked=false;
    check(QueueRevisitTravel(120,101,early),"prologue normal-map departure records its actual position");
    const auto earlyTicket=ReadRevisitReturnStatus(early).record.ticket;
    fixtureStatus.phase=RevisitNativePhase::Arrived;
    early.beforeScriptReturnBlocked=true;
    check(RevisitTargetAllowed(120,early),"existing trip leaves ordinary destination decisions to native rules");
    // 当前源场景的前置分支不等于已记录返程目标的分支。协调层传入具体记录，
    // 最终消费者仍会按最新剧情核对；拒绝时不能排队、结束活动点或替换原记录。
    auto differentSource=early;
    std::memset(differentSource.scene,0,sizeof(differentSource.scene));
    std::memcpy(differentSource.scene,"mp6012",sizeof("mp6012"));
    check(RevisitTargetAllowed(kRevisitReturnTarget,differentSource) &&
        std::strcmp(fixturePhaseCheckedPoint.scene,"mp6011")==0 && fixturePhaseCheckedPoint.xyz[0]==12.5f,
        "exact return checks the stored destination even when the current source is phase-blocked");
    early.beforeScriptReturnBlocked=false;
    fixtureReturnPhaseAllowed=false;
    const auto beforeReturnRejectQueues=fixtureQueues;
    check(!RevisitTargetAllowed(kRevisitReturnTarget,early) &&
        !QueueRevisitTravel(kRevisitReturnTarget,103,early) && fixtureQueues==beforeReturnRejectQueues &&
        ReadRevisitReturnStatus(early).record.ticket==earlyTicket,
        "blocked destination phase rejects exact return and preserves the active origin");
    fixtureReturnPhaseAllowed=true;
    early.progressSignature=112;
    check(ReadRevisitReturnStatus(early).active,"legitimate same-chapter flag changes preserve active origin");
    auto transient=early;transient.valid=false;transient.chapter=9;
    ReadRevisitReturnStatus(transient);
    check(ReadRevisitReturnStatus(early).active,"incomplete loading context does not infer a chapter switch");
    auto anotherChapter=early;anotherChapter.chapter=1;
    fixtureExperimentalTrip=true;
    const auto switched=ReadRevisitReturnStatus(anotherChapter);
    check(!switched.active && !switched.hasRecord,"real chapter switch retires active origin without cross-chapter reuse");
    check(!fixtureExperimentalTrip,"chapter switch does not carry experimental protection into normal story");
    const auto manualRecovery=ReadRevisitReturnStatus(early);
    check(!manualRecovery.active && manualRecovery.hasRecord && manualRecovery.record.ticket==earlyTicket,
        "returning to old chapter keeps history but never revives the old active trip");
    check(!AuthorizeRevisit(120,early,101),"retired chapter request cannot remain authorized");
    check(QueueRevisitTravel(120,102,early) && ReadRevisitReturnStatus(early).record.ticket!=earlyTicket,
        "new normal-story departure captures a fresh origin after chapter switch");
    if constexpr (revisit_policy::kUnrestricted) {
        // 第8章旧版无记录存档仍能离开序章，但实验策略必须先建立真实活动锚点。
        // 这防止最终消费者开启了跨章保护、协调层却认为不存在可清理的活动行程。
        InstallRevisit(0,L"unused-experimental-emergency");
        fixtureStatus={};fixtureAvailable=fixtureOrdinaryAvailable=fixtureCapture=fixtureQueue=fixtureDisk=true;
        auto legacy=early;legacy.chapter=8;legacy.beforeScriptReturnBlocked=true;
        fixturePoint.chapter=8;
        const auto beforeEmergency=fixtureWrites;
        legacy.returnPointReady=false;
        check(!QueueRevisitTravel(15,200,legacy) && fixtureWrites==beforeEmergency,
            "experimental emergency still requires a valid origin");
        legacy.returnPointReady=true;
        check(QueueRevisitTravel(15,201,legacy) && fixtureWrites==beforeEmergency+1 &&
            ReadRevisitReturnStatus(legacy).active,
            "experimental legacy exit persists an anchor before native submission");
        fixtureExperimentalTrip=true;
        auto nextChapter=legacy;nextChapter.chapter=9;
        check(!ReadRevisitReturnStatus(nextChapter).active && !fixtureExperimentalTrip,
            "experimental emergency anchor participates in chapter cleanup");
    }
    std::printf("%u revisit session failure(s)\n",failures);
    return failures ? 1 : 0;
}
