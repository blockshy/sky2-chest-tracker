// 迷途之森没有原生传送点，入场前必须确认其旧谜题触发保护已安装。
// 保护只作用于完成剧情后的该场景，不改变剧情旗标或玩家的游戏存档。
#pragma once
#include <cstdint>

namespace tracker {
bool InstallForestRevisitGuard(uintptr_t gameBase) noexcept;
bool ForestRevisitGuardReady() noexcept;
}
