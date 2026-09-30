// Hub 的业务软停用门闩。代码、虚表和 trampoline 始终驻留；只在安全收尾完成后
// 旁路附加效果。原 ASI / Standalone 没有生命周期调用，门闩始终保持默认开启。
#pragma once
#include <atomic>
namespace tracker {
inline std::atomic<bool> g_hostedEffectsEnabled{true};
inline bool HostedEffectsEnabled() noexcept {
    return g_hostedEffectsEnabled.load(std::memory_order_acquire);
}
}
