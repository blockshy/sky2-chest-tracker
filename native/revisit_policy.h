// 正式构建默认提供全传送；开发者可在编译时选择保留剧情限制的对照策略。
// 策略不能通过运行时快捷键切换；两套自动测试均显式指定策略，不依赖此默认值。
// 本开关仅允许绕过目的地的剧情门槛，不授予任意地址/场景/线程操作权限。
#pragma once

#ifndef SKY2_UNRESTRICTED_TRAVEL
#define SKY2_UNRESTRICTED_TRAVEL 1
#endif

namespace tracker::revisit_policy {
static_assert(SKY2_UNRESTRICTED_TRAVEL == 0 || SKY2_UNRESTRICTED_TRAVEL == 1,
              "SKY2_UNRESTRICTED_TRAVEL must be 0 or 1");
inline constexpr bool kUnrestricted = SKY2_UNRESTRICTED_TRAVEL != 0;
}
