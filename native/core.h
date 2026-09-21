// 纯状态逻辑：与游戏地址、绘制 API 分离，方便验证当前周目与继承记录不会混用。
#pragma once
#include <cstddef>
#include <cstdint>

namespace tracker {
enum class Mode : unsigned { Current = 0, Inherited = 1 };
inline constexpr uint32_t kClosedIcon = 0x198;
inline constexpr uint32_t kOpenedIcon = 0x199;

// 标志 0 在游戏中表示「未指定」，不能作为有效宝箱状态读出。
inline bool Flag(const uint8_t* bits, size_t size, uint32_t id) noexcept {
    return id != 0 && id / 8 < size && (bits[id / 8] & (1u << (id % 8))) != 0;
}

// 继承视图始终包含本次已开；本周目视图只服从当前状态，读旧档时允许回退。
inline uint32_t Icon(Mode mode, bool current, bool inherited) noexcept {
    return (current || (mode == Mode::Inherited && inherited)) ? kOpenedIcon : kClosedIcon;
}
}
