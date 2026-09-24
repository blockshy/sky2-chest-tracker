// 原生插件模块之间共享的最小接口，避免绘制层直接操作游戏状态。
#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include "core.h"
#include "progress.h"
#include "log.h"

namespace tracker {
extern HMODULE g_module;
extern std::atomic<bool> g_enabled;
extern std::atomic<bool> g_panel;
extern std::atomic<Mode> g_mode;

struct Counts {
    bool valid = false;
    unsigned current = 0;
    unsigned inherited = 0;
    unsigned map_current = 0;
    unsigned map_inherited = 0;
    unsigned map_total = 0;
    std::string map;
    // 全地图和总数来自同一次快照，避免开箱瞬间两处显示不一致。
    std::vector<MapProgress> maps;
};

// 排入工作线程；不在 DLL 加载锁内等待或创建图形设备。
// pluginMode 仅选择数据目录与诊断标识，不改变游戏功能；两种入口共用实现与重复加载保护。
void Start(bool pluginMode = false) noexcept;
Counts ReadCounts();
bool InstallOverlay();
}
