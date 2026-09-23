// 旧地图回访的原生菜单适配。接口只传递值对象，不向界面暴露或缓存游戏对象指针。
#pragma once
#include <cstdint>
#include <array>
#include "revisit_return_point.h"

namespace tracker {

struct RevisitNativeContext {
    bool available = false;
    bool valid = false;
    bool browsing = false;
    bool busy = true;
    bool prologueCompleted = false;
    // 仅表示本次原生更新已成功捕获并核对当前返程坐标，不是恒久传送许可。
    // 场景/角色读取失败或换图繁忙时重新置false；首次出发可据此明确提示，
    // 已有活动会话和最终原生授权仍按各自实时校验执行，不依赖旧快照补许可。
    bool returnPointReady = false;
    // 只在真实读取到原生换图繁忙位时为真；无场景/标题画面的默认 busy 不算加载证据。
    bool transitionActive = false;
    uint32_t chapter = 0;
    uint32_t region = 0;
    uint32_t returnSpot = 15;
    char scene[32]{};
    // 旗标摘要只用于让五秒内待处理请求拒绝换档/进度变化，不作为存档永久身份。
    uint64_t progressSignature = 0;
    // 仅在当前进程中识别打开的原生菜单；不可持久化，也不是可解引用的游戏地址。
    uint64_t browseIdentity = 0;
    // 已审核目的地的剧情准入位图；只由游戏线程按当前旗标发布，界面不得补位。
    // 独立实验构建忽略此位图作许可，但仍保持真实捕获值供诊断和默认策略使用。
    uint32_t destinationMask = 0;
    // 保留原始地形数据用于诊断。region 是经场景/地点表核对的回访归属，
    // 子地形 nativeRegion 可以为0；荣耀号终章 sceneRegion 可以为7。
    uint32_t nativeRegion = 0, sceneRegion = 0, place = 0, mapPlace = 0;
    // 0未有当前规则，1可用，其余值为未登记/剧情禁用/分组禁用/内部入口/前置剧情/不一致。
    // 只由已执行完原生MapJumpState的观察路径产生，不能依据显示visible位放开。
    // 实验构建按公开目录开放目标；本数组继续记录原生结果，不伪造登记或灰态。
    std::array<uint8_t,1001> nativeRuleStatus{};
    // 独立于原生登记/灰态的前置脚本保护。值为1表示直接提交该目标会跳过
    // 当前活跃剧情分支；每次场景捕获重新从旗标推导，不继承上一存档的结果。
    // 索引0仅保存未匹配目标的默认分支，不是允许传送到原生目的地0。
    std::array<uint8_t,1001> beforeScriptBlocked{};
    // 新开放的0..7章中，当前场景若对应任一有前置剧情的原生目标，不能把它
    // 记录为新出发点。没有对应Spot的合法地形仍须检查默认前置分支及跨地区
    // 别名；仅有其他特定目标的正向分支时，不把整条道路连带禁用。
    // 该值只代表当前场景，不代表已选返程记录；具体返程另调用下面的独立API。
    // 8/9章保留既有行为，避免把正在晚期旧图中的玩家锁在无法自然离开的场景。
    bool beforeScriptReturnBlocked = false;
    // 只读当前20064/20067完成条件；保护模块是否就绪在最终提交时单独复核，
    // 不能把界面快照中的可用状态当作允许进入迷途之森的永久授权。
    bool forestStoryComplete = false;
};

enum class RevisitNativePhase : uint32_t {
    Idle, Queued, ClosingMap, Dispatched, Rejected, Expired, Arrived, ArrivalUnconfirmed
};
struct RevisitNativeStatus {
    uint64_t token = 0;
    uint32_t target = 0;
    RevisitNativePhase phase = RevisitNativePhase::Idle;
};

// 准入回调在原生地图更新线程中、提交退出请求之前执行。调用者应只检查自身已准备
// 完成的保护状态；不要在这里弹窗、等待输入或执行游戏脚本。false 会立即取消请求。
using RevisitNativeAuthorize = bool (*)(uint32_t target, const RevisitNativeContext& context,
                                        uint64_t token) noexcept;
void SetRevisitNativeAuthorize(RevisitNativeAuthorize callback) noexcept;
void InstallRevisitNative(uintptr_t gameBase) noexcept;
RevisitNativeContext ReadRevisitNativeContext() noexcept;
RevisitNativeStatus ReadRevisitNativeStatus() noexcept;
bool RevisitNativeTargetAvailable(uint32_t target, const RevisitNativeContext& context) noexcept;
bool RevisitNativeOrdinaryTargetAvailable(uint32_t target, const RevisitNativeContext& context) noexcept;
// 默认策略按已捕获的前置规则和静态目录审核具体返程场景；实验策略只保留
// 记录格式与当前章节一致性。两者均不读取游戏地址或执行脚本，最终加载仍须
// 另行核对真实地点、坐标和菜单身份。不能误用当前场景的首次出发预检结果。
bool RevisitNativeReturnPhaseAllowed(const RevisitReturnPoint& point,
                                    const RevisitNativeContext& context) noexcept;
const char* RevisitNativeTargetReason(uint32_t target, const RevisitNativeContext& context) noexcept;
// 由现有显示构建桥在已确认原生规则脚本结束后调用，只观察，不重新执行任何脚本。
void ObserveRevisitNativeRules(uintptr_t manager) noexcept;
// 返回原生游戏线程本帧发布的站位副本；读取失败不会交出上一张地图的缓存坐标。
// expected 必须仍是同一次打开的稳定地图；写入持久化记录之前应调用此接口。
bool ReadRevisitNativeReturnPoint(const RevisitNativeContext& expected,
                                  RevisitReturnPoint& output) noexcept;
bool QueueRevisitNativeTravel(uint32_t target, uint64_t token,
                              const RevisitNativeContext& expected) noexcept;
// 返程不查找“最近传送点”，而是将校验后的场景、XYZ、朝向交给原生完整换图 API。
// 上层必须先让玩家明确确认所选本机记录；本接口不把剧情摘要当作唯一存档身份。
bool QueueRevisitNativeReturn(const RevisitReturnPoint& point, uint64_t token,
                              const RevisitNativeContext& expected) noexcept;
void CancelRevisitNativeTravel() noexcept;

// 在现有 Sky2BeforeMapRefresh 的最前方调用。true 表示本帧已提交/拒绝请求，应跳过
// 旧菜单输入；原生 Menu::Update、退出动画、对象销毁及换图消费者继续自然运行。
bool BeforeRevisitNativeBrowse(uintptr_t menu) noexcept;

}
