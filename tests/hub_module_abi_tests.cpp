// 加载真实模块二进制验证导出与 ABI 拒绝行为，不提供游戏上下文或安装任何挂钩。
// DllMain/Query 必须无业务副作用；未提供完整宿主函数表时 initialize 必须拒绝。
#include <Windows.h>
#include "sky2_hub.h"
#include <cstring>
#include <cstddef>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    HMODULE module = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(module);
    const auto query = reinterpret_cast<Sky2ModuleQuery>(GetProcAddress(module, "Sky2Module_Query"));
    assert(query);
    // 模块不能再导出旧 ASI 或 XInput 入口，否则会被错误的装载器当作独立入口启动。
    assert(GetProcAddress(module, "InitializeASI") == nullptr);
    assert(GetProcAddress(module, "XInputGetState") == nullptr);
    Sky2ModuleApi api{};
    api.size = sizeof(api);
    assert(!query(SKY2_HUB_ABI + 1, &api));
    assert(!query(SKY2_HUB_ABI, nullptr));
    api.size = 0;
    assert(!query(SKY2_HUB_ABI, &api));
    api.size = sizeof(api);
    assert(query(SKY2_HUB_ABI, &api));
    assert(api.abi == SKY2_HUB_ABI);
    assert(std::strcmp(api.id, "chest") == 0);
    assert(std::strcmp(api.legacy_asi, "Sky2ChestTracker.asi") == 0);
    assert(std::strcmp(api.data_folder, "Sky2ChestTracker") == 0);
    assert(api.initialize && api.tick_ui && api.draw_page && api.draw_overlay && api.visibility_changed);
    assert(api.request_enabled && api.activity_state && api.activity_message && api.draw_header);
    assert(!api.request_enabled(0) && api.activity_state() == 0);
    // 模拟只分配原 ABI 前缀的旧宿主；其后字节是哨兵，扩展查询不能写出容量。
    constexpr size_t legacySize = offsetof(Sky2ModuleApi, request_enabled);
    alignas(Sky2ModuleApi) std::array<unsigned char, sizeof(Sky2ModuleApi)> legacy{};
    legacy.fill(0xA5);
    reinterpret_cast<Sky2ModuleApi*>(legacy.data())->size = static_cast<uint32_t>(legacySize);
    assert(query(SKY2_HUB_ABI, reinterpret_cast<Sky2ModuleApi*>(legacy.data())));
    for (size_t i = legacySize; i < legacy.size(); ++i) assert(legacy[i] == 0xA5);
    // 0.4.x 宿主具有生命周期尾部但没有固定页头槽位，同样不能被新版导出越界写入。
    constexpr size_t lifecycleSize = offsetof(Sky2ModuleApi, draw_header);
    legacy.fill(0xA5);
    reinterpret_cast<Sky2ModuleApi*>(legacy.data())->size = static_cast<uint32_t>(lifecycleSize);
    assert(query(SKY2_HUB_ABI, reinterpret_cast<Sky2ModuleApi*>(legacy.data())));
    for (size_t i = lifecycleSize; i < legacy.size(); ++i) assert(legacy[i] == 0xA5);
    assert(!api.initialize(nullptr));
    Sky2HostApi incomplete{};
    incomplete.size = sizeof(incomplete); incomplete.abi = SKY2_HUB_ABI;
    assert(!api.initialize(&incomplete));
    // 入口拒绝不完整宿主后仍可查询；没有形成半初始化或一次性锁失效。
    api.size = sizeof(api);
    assert(query(SKY2_HUB_ABI, &api));
    assert(FreeLibrary(module));
    std::cout << "Module query and invalid host checks passed.\n";
}
