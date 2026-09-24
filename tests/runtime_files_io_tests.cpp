// 在专属临时目录直接调用正式日志实现，覆盖截断边界、外部占用及多句柄并发。
// 测试不访问游戏目录，不生成备份；清理时仅逐个删除本测试创建的文件和空目录。
#include "runtime_files.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
static std::string ReadAll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void WriteFixture(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

int main() {
    unsigned failures = 0;
    const auto check = [&](bool ok, const char* name) {
        if (!ok) { ++failures; std::printf("FAIL %s\n", name); }
    };
    const fs::path root = fs::current_path() / ("runtime-files-fixture-" + std::to_string(GetCurrentProcessId()) +
        "-" + std::to_string(GetTickCount64()));
    if (!fs::create_directory(root)) return 2;
    const auto data = root / L"plugins" / L"Sky2ChestTracker";
    check(tracker::PrepareRuntimeDataDirectory(data.wstring(), true), "prepare ASI directory hierarchy");
    check(tracker::PrepareRuntimeDataDirectory(data.wstring(), true), "existing data folder is reusable");
    check(!fs::exists(root / L"Sky2Mods"), "old ASI root is never created");
    const auto standalone = root / L"Sky2ChestTracker";
    check(tracker::PrepareRuntimeDataDirectory(standalone.wstring(), false), "prepare standalone folder separately");
    const auto log = data / L"tracker.log";
    const auto append = [&](std::string_view line) { return tracker::AppendBoundedRuntimeLog(log.wstring(), line); };
    check(append("first\r\n") && append("second\r\n") && ReadAll(log) == "first\r\nsecond\r\n", "normal lines append in order");

    // 单行自身超过容量时拒绝写入，不能为了无法保存的消息清空已有日志。
    const auto original = ReadAll(log);
    check(!append(std::string(tracker::kRuntimeLogMaxBytes + 1, 'x')) && ReadAll(log) == original,
        "oversized single line preserves previous log");
    check(!append("") && ReadAll(log) == original, "empty input leaves log unchanged");
    WriteFixture(log, std::string(tracker::kRuntimeLogMaxBytes - 2, 'a'));
    check(append("\r\n") && fs::file_size(log) == tracker::kRuntimeLogMaxBytes,
        "exactly filling capacity does not truncate");
    check(append("after-limit\r\n") && ReadAll(log) == "after-limit\r\n",
        "next line resets the same file at capacity");
    WriteFixture(log, std::string(tracker::kRuntimeLogMaxBytes + 8, 'b'));
    check(append("after-old-large-log\r\n") && ReadAll(log) == "after-old-large-log\r\n",
        "preexisting oversized log is reduced on first append");
    check(append(std::string(tracker::kRuntimeLogMaxBytes, 'c')) && fs::file_size(log) == tracker::kRuntimeLogMaxBytes,
        "single line exactly at limit is supported");

    // 普通查看器或其它程序若占用写入权限，诊断失败必须保持原文件，不影响调用者。
    WriteFixture(log, "locked-sentinel");
    HANDLE locked = CreateFileW(log.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "fixture read lock established");
    check(!append("blocked\r\n") && ReadAll(log) == "locked-sentinel", "write contention preserves log");
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);

    // 外部句柄可以允许读写共享却长期锁住字节范围。此时记录应有界失败，不能因为
    // 日志阻塞游戏；同一线程持锁即可稳定复现，也无需启动真实游戏或辅助进程。
    locked = CreateFileW(log.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    OVERLAPPED range{};
    const bool rangeLocked = locked != INVALID_HANDLE_VALUE &&
        LockFileEx(locked, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &range);
    check(rangeLocked, "fixture byte-range lock established");
    if (rangeLocked) {
        const ULONGLONG start = GetTickCount64();
        check(!append("blocked-range\r\n") && GetTickCount64() - start < 2000,
            "external byte-range lock has bounded wait");
        UnlockFileEx(locked, 0, MAXDWORD, MAXDWORD, &range);
        check(ReadAll(log) == "locked-sentinel", "byte-range lock failure keeps existing bytes");
    }
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);

    // 硬链接指向的内容不应因日志到达上限而被截断。使用真实 NTFS 链接验证句柄检查。
    const auto foreign = root / L"foreign.txt";
    WriteFixture(foreign, "foreign-file-sentinel");
    fs::remove(log);
    const bool linked = CreateHardLinkW(log.c_str(), foreign.c_str(), nullptr) != FALSE;
    check(linked, "hard-link fixture created");
    if (linked) {
        check(!append("blocked-link\r\n") && ReadAll(foreign) == "foreign-file-sentinel",
            "hard-linked log never changes foreign content");
        fs::remove(log);
    }
    fs::remove(foreign);

    // 每次调用分别打开文件，模拟多个 DLL 副本各自持有句柄。生产实现抢锁最多等待
    // 50 ms，CI 调度饥饿可能使某次调用合法返回 false，因此由测试端重试尚未成功的
    // 同一行；不能据此放宽完整性检查，也不能为了测试把游戏中的锁等待延长。
    // 全部线程共用 5 秒重试期限；非零 retry 合法，任何到期失败仍导致测试失败。
    // 最终仍要求全部行完整且恰好出现一次，不依赖同一个 C++ mutex 才能通过测试。
    constexpr unsigned threadCount = 4, linesPerThread = 250;
    constexpr ULONGLONG retryBudgetMs = 5000;
    const ULONGLONG concurrentStart = GetTickCount64();
    std::atomic<unsigned> retryCount{0}, expiredWrites{0};
    std::vector<std::thread> writers;
    for (unsigned thread = 0; thread < threadCount; ++thread) {
        writers.emplace_back([&, thread] {
            for (unsigned row = 0; row < linesPerThread; ++row) {
                const auto line = std::to_string(thread) + ":" + std::to_string(row) + "\n";
                bool written = false;
                while (!written) {
                    written = append(line);
                    if (written) break;
                    if (GetTickCount64() - concurrentStart >= retryBudgetMs) {
                        ++expiredWrites;
                        break;
                    }
                    ++retryCount;
                    Sleep(1);
                }
                // 到期后停止当前写入线程，避免永久写入失败时为余下每行重新等待。
                // 其它线程也共享同一期限；后续完整行断言会明确报告尚未写入的行。
                if (!written) break;
            }
        });
    }
    for (auto& writer : writers) writer.join();
    std::istringstream lines(ReadAll(log));
    std::set<std::string> actual;
    unsigned count = 0;
    for (std::string line; std::getline(lines, line); ++count) actual.insert(line);
    bool complete = actual.size() == threadCount * linesPerThread;
    for (unsigned thread = 0; thread < threadCount; ++thread)
        for (unsigned row = 0; row < linesPerThread; ++row)
            complete = complete && actual.count(std::to_string(thread) + ":" + std::to_string(row)) == 1;
    std::printf("Concurrent log: retries=%u expired=%u lines=%u unique=%zu elapsed=%llu ms\n",
        retryCount.load(), expiredWrites.load(), count, actual.size(), GetTickCount64() - concurrentStart);
    check(expiredWrites == 0 && complete && count == threadCount * linesPerThread,
        "independent concurrent handles preserve every complete line");
    check(fs::file_size(log) <= tracker::kRuntimeLogMaxBytes, "concurrent append remains within capacity");
    unsigned dataFiles = 0;
    for (const auto& unused : fs::directory_iterator(data)) { (void)unused; ++dataFiles; }
    check(dataFiles == 1, "logger creates no rotation or backup files");

    fs::remove(log);
    check(fs::remove(data) && fs::remove(root / L"plugins") && fs::remove(standalone) && fs::remove(root),
        "remove only fixture files and empty directories");
    std::printf("%u runtime file I/O failure(s)\n", failures);
    return failures ? 1 : 0;
}
