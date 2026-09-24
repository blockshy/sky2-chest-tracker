# 此目录也可独立构建；诊断插件与假宿主均不依赖游戏资源、ImGui 或 MinHook。
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(Sky2AsiDiagnostics LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ASI integration diagnostics require Windows x64 and MSVC.")
endif()

add_library(Sky2CoexistProbe SHARED coexist_probe.cpp)
target_link_libraries(Sky2CoexistProbe PRIVATE version user32)
add_library(Sky2AsiFixtureA SHARED loader_fixture.cpp)
target_compile_definitions(Sky2AsiFixtureA PRIVATE SKY2_FIXTURE_ID=65)
add_library(Sky2AsiFixtureB SHARED loader_fixture.cpp)
target_compile_definitions(Sky2AsiFixtureB PRIVATE SKY2_FIXTURE_ID=66)
add_executable(sky2_asi_loader_host loader_host.cpp)
# 游戏按 ordinal 2/3 导入 XInput；UAL 在装载时修补宿主 IAT，通用 DLL 的原始
# GetProcAddress(ordinal) 并不等价。生成专用导入库，让假宿主覆盖真实装载路径。
set(SKY2_HOST_XINPUT_IMPORT "${CMAKE_CURRENT_BINARY_DIR}/sky2_host_xinput.lib")
add_custom_command(OUTPUT "${SKY2_HOST_XINPUT_IMPORT}"
    COMMAND "${CMAKE_AR}" /nologo /machine:x64
            "/def:${CMAKE_CURRENT_SOURCE_DIR}/xinput_host_imports.def"
            "/out:${SKY2_HOST_XINPUT_IMPORT}"
    DEPENDS xinput_host_imports.def VERBATIM)
add_custom_target(sky2_host_xinput_import DEPENDS "${SKY2_HOST_XINPUT_IMPORT}")
add_dependencies(sky2_asi_loader_host sky2_host_xinput_import)
target_link_libraries(sky2_asi_loader_host PRIVATE "${SKY2_HOST_XINPUT_IMPORT}")
foreach(target Sky2CoexistProbe Sky2AsiFixtureA Sky2AsiFixtureB sky2_asi_loader_host)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(${target} PRIVATE /utf-8 /W4 /EHsc)
endforeach()
foreach(target Sky2CoexistProbe Sky2AsiFixtureA Sky2AsiFixtureB)
    set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".asi")
endforeach()
