cmake_minimum_required(VERSION 3.20)
project(Sky2ChestTracker VERSION 0.6.0 LANGUAGES C CXX)

# 关闭 DLL 构建时，仅运行纯逻辑测试；CI 不需要下载或持有游戏资源。
option(SKY2_BUILD_MOD "构建 Windows 游戏插件" ON)
# 正式 DLL 默认提供全传送。剧情限制策略保留作开发对照；测试分别显式指定 0/1，
# 不依赖 DLL 选项或头文件默认值，避免切换发布默认后丢失原有保守规则回归。
option(SKY2_UNRESTRICTED_TRAVEL "全传送：不以剧情条件限制清单目的地" ON)
set(SKY2_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "固定提交的第三方源码目录")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(CTest)

# 显式登记头文件，防止中文 MSVC 的包含文件输出解析异常导致复用陈旧对象。
# 发布脚本另执行 clean-first；上游依赖固定到已记录的提交。
set(TRACKER_LOCAL_HEADERS
    native/core.h native/progress.h native/tracker.h native/log.h native/runtime_files.h native/controller_logic.h native/input_bridge.h native/ui_scale.h
    native/localization.h native/localized_names.h native/game_language.h native/ui_text.h
    native/exploration.h native/exploration_logic.h native/travel_refresh.h native/travel_menu.h
    native/revisit.h native/revisit_catalog.h native/revisit_logic.h native/revisit_native.h native/revisit_event_guard.h
    native/revisit_event_guard_rules.h native/revisit_return_point.h native/revisit_journal.h
    native/revisit_forest.h native/revisit_forest_rules.h native/revisit_phase_rules.h native/revisit_policy.h)
list(TRANSFORM TRACKER_LOCAL_HEADERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(SKY2_HAVE_CATALOG OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/chest_catalog.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/map_catalog.h")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/travel_catalog.h")
        list(APPEND TRACKER_LOCAL_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/travel_catalog.h")
    endif()
    set(SKY2_HAVE_CATALOG ON)
    list(APPEND TRACKER_LOCAL_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/chest_catalog.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/map_catalog.h")
    # 八语表属于本机游戏派生资源；明确追踪依赖，语言名称更新必须触发重新编译。
    foreach(catalog map_localization.h travel_localization.h)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/${catalog}")
            list(APPEND TRACKER_LOCAL_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/${catalog}")
        endif()
    endforeach()
endif()
set_source_files_properties(
    native/tracker.cpp native/runtime_files_win.cpp native/overlay.cpp native/game_language.cpp native/input_bridge.cpp native/xinput_proxy.cpp native/plugin_entry.cpp native/exploration.cpp
    native/revisit.cpp native/revisit_native.cpp native/revisit_event_guard.cpp native/revisit_forest.cpp
    native/revisit_journal_win.cpp tests/revisit_journal_io_tests.cpp tests/revisit_tests.cpp tests/revisit_catalog_tests.cpp
    tests/revisit_event_guard_tests.cpp tests/revisit_event_guard_abi_tests.cpp tests/revisit_native_abi_tests.cpp
    tests/revisit_forest_tests.cpp tests/revisit_forest_abi_tests.cpp
    tests/core_tests.cpp tests/runtime_files_tests.cpp tests/runtime_files_io_tests.cpp tests/controller_tests.cpp tests/ui_scale_tests.cpp tests/exploration_tests.cpp tests/travel_refresh_tests.cpp
    tests/language_tests.cpp tests/localized_names_tests.cpp tests/ui_text_tests.cpp
    PROPERTIES OBJECT_DEPENDS "${TRACKER_LOCAL_HEADERS}")

if(SKY2_BUILD_MOD)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "DLL requires Windows x64 and MSVC. For portable tests use -DSKY2_BUILD_MOD=OFF.")
    endif()
    # 原生内部调用的寄存器保留约定超出标准 x64 ABI，使用带展开信息的 MASM 跳板。
    enable_language(ASM_MASM)
    if(NOT SKY2_HAVE_CATALOG OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/travel_catalog.h")
        message(FATAL_ERROR "Missing local game catalog. Run tools/Build-Mod.ps1 -GamePath <game directory>.")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/map_localization.h" OR
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/travel_localization.h")
        message(FATAL_ERROR "Missing native-language catalogs. Regenerate map and travel catalogs from your game.")
    endif()
    if(NOT EXISTS "${SKY2_DEPS_DIR}/minhook/CMakeLists.txt" OR NOT EXISTS "${SKY2_DEPS_DIR}/imgui/imgui.cpp")
        message(FATAL_ERROR "Missing dependencies. Run tools/Setup-Dependencies.ps1.")
    endif()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${SKY2_DEPS_DIR}/minhook" "minhook")

    # 第三方库在开发阶段编译；玩家运行时不下载任何依赖。
    add_library(tracker_imgui STATIC
        "${SKY2_DEPS_DIR}/imgui/imgui.cpp" "${SKY2_DEPS_DIR}/imgui/imgui_draw.cpp"
        "${SKY2_DEPS_DIR}/imgui/imgui_tables.cpp" "${SKY2_DEPS_DIR}/imgui/imgui_widgets.cpp"
        "${SKY2_DEPS_DIR}/imgui/backends/imgui_impl_dx11.cpp"
        "${SKY2_DEPS_DIR}/imgui/backends/imgui_impl_win32.cpp")
    target_include_directories(tracker_imgui PUBLIC "${SKY2_DEPS_DIR}/imgui" "${SKY2_DEPS_DIR}/imgui/backends")
    # 两种分发共用完全相同的功能对象，只有加载入口不同；避免独立维护两套游戏逻辑。
    # 独立版仍由 XInput 代理启动；插件版由 UAL 的 InitializeASI 启动。
    add_library(tracker_runtime OBJECT
        native/tracker.cpp native/runtime_files_win.cpp native/overlay.cpp native/game_language.cpp native/input_bridge.cpp
        native/exploration.cpp native/exploration_shims.asm
        native/revisit.cpp native/revisit_native.cpp native/revisit_native_shims.asm
        native/revisit_event_guard.cpp native/revisit_event_guard.asm
        native/revisit_forest.cpp native/revisit_forest.asm
        native/revisit_journal.cpp native/revisit_journal_win.cpp)
    target_include_directories(tracker_runtime PRIVATE native data/generated)
    target_compile_definitions(tracker_runtime PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(tracker_runtime PRIVATE SKY2_UNRESTRICTED_TRAVEL=$<BOOL:${SKY2_UNRESTRICTED_TRAVEL}>)
    target_compile_options(tracker_runtime PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
    target_link_libraries(tracker_runtime PRIVATE minhook tracker_imgui)
    add_library(Sky2ChestTracker SHARED native/xinput_proxy.cpp native/xinput_proxy.def $<TARGET_OBJECTS:tracker_runtime>)
    add_library(Sky2ChestTrackerAsi SHARED native/plugin_entry.cpp $<TARGET_OBJECTS:tracker_runtime>)
    foreach(target Sky2ChestTracker Sky2ChestTrackerAsi)
        target_include_directories(${target} PRIVATE native data/generated)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
        target_link_libraries(${target} PRIVATE minhook tracker_imgui d3d11 dxgi d3dcompiler bcrypt)
    endforeach()
    set_target_properties(Sky2ChestTracker PROPERTIES OUTPUT_NAME xinput1_4 PREFIX "")
    set_target_properties(Sky2ChestTrackerAsi PROPERTIES OUTPUT_NAME Sky2ChestTracker PREFIX "" SUFFIX ".asi")
endif()

if(BUILD_TESTING)
    if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 隔离宿主和诊断插件不进入玩家正式包；真实 UAL 由独立集成脚本按固定版本验证。
        add_subdirectory(tests/asi)
    endif()
    # 有本机目录时额外检查 59 地图、566 箱；无目录时仍验证位标志与跨周目图标语义。
    add_executable(tracker_core_tests tests/core_tests.cpp)
    target_include_directories(tracker_core_tests PRIVATE native)
    if(SKY2_HAVE_CATALOG)
        target_include_directories(tracker_core_tests PRIVATE data/generated)
        target_compile_definitions(tracker_core_tests PRIVATE SKY2_HAVE_CATALOG=1)
    endif()
    add_executable(tracker_controller_tests tests/controller_tests.cpp)
    target_include_directories(tracker_controller_tests PRIVATE native)
    add_executable(tracker_ui_scale_tests tests/ui_scale_tests.cpp)
    target_include_directories(tracker_ui_scale_tests PRIVATE native)
    add_executable(tracker_ui_text_tests tests/ui_text_tests.cpp)
    target_include_directories(tracker_ui_text_tests PRIVATE native)
    add_executable(tracker_localized_names_tests tests/localized_names_tests.cpp)
    target_include_directories(tracker_localized_names_tests PRIVATE native)
    if(SKY2_HAVE_CATALOG)
        target_include_directories(tracker_localized_names_tests PRIVATE data/generated)
    endif()
    # 原生枚举映射可跨平台验证；Windows 另链接只读检测层并用合成内存验证失效指针。
    add_executable(tracker_language_tests tests/language_tests.cpp)
    target_include_directories(tracker_language_tests PRIVATE native)
    if(WIN32)
        target_sources(tracker_language_tests PRIVATE native/game_language.cpp)
        target_compile_definitions(tracker_language_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    add_executable(tracker_runtime_files_tests tests/runtime_files_tests.cpp)
    target_include_directories(tracker_runtime_files_tests PRIVATE native)
    add_executable(tracker_exploration_tests tests/exploration_tests.cpp)
    target_include_directories(tracker_exploration_tests PRIVATE native)
    add_executable(tracker_travel_refresh_tests tests/travel_refresh_tests.cpp)
    target_include_directories(tracker_travel_refresh_tests PRIVATE native)
    add_executable(tracker_revisit_tests tests/revisit_tests.cpp)
    target_include_directories(tracker_revisit_tests PRIVATE native)
    add_executable(tracker_revisit_catalog_tests tests/revisit_catalog_tests.cpp)
    target_include_directories(tracker_revisit_catalog_tests PRIVATE native)
    if(SKY2_HAVE_CATALOG)
        target_include_directories(tracker_revisit_catalog_tests PRIVATE data/generated)
    endif()
    add_executable(tracker_revisit_event_guard_tests tests/revisit_event_guard_tests.cpp)
    target_include_directories(tracker_revisit_event_guard_tests PRIVATE native)
    add_executable(tracker_revisit_forest_tests tests/revisit_forest_tests.cpp)
    target_include_directories(tracker_revisit_forest_tests PRIVATE native)
    add_executable(tracker_revisit_journal_tests tests/revisit_journal_tests.cpp native/revisit_journal.cpp)
    target_include_directories(tracker_revisit_journal_tests PRIVATE native)
    set_source_files_properties(tests/revisit_journal_tests.cpp native/revisit_journal.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/native/revisit_journal.h;${CMAKE_CURRENT_SOURCE_DIR}/native/revisit_return_point.h")
    foreach(target tracker_core_tests tracker_controller_tests tracker_ui_scale_tests tracker_ui_text_tests tracker_localized_names_tests tracker_language_tests tracker_runtime_files_tests tracker_exploration_tests tracker_travel_refresh_tests tracker_revisit_tests tracker_revisit_catalog_tests tracker_revisit_event_guard_tests tracker_revisit_forest_tests tracker_revisit_journal_tests)
        target_compile_definitions(${target} PRIVATE SKY2_UNRESTRICTED_TRAVEL=0)
        if(MSVC)
            target_compile_options(${target} PRIVATE /utf-8 /W4)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME ${target} COMMAND ${target})
    endforeach()
    if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 文件生命周期与协调授权的验证不连接游戏，也不需要任何真实游戏资源。
        add_executable(tracker_revisit_journal_io_tests tests/revisit_journal_io_tests.cpp
            native/revisit_journal.cpp native/revisit_journal_win.cpp)
        add_executable(tracker_revisit_session_tests tests/revisit_session_tests.cpp)
        add_executable(tracker_revisit_session_unrestricted_tests tests/revisit_session_tests.cpp)
        add_executable(tracker_runtime_files_io_tests tests/runtime_files_io_tests.cpp native/runtime_files_win.cpp)
        target_compile_definitions(tracker_revisit_journal_io_tests PRIVATE SKY2_UNRESTRICTED_TRAVEL=0)
        target_compile_definitions(tracker_revisit_session_tests PRIVATE SKY2_UNRESTRICTED_TRAVEL=0)
        target_compile_definitions(tracker_revisit_session_unrestricted_tests PRIVATE SKY2_UNRESTRICTED_TRAVEL=1)
        foreach(target tracker_revisit_journal_io_tests tracker_revisit_session_tests tracker_revisit_session_unrestricted_tests tracker_runtime_files_io_tests)
            target_include_directories(${target} PRIVATE native)
            target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
            target_compile_options(${target} PRIVATE /utf-8 /W4 /EHsc)
            target_link_libraries(${target} PRIVATE bcrypt)
            add_test(NAME ${target} COMMAND ${target})
        endforeach()
        set_source_files_properties(tests/revisit_session_tests.cpp PROPERTIES
            OBJECT_DEPENDS "${TRACKER_LOCAL_HEADERS};${CMAKE_CURRENT_SOURCE_DIR}/native/revisit.cpp")
        # 直接链接正式 MASM 跳板，独立验证内部 ABI，不需要游戏资源、DLL 或第三方依赖。
        enable_language(ASM_MASM)
        add_executable(tracker_exploration_abi_tests
            tests/exploration_abi_tests.cpp tests/exploration_abi_driver.asm native/exploration_shims.asm)
        target_compile_options(tracker_exploration_abi_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4>)
        add_test(NAME tracker_exploration_abi_tests COMMAND tracker_exploration_abi_tests)
        add_executable(tracker_revisit_event_guard_abi_tests
            tests/revisit_event_guard_abi_tests.cpp tests/revisit_event_guard_abi_driver.asm native/revisit_event_guard.asm)
        target_compile_options(tracker_revisit_event_guard_abi_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4>)
        add_test(NAME tracker_revisit_event_guard_abi_tests COMMAND tracker_revisit_event_guard_abi_tests)
        add_executable(tracker_revisit_native_abi_tests
            tests/revisit_native_abi_tests.cpp tests/revisit_native_abi_driver.asm native/revisit_native_shims.asm)
        target_compile_options(tracker_revisit_native_abi_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4>)
        add_test(NAME tracker_revisit_native_abi_tests COMMAND tracker_revisit_native_abi_tests)
        add_executable(tracker_revisit_forest_abi_tests
            tests/revisit_forest_abi_tests.cpp tests/revisit_forest_abi_driver.asm native/revisit_forest.asm)
        target_compile_options(tracker_revisit_forest_abi_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4>)
        add_test(NAME tracker_revisit_forest_abi_tests COMMAND tracker_revisit_forest_abi_tests)
        foreach(target tracker_exploration_abi_tests tracker_revisit_event_guard_abi_tests tracker_revisit_native_abi_tests tracker_revisit_forest_abi_tests)
            target_compile_definitions(${target} PRIVATE SKY2_UNRESTRICTED_TRAVEL=0)
        endforeach()
    endif()
    if(SKY2_BUILD_MOD)
        # 在测试自行分配的内存中调用真实 C++ 助手，核对只改菜单目标字节且不改保存旗标。
        # 此测试链接已准备的 MinHook，但不安装任何挂钩、不加载游戏、不读取存档。
        add_executable(tracker_exploration_runtime_tests
            tests/exploration_runtime_tests.cpp native/exploration_shims.asm)
        target_include_directories(tracker_exploration_runtime_tests PRIVATE native data/generated)
        target_compile_definitions(tracker_exploration_runtime_tests PRIVATE SKY2_UNRESTRICTED_TRAVEL=0)
        target_compile_options(tracker_exploration_runtime_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
        target_link_libraries(tracker_exploration_runtime_tests PRIVATE minhook)
        set_source_files_properties(tests/exploration_runtime_tests.cpp PROPERTIES
            OBJECT_DEPENDS "${TRACKER_LOCAL_HEADERS};${CMAKE_CURRENT_SOURCE_DIR}/native/exploration.cpp;${CMAKE_CURRENT_SOURCE_DIR}/tests/travel_menu_runtime_fixture.h")
        add_test(NAME tracker_exploration_runtime_tests COMMAND tracker_exploration_runtime_tests)
        # 独立运行真实回访 helper；所有对象均为测试自行分配的内存，不连接游戏。
        add_executable(tracker_revisit_event_guard_runtime_tests
            tests/revisit_event_guard_runtime_tests.cpp native/revisit_event_guard.asm)
        target_include_directories(tracker_revisit_event_guard_runtime_tests PRIVATE native data/generated)
        target_compile_definitions(tracker_revisit_event_guard_runtime_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SKY2_UNRESTRICTED_TRAVEL=0)
        target_compile_options(tracker_revisit_event_guard_runtime_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
        target_link_libraries(tracker_revisit_event_guard_runtime_tests PRIVATE minhook)
        set_source_files_properties(tests/revisit_event_guard_runtime_tests.cpp PROPERTIES
            OBJECT_DEPENDS "${TRACKER_LOCAL_HEADERS};${CMAKE_CURRENT_SOURCE_DIR}/native/revisit_event_guard.cpp")
        add_test(NAME tracker_revisit_event_guard_runtime_tests COMMAND tracker_revisit_event_guard_runtime_tests)
        # 森林初始化保护必须在加载繁忙期就有效；隔离VM、事件盒和保存旗标内存，
        # 验证只改本次启用参数，不借用真实游戏进程或存档来进行自动化测试。
        add_executable(tracker_revisit_forest_runtime_tests
            tests/revisit_forest_runtime_tests.cpp native/revisit_forest.asm)
        target_include_directories(tracker_revisit_forest_runtime_tests PRIVATE native data/generated)
        target_compile_definitions(tracker_revisit_forest_runtime_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SKY2_UNRESTRICTED_TRAVEL=0)
        target_compile_options(tracker_revisit_forest_runtime_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
        target_link_libraries(tracker_revisit_forest_runtime_tests PRIVATE minhook)
        set_source_files_properties(tests/revisit_forest_runtime_tests.cpp PROPERTIES
            OBJECT_DEPENDS "${TRACKER_LOCAL_HEADERS};${CMAKE_CURRENT_SOURCE_DIR}/native/revisit_forest.cpp")
        add_test(NAME tracker_revisit_forest_runtime_tests COMMAND tracker_revisit_forest_runtime_tests)
        add_executable(tracker_revisit_native_runtime_tests
            tests/revisit_native_runtime_tests.cpp native/revisit_native_shims.asm)
        target_include_directories(tracker_revisit_native_runtime_tests PRIVATE native data/generated)
        target_compile_definitions(tracker_revisit_native_runtime_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SKY2_UNRESTRICTED_TRAVEL=0)
        target_compile_options(tracker_revisit_native_runtime_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
        target_link_libraries(tracker_revisit_native_runtime_tests PRIVATE minhook)
        set_source_files_properties(tests/revisit_native_runtime_tests.cpp PROPERTIES
            OBJECT_DEPENDS "${TRACKER_LOCAL_HEADERS};${CMAKE_CURRENT_SOURCE_DIR}/native/revisit_native.cpp")
        add_test(NAME tracker_revisit_native_runtime_tests COMMAND tracker_revisit_native_runtime_tests)
        # 同一生产实现分别按剧情限制/全传送策略编译，两套预期独立于 DLL 构建选项。
        # 所有运行时对象仍为测试分配，不挂接正在运行的游戏、不回写任何玩家存档。
        foreach(component event_guard forest native)
            set(target tracker_revisit_${component}_unrestricted_tests)
            set(bridge native/revisit_${component}.asm)
            if(component STREQUAL "native")
                set(bridge native/revisit_native_shims.asm)
            endif()
            add_executable(${target} tests/revisit_${component}_runtime_tests.cpp ${bridge})
            target_include_directories(${target} PRIVATE native data/generated)
            target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SKY2_UNRESTRICTED_TRAVEL=1)
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
            target_link_libraries(${target} PRIVATE minhook)
            add_test(NAME ${target} COMMAND ${target})
        endforeach()
    endif()
endif()
