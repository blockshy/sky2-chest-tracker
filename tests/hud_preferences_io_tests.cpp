// 在进程专属合成目录运行正式文件实现，验证持久化、损坏输入和保存失败。
// 只逐项清理本测试创建的文件；不读取游戏目录或玩家存档。
#include "hud_preferences.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>

int main() {
    using namespace tracker;
    namespace fs = std::filesystem;
    const auto folder = fs::current_path() / ("hud-fixture-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (!fs::create_directory(folder)) return 2;
    unsigned failures = 0;
    const auto check = [&](bool value, const char* label) { if (!value) { ++failures; std::printf("FAIL %s\n", label); } };
    HudPreferences value; value.x = .4f; value.areaName = true;
    HudPreferences loaded;
    check(ReadHudPreferences(folder.wstring(), loaded) == HudReadResult::Missing, "missing preferences use defaults");
    check(WriteHudPreferences(folder.wstring(), value) && ReadHudPreferences(folder.wstring(), loaded) == HudReadResult::Valid &&
        loaded.areaName && loaded.x == value.x, "actual file round trip");
    const auto path = folder / L"hud.ini";
    const auto held = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(held != INVALID_HANDLE_VALUE, "fixture locks existing configuration");
    value.x = .8f; check(!WriteHudPreferences(folder.wstring(), value), "locked config refuses replacement");
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    check(ReadHudPreferences(folder.wstring(), loaded) == HudReadResult::Valid && loaded.x == .4f, "failed save preserves disk state");
    check(WriteHudPreferences(folder.wstring(), value), "retry succeeds after lock release");
    { std::ofstream file(path, std::ios::trunc); file << "[Hud]\nVersion=1\nX=nan\n"; }
    loaded.y = .25f;
    check(ReadHudPreferences(folder.wstring(), loaded) == HudReadResult::Invalid && loaded.y == .25f, "invalid file preserves caller snapshot");
    fs::remove(path); fs::create_directory(path);
    check(!WriteHudPreferences(folder.wstring(), value), "directory cannot be replaced by configuration");
    fs::remove(path);
    check(fs::is_empty(folder), "no failed-write temporary files remain");
    fs::remove(folder);
    return failures ? 1 : 0;
}
