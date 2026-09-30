// 公共宿主专用入口：查询阶段无副作用；业务在宿主初始化线程中同步启动。
// 此目标不包含 xinput_proxy、旧 ASI 入口或私有 ImGui/MinHook 实现。
#include "sky2_hub.h"
#include "tracker.h"
#include "hub_panel.h"
#include <cstddef>
#include <algorithm>
#include <cstring>
#include <mutex>

const Sky2HostApi* Sky2Hub_Host = nullptr;

namespace {
bool moduleInitialized = false;
int32_t SKY2_CALL Initialize(const Sky2HostApi* host) noexcept {
    // 卡片等布局函数是 ABI v1 的可选尾部。只要求原函数表完整，新布局通过
    // sky2_ui.hpp 在旧宿主上回退为线性控件，不能因 sizeof 增长拒绝旧宿主。
    constexpr size_t requiredUiSize = offsetof(Sky2UiApi, is_any_item_active) + sizeof(Sky2UiApi::is_any_item_active);
    // 先校验全部实际使用的函数，避免旧宿主的短结构或空函数造成跨 DLL 崩溃。
    if (!host || host->size < sizeof(Sky2HostApi) || host->abi != SKY2_HUB_ABI ||
        !host->owner || !host->module_handle || !host->game_base || !host->log ||
        !host->ui || host->ui->size < requiredUiSize || !host->data_directory ||
        !host->register_action || !host->open_page || !host->language ||
        !host->create_hook || !host->enable_hook || !host->disable_hook || !host->remove_hook)
        return 0;
    const auto* ui = host->ui;
    if (!ui->text || !ui->text_wrapped || !ui->text_color || !ui->separator ||
        !ui->same_line || !ui->spacing || !ui->button || !ui->checkbox ||
        !ui->selectable || !ui->begin_disabled || !ui->end_disabled ||
        !ui->begin_child || !ui->end_child || !ui->input_text || !ui->rect ||
        !ui->draw_text || !ui->measure_text) return 0;
    static std::once_flag once;
    static bool initialized = false;
    try {
        std::call_once(once, [host] {
            Sky2Hub_Host = host;
            tracker::g_module = static_cast<HMODULE>(host->module_handle);
            // 动作注册先于业务挂钩；失败不会留下缺少控制入口的地图修改。
            initialized = tracker::RegisterHubActions() && tracker::InitializeHostedRuntime();
        });
    } catch (...) { return 0; }
    // 幂等调用仅限最初宿主身份；已有挂钩不能被第二份函数表重新解释或接管。
    moduleInitialized = initialized;
    return initialized && Sky2Hub_Host == host ? 1 : 0;
}

// 所有 C ABI 回调都在模块边界捕获 C++ 异常；这并非原生崩溃隔离机制。
void SKY2_CALL Tick(const Sky2Frame* frame) noexcept {
    try { tracker::HubTick(frame); } catch (...) { tracker::HubVisibilityChanged(0); }
}
void SKY2_CALL Page(const Sky2Frame* frame) noexcept {
    try { tracker::HubDrawPage(frame); } catch (...) { tracker::HubVisibilityChanged(0); }
}
void SKY2_CALL Header(const Sky2Frame* frame) noexcept {
    try { tracker::HubDrawHeader(frame); } catch (...) { tracker::HubVisibilityChanged(0); }
}
void SKY2_CALL Overlay(const Sky2Frame* frame) noexcept {
    try { tracker::HubDrawOverlay(frame); } catch (...) { tracker::HubVisibilityChanged(0); }
}
void SKY2_CALL Visibility(int32_t active) noexcept { tracker::HubVisibilityChanged(active); }
int32_t SKY2_CALL RequestEnabled(int32_t enabled) noexcept {
    return moduleInitialized ? tracker::HubRequestEnabled(enabled) : 0;
}
int32_t SKY2_CALL ActivityState() noexcept { return moduleInitialized ? tracker::HubActivityState() : 0; }
const char* SKY2_CALL ActivityMessage() noexcept { return tracker::HubActivityMessage(); }
}

extern "C" __declspec(dllexport) int32_t SKY2_CALL Sky2Module_Query(
    uint32_t requestedAbi, Sky2ModuleApi* output) noexcept {
    // 生命周期是 v1 的可选尾部。旧宿主只提供旧前缀时仍可加载；按调用者容量拷贝，
    // 不能用新版整结构赋值越过旧宿主的缓冲区，也不能因新增能力拒绝原 v1 前缀。
    constexpr size_t required = offsetof(Sky2ModuleApi, request_enabled);
    if (requestedAbi != SKY2_HUB_ABI || !output || output->size < required) return 0;
    const size_t capacity = output->size;
    const Sky2ModuleApi api{sizeof(Sky2ModuleApi), SKY2_HUB_ABI, "chest", "宝箱追踪", "0.6.0-hub.1",
        "Sky2ChestTracker.asi", "Sky2ChestTracker", &Initialize, &Tick, &Page, &Overlay, &Visibility,
        &RequestEnabled, &ActivityState, &ActivityMessage, &Header};
    std::memcpy(output, &api, std::min(capacity, sizeof(api)));
    return 1;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
