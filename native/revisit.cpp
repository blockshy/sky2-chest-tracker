// 回访协调层：出发前持久保存精确位置，跨旧地图时只复用最初位置，返回确认到达后
// 才结束会话。所有记录都是Mod自己的文件；不回退剧情、不改游戏保存数据。
#include "revisit.h"
#include "revisit_event_guard.h"
#include "revisit_forest.h"
#include "exploration.h"
#include "log.h"
#include "revisit_policy.h"
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <ctime>
#include <bcrypt.h>

namespace tracker {
namespace {
std::atomic<bool> ready{false};
std::mutex sessionMutex;
std::wstring recordFolder;
std::vector<RevisitReturnRecord> records;
bool storageReady=false;
bool hasActive=false;
bool preparedFirstDeparture=false;
RevisitReturnRecord activeRecord{};
size_t selectedRecord=0;
uint64_t preparedToken=0;
uint32_t preparedTarget=0;

// 历史记录按章节筛选只是减少误选，不代表能证明记录属于当前存档。界面始终显示
// 地点和保存时间，并要求两次确认；绝不因发现相同章节就自动返回或修改当前存档。
std::vector<size_t> CompatibleRecords(const RevisitNativeContext& context) {
    std::vector<size_t> compatible;
    for (size_t i=0;i<records.size();++i)
        if (records[i].point.chapter==context.chapter) compatible.push_back(i);
    return compatible;
}
void RefreshSession(const RevisitNativeStatus& status,const RevisitNativeContext& context) noexcept {
    // 明确读到另一合法章节后，旧活动会话立即结束；再次读回原章节时只能手动选择
    // 历史记录，不能悄然复活之前的活动出发点。换图中不完整的默认 context 不作为
    // 换档证据；同章的摘要变化也可能来自正常剧情、机关或开箱，不能据此误判读档。
    if (hasActive && RevisitContextAllowed(context) && activeRecord.point.chapter!=context.chapter) {
        // 换章即退出实验行程保护，不能让旧行程的宝箱/森林例外影响新章正常主线。
        SetExperimentalRevisitTripActive(false);
        hasActive=false;
        activeRecord={};
        preparedFirstDeparture=false;
        preparedToken=0;
        preparedTarget=0;
        selectedRecord=0;
    }
    if (!preparedToken || status.token!=preparedToken) return;
    if (status.phase==RevisitNativePhase::Dispatched) preparedFirstDeparture=false;
    if (status.phase==RevisitNativePhase::Arrived) {
        if (preparedTarget==kRevisitReturnTarget) {
            hasActive=false;
            SetExperimentalRevisitTripActive(false);
        }
        preparedFirstDeparture=false;
        preparedToken=0;
    } else if (status.phase==RevisitNativePhase::ArrivalUnconfirmed) {
        // 加载已被原生接受但观测未确认，不等于“从未离开”。保留本次最初记录，
        // 允许玩家在稳定地图中重新选择传送或返程，不能在超时后丢弃活动返程点。
        preparedFirstDeparture=false;
        preparedToken=0;
    } else if (status.phase==RevisitNativePhase::Rejected || status.phase==RevisitNativePhase::Expired) {
        // 派发失败不破坏此前已经开始的回访；首次出发未成功时也不强迫玩家使用
        // 失效的内存会话。文件仍保留，防止实际换图结果与界面观测存在时间差。
        if (preparedFirstDeparture) hasActive=false;
        preparedFirstDeparture=false;
        preparedToken=0;
    }
}
RevisitReturnStatus ReturnStatusLocked(const RevisitNativeContext& context) {
    RevisitReturnStatus result{};
    result.storageReady=storageReady;
    // 活动记录不会因步行离开旧地图被覆盖；四塔和旧校舍出口可以回到普通大地图。
    // RefreshSession 已在有效章节变化时结束旧会话；此处还按章节再次限制展示，
    // 历史候选始终需要玩家核对地点和时间，不将章节相同当作同一存档的证明。
    result.active=hasActive && activeRecord.point.chapter==context.chapter;
    if (result.active) {
        result.hasRecord=true; result.record=activeRecord; result.count=1;
    } else {
        const auto compatible=CompatibleRecords(context);
        result.count=compatible.size();
        if (result.count) {
            selectedRecord%=result.count;
            result.hasRecord=true; result.index=selectedRecord;
            result.record=records[compatible[selectedRecord]];
        }
    }
    return result;
}
bool TargetAllowedLocked(uint32_t target,const RevisitNativeContext& context) {
    if (!ready.load() || !RevisitContextAllowed(context)) return false;
    if (!RevisitNativeTargetAvailable(target,context)) return false;
    const auto state=ReturnStatusLocked(context);
    // 精确返程必须核对所选记录的场景；当前所在场景可能属于另一组传送前置剧情，
    // 不能拿当前源场景的 beforeScriptReturnBlocked 代替返程目标本身的校验。
    if (target==kRevisitReturnTarget)
        return state.hasRecord && RevisitNativeReturnPhaseAllowed(state.record.point,context);
    // 保留0.5.0中已经生成的第8章序章存档应急出口；终章不会错用柏斯返程。
    if (!revisit_policy::kUnrestricted && target==15 && context.chapter==8 && context.region==6 && !state.hasRecord) return true;
    // 原生层不知道历史记录是否存在，只能提供应急出口候选。协调层掌握会话后，
    // 对所有非应急的柏斯请求重新应用普通规则，避免已有返程点时绕过剧情禁用。
    if (target==15 && !RevisitNativeOrdinaryTargetAvailable(target,context)) return false;
    if (!RevisitDestinationListed(target) || !storageReady) return false;
    // 前期剧情若接管当前出发场景所对应的原生传送，日后直接坐标返程会跳过该流程，
    // 因而尚未开始回访时不得创建新行程；已有行程的普通目标仍由原生逐点规则决定。
    if (!revisit_policy::kUnrestricted && !state.active && context.beforeScriptReturnBlocked) return false;
    // 首次出发必须已有本帧验证过的真实站位，不能只因目标可用就让界面反复确认。
    // 此快照只是提前提示；实际持久化前仍由 ReadRevisitNativeReturnPoint 重新核对。
    // 已有活动行程或明确返程无需重新建立出发点，不因当前站位暂缺而丢失旧记录。
    if (!state.active && !context.returnPointReady) return false;
    // 第8/9章重启后位于旧地图时，先手动返回记录地点，避免覆盖最初出发点。
    // 早期主线也会正常进入这些场景，不能仅根据场景名要求不存在的回访记录。
    // 实验模式的旧版应急出口也先记录出发点，确保跨章节保护始终属于有锚点的行程。
    // 无记录时只让柏斯出口越过恢复优先；已有历史时仍须先核对返程，不覆盖原锚点。
    const bool experimentalEmergency = revisit_policy::kUnrestricted && target==15 &&
        context.chapter==8 && context.region==6 && !state.hasRecord;
    return state.active || !RevisitRecoveryRequired(context) || experimentalEmergency;
}
bool AuthorizeRevisit(uint32_t target,const RevisitNativeContext& context,uint64_t token) noexcept {
    try {
        std::lock_guard<std::mutex> lock(sessionMutex);
        return token && token==preparedToken && target==preparedTarget &&
            TargetAllowedLocked(target,context);
    } catch (...) { return false; }
}
}

bool RevisitReady() noexcept { return ready.load(); }
bool RevisitContextAllowed(const RevisitNativeContext& context) noexcept {
    return context.valid && ValidRevisitChapter(context.chapter) &&
        context.region>=1 && context.region<=9;
}
bool RevisitRecoveryRequired(const RevisitNativeContext& context) noexcept {
    return RevisitContextAllowed(context) && (context.chapter==8 || context.chapter==9) &&
        IsRevisitScene(context.scene);
}
RevisitReturnStatus ReadRevisitReturnStatus(const RevisitNativeContext& context) noexcept {
    try {
        const auto status=ReadRevisitNativeStatus();
        std::lock_guard<std::mutex> lock(sessionMutex);
        RefreshSession(status,context);
        return ReturnStatusLocked(context);
    } catch (...) { return {}; }
}
void CycleRevisitReturnRecord(const RevisitNativeContext& context) noexcept {
    try {
        const auto status=ReadRevisitNativeStatus();
        if (status.phase==RevisitNativePhase::Queued || status.phase==RevisitNativePhase::ClosingMap ||
            status.phase==RevisitNativePhase::Dispatched) return;
        std::lock_guard<std::mutex> lock(sessionMutex);
        RefreshSession(status,context);
        const auto state=ReturnStatusLocked(context);
        if (!state.active && state.count>1) selectedRecord=(selectedRecord+1)%state.count;
    } catch (...) {}
}
bool RevisitTargetAllowed(uint32_t target,const RevisitNativeContext& context) noexcept {
    try {
        const auto status=ReadRevisitNativeStatus();
        std::lock_guard<std::mutex> lock(sessionMutex);
        RefreshSession(status,context);
        return TargetAllowedLocked(target,context);
    } catch (...) { return false; }
}
bool QueueRevisitTravel(uint32_t target,uint64_t token,const RevisitNativeContext& expected) noexcept {
    try {
        if (!token || !expected.browsing || expected.busy) return false;
        RevisitReturnRecord anchor{};
        bool firstDeparture=false;
        {
            const auto status=ReadRevisitNativeStatus();
            std::lock_guard<std::mutex> lock(sessionMutex);
            RefreshSession(status,expected);
            if (status.phase==RevisitNativePhase::Queued || status.phase==RevisitNativePhase::ClosingMap ||
                status.phase==RevisitNativePhase::Dispatched || !TargetAllowedLocked(target,expected)) return false;
            const auto state=ReturnStatusLocked(expected);
            anchor=state.record;
            const bool legacyReturn=!revisit_policy::kUnrestricted && target==15 &&
                expected.chapter==8 && expected.region==6 && !state.hasRecord;
            firstDeparture=target!=kRevisitReturnTarget && !legacyReturn && !state.active;
        }
        if (firstDeparture) {
            anchor={};
            if (!ReadRevisitNativeReturnPoint(expected,anchor.point)) return false;
            anchor.createdUnixSeconds=static_cast<uint64_t>(std::time(nullptr));
            if (BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&anchor.ticket),sizeof(anchor.ticket),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0 || !anchor.ticket) return false;
            // 磁盘写入在渲染请求侧完成；游戏更新线程中的授权回调只读内存，不等待I/O。
            if (!WriteRevisitRecordFile(recordFolder,anchor)) {
                Log("Revisit: return point could not be persisted; departure blocked."); return false;
            }
        }
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            if (firstDeparture) {
                records.insert(records.begin(),anchor);
                selectedRecord=0; activeRecord=anchor; hasActive=true;
            }
            preparedToken=token; preparedTarget=target; preparedFirstDeparture=firstDeparture;
        }
        const bool submitted=target==kRevisitReturnTarget ?
            QueueRevisitNativeReturn(anchor.point,token,expected) : QueueRevisitNativeTravel(target,token,expected);
        if (!submitted) {
            std::lock_guard<std::mutex> lock(sessionMutex);
            if (preparedToken==token) {
                if (preparedFirstDeparture) hasActive=false;
                preparedToken=0; preparedFirstDeparture=false;
            }
        }
        return submitted;
    } catch (...) { return false; }
}
void InstallRevisit(uintptr_t gameBase,const std::wstring& folder) noexcept {
    try {
        // 实验保护仅由真正执行的本次传送开启，历史文件和相同章节不能证明存档身份。
        SetExperimentalRevisitTripActive(false);
        if (!ReadExplorationStatus().travelAvailable || !InstallRevisitEventGuard(gameBase)) {
            Log("Revisit unavailable: native browse or treasure event guard validation failed."); return;
        }
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            recordFolder=folder;
            storageReady=ReadRevisitRecordHistory(folder,records);
            hasActive=false; preparedToken=0;
        }
        SetRevisitEventGuardActive(true);
        // 迷途之森没有自然出口，只有专用谜题保护完整安装后才允许新的入场。
        // 可选保护安装失败只禁用该入口，不影响原有回访或离开旧地图的精确返程。
        InstallForestRevisitGuard(gameBase);
        SetRevisitNativeAuthorize(&AuthorizeRevisit);
        ready.store(true);
        InstallRevisitNative(gameBase);
        Log(storageReady ? "Revisit initialized: persistent departure points, manual recovery, chapter-aware destinations." :
            "Revisit initialized: return history unavailable; new departures disabled.");
    } catch (...) { ready.store(false); Log("Revisit initialization failed."); }
}
}
