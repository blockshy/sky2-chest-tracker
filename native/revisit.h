// 回访模块总入口：先安装开箱剧情保护，再允许界面提交原生传送请求。
#pragma once
#include "revisit_native.h"
#include "revisit_catalog.h"
#include "revisit_journal.h"
namespace tracker {
void InstallRevisit(uintptr_t gameBase, const std::wstring& folder) noexcept;
bool RevisitReady() noexcept;
// 序章至终章均可申请；自由行动、稳定地图以及具体目的地的原生规则由游戏线程复核。
bool RevisitContextAllowed(const RevisitNativeContext& context) noexcept;
// 第8/9章的旧场景保留原有手动恢复策略；早期正常剧情场景不因此禁止记录新出发点。
// 返回 true 只表示场景要求恢复检查；活动回访内部跳转仍可保留同一出发点继续传送。
bool RevisitRecoveryRequired(const RevisitNativeContext& context) noexcept;
struct RevisitReturnStatus {
    bool storageReady = false;
    bool active = false;
    bool hasRecord = false;
    size_t index = 0;
    size_t count = 0;
    RevisitReturnRecord record{};
};
// 更新会话只使用原生线程发布的值快照。读取其他存档不会自动选择/执行返程。
RevisitReturnStatus ReadRevisitReturnStatus(const RevisitNativeContext& context) noexcept;
void CycleRevisitReturnRecord(const RevisitNativeContext& context) noexcept;
bool RevisitTargetAllowed(uint32_t target, const RevisitNativeContext& context) noexcept;
// 两次确认后才调用：第一次出发必须先把出发坐标可靠写入Mod记录，再排入原生请求。
// 回访地点之间跳转保留最初记录，不会把新地点当成新的出发点。
bool QueueRevisitTravel(uint32_t target, uint64_t token,
                        const RevisitNativeContext& expected) noexcept;
}
