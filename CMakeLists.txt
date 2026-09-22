cmake_minimum_required(VERSION 3.20)
project(Sky2ChestTracker VERSION 0.4.0 LANGUAGES C CXX)

# 关闭 DLL 构建时，仅运行纯逻辑测试；CI 不需要下载或持有游戏资源。
option(SKY2_BUILD_MOD "构建 Windows 游戏插件" ON)
set(SKY2_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "固定提交的第三方源码目录")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(CTest)

# 显式登记头文件，防止中文 MSVC 的包含文件输出解析异常导致复用陈旧对象。
# 发布脚本另执行 clean-first；上游依赖固定到已记录的提交。
set(TRACKER_LOCAL_HEADERS
    native/core.h native/progress.h native/tracker.h native/controller_logic.h native/input_bridge.h native/ui_scale.h
    native/exploration.h native/exploration_logic.h native/travel_refresh.h native/travel_menu.h)
list(TRANSFORM TRACKER_LOCAL_HEADERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(SKY2_HAVE_CATALOG OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/chest_catalog.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/map_catalog.h")
    set(SKY2_HAVE_CATALOG ON)
    list(APPEND TRACKER_LOCAL_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/chest_catalog.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/generated/map_catalog.h")
endif()
set_source_files_properties(
    native/tracker.cpp native/overlay.cpp native/input_bridge.cpp native/xinput_proxy.cpp native/exploration.cpp
    tests/core_tests.cpp tests/controller_tests.cpp tests/ui_scale_tests.cpp tests/exploration_tests.cpp tests/travel_refresh_tests.cpp
    PROPERTIES OBJECT_DEPENDS "${TRACKER_LOCAL_HEADERS}")

if(SKY2_BUILD_MOD)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "DLL requires Windows x64 and MSVC. For portable tests use -DSKY2_BUILD_MOD=OFF.")
    endif()
    # 原生内部调用的寄存器保留约定超出标准 x64 ABI，使用带展开信息的 MASM 跳板。
    enable_language(ASM_MASM)
    if(NOT SKY2_HAVE_CATALOG)
        message(FATAL_ERROR "Missing local game catalog. Run tools/Build-Mod.ps1 -GamePath <game directory>.")
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
    add_library(Sky2ChestTracker SHARED
        native/tracker.cpp native/overlay.cpp native/input_bridge.cpp native/xinput_proxy.cpp native/xinput_proxy.def
        native/exploration.cpp native/exploration_shims.asm)
    target_include_directories(Sky2ChestTracker PRIVATE native data/generated)
    target_compile_definitions(Sky2ChestTracker PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(Sky2ChestTracker PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
    target_link_libraries(Sky2ChestTracker PRIVATE minhook tracker_imgui d3d11 dxgi d3dcompiler bcrypt)
    set_target_properties(Sky2ChestTracker PROPERTIES OUTPUT_NAME xinput1_4 PREFIX "")
endif()

if(BUILD_TESTING)
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
    add_executable(tracker_exploration_tests tests/exploration_tests.cpp)
    target_include_directories(tracker_exploration_tests PRIVATE native)
    add_executable(tracker_travel_refresh_tests tests/travel_refresh_tests.cpp)
    target_include_directories(tracker_travel_refresh_tests PRIVATE native)
    foreach(target tracker_core_tests tracker_controller_tests tracker_ui_scale_tests tracker_exploration_tests tracker_travel_refresh_tests)
        if(MSVC)
            target_compile_options(${target} PRIVATE /utf-8 /W4)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME ${target} COMMAND ${target})
    endforeach()
    if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 直接链接正式 MASM 跳板，独立验证内部 ABI，不需要游戏资源、DLL 或第三方依赖。
        enable_language(ASM_MASM)
        add_executable(tracker_exploration_abi_tests
            tests/exploration_abi_tests.cpp tests/exploration_abi_driver.asm native/exploration_shims.asm)
        target_compile_options(tracker_exploration_abi_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4>)
        add_test(NAME tracker_exploration_abi_tests COMMAND tracker_exploration_abi_tests)
    endif()
    if(SKY2_BUILD_MOD)
        # 在测试自行分配的内存中调用真实 C++ 助手，核对只改菜单目标字节且不改保存旗标。
        # 此测试链接已准备的 MinHook，但不安装任何挂钩、不加载游戏、不读取存档。
        add_executable(tracker_exploration_runtime_tests
            tests/exploration_runtime_tests.cpp native/exploration_shims.asm)
        target_include_directories(tracker_exploration_runtime_tests PRIVATE native data/generated)
        target_compile_options(tracker_exploration_runtime_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>)
        target_link_libraries(tracker_exploration_runtime_tests PRIVATE minhook)
        set_source_files_properties(tests/exploration_runtime_tests.cpp PROPERTIES
            OBJECT_DEPENDS "${TRACKER_LOCAL_HEADERS};${CMAKE_CURRENT_SOURCE_DIR}/native/exploration.cpp;${CMAKE_CURRENT_SOURCE_DIR}/tests/travel_menu_runtime_fixture.h")
        add_test(NAME tracker_exploration_runtime_tests COMMAND tracker_exploration_runtime_tests)
    endif()
endif()
