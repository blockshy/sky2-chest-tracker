// 旧地图回访期间的特殊宝箱保护：只改开箱脚本的临时参数，不改资源或存档。
#pragma once
#include <cstdint>

namespace tracker {
// 完整游戏 EXE 校验和 MinHook 初始化成功后安装。入口字节不符或挂钩失败返回 false，
// 回访调用方必须据此停止提供入口；不能在缺少特殊宝箱保护时继续允许传送。
bool InstallRevisitEventGuard(uintptr_t gameBase) noexcept;

// 安装成功后默认允许内部判定，调用方可显式停用。真正生效仍要求原生回调当时处于
// 已审查的章节、地区、剧情完成集合与当前场景一致；序章支持第八章/终章，荣耀号
// 只支持已结束舰内主线的终章。不能依赖渲染线程延迟更新的“已到达”。
// 这样重启后直接载入回访自动存档，也能在首个开箱回调前获得同样的保护。
void SetRevisitEventGuardActive(bool active) noexcept;

// 全传送的跨章节保护仅属于玩家明确确认的本次传送行程，不能根据存档历史或场景名
// 自动开启。最终换图消费者须在全部校验通过后、调用原生加载器前设置；精确返程
// 应先关闭，防止返回正常主线森林时抑制本来应该执行的谜题初始化。安装、换章及
// 返程确认也应清理。此状态不持久化，标准构建始终保持 false。开启必须显式提供
// 合法的未标记章节 0～9；省略章节只能用于关闭或查询记账状态，不能开启保护。
// 保护回调必须传入刚读取的当前章节：发现另一合法章节时立即原子清除旧行程，
// 无需等待面板刷新，也不会在之后重新读回原章节时自动恢复。
void SetExperimentalRevisitTripActive(bool active, uint32_t chapter = UINT32_MAX) noexcept;
bool ExperimentalRevisitTripActive(uint32_t chapter = UINT32_MAX) noexcept;
}
