// 面板传送确认的纯状态：界面隐藏/切页/失焦和原生上下文变化都会撤销许可。
// 不读取游戏内存，不提交传送；调用者仍需在第二次确认后走 QueueRevisitTravel。
#pragma once
#include "revisit_logic.h"
#include "revisit_native.h"
#include <cstring>

namespace tracker {
class PanelTravelConfirmation {
    RevisitConfirmation confirmation_;
    RevisitNativeContext expected_{};
public:
    void Cancel() noexcept { confirmation_.Cancel(); }
    // 快照只用于淘汰陈旧确认；真正的目标、站位和原生线程权限仍由业务层复核。
    void Observe(bool active, bool ready, const RevisitNativeContext& context) noexcept {
        if (!active || !ready || context.chapter != expected_.chapter ||
            context.browseIdentity != expected_.browseIdentity ||
            context.progressSignature != expected_.progressSignature ||
            std::strcmp(context.scene, expected_.scene) != 0) Cancel();
    }
    bool Press(uint32_t target, uint64_t now, bool active, bool ready,
               const RevisitNativeContext& context) noexcept {
        Observe(active, ready, context);
        if (!active || !ready) return false;
        if (confirmation_.Press(target, now)) return true;
        expected_ = context;
        return false;
    }
    bool Armed(uint32_t target, uint64_t now) const noexcept { return confirmation_.Armed(target, now); }
};
}
