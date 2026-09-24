// 验证发行形式与运行目录的对应关系，不创建文件，也不要求持有游戏资源。
#include "runtime_files.h"
#include <cstdio>

int main() {
    unsigned failures = 0;
    const auto check = [&](bool ok, const char* name) {
        if (!ok) { ++failures; std::printf("FAIL %s\n", name); }
    };
    check(tracker::RuntimeDataDirectory(L"E:\\SteamLibrary\\Game\\sora_2nd.exe", true) ==
        L"E:\\SteamLibrary\\Game\\plugins\\Sky2ChestTracker", "ASI data stays below plugins");
    check(tracker::RuntimeDataDirectory(L"E:\\SteamLibrary\\Game\\sora_2nd.exe", false) ==
        L"E:\\SteamLibrary\\Game\\Sky2ChestTracker", "standalone data location is unchanged");
    check(tracker::RuntimeDataDirectory(L"C:\\sora_2nd.exe", true) ==
        L"C:\\plugins\\Sky2ChestTracker", "drive root retains absolute separator");
    check(tracker::RuntimeDataDirectory(L"\\\\server\\share\\空之轨迹\\sora_2nd.exe", true) ==
        L"\\\\server\\share\\空之轨迹\\plugins\\Sky2ChestTracker", "UNC and non-ASCII paths are preserved");
    check(tracker::RuntimeDataDirectory(L"E:/games/sky2/sora_2nd.exe", true) ==
        L"E:/games/sky2/plugins\\Sky2ChestTracker", "forward slash EXE path is supported");
    check(tracker::RuntimeDataDirectory(L"", true).empty() &&
        tracker::RuntimeDataDirectory(L"sora_2nd.exe", true).empty() &&
        tracker::RuntimeDataDirectory(L"E:\\game\\", false).empty(), "missing EXE parent or filename is rejected");
    std::printf("%u runtime path failure(s)\n", failures);
    return failures ? 1 : 0;
}
