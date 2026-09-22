// Windows x64 汇编桥的 ABI 回归测试：直接链接 native/exploration_shims.asm。
// 该程序只使用自包含测试桩，不链接探索模块的游戏逻辑，也不加载游戏或读写存档。
// CMake 应仅在 Windows + MSVC + x64 下构建本测试；其他平台继续运行纯逻辑测试。
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <windows.h>

// 与 exploration_abi_driver.asm 的 CAPTURE 宏共享布局；字段偏移全部在编译期检查。
// RSP 仅用于比较调用栈；其余整数及 XMM 字段是需要桥接函数保存的易失寄存器。
struct alignas(16) RegisterSnapshot {
    uint64_t rax, rcx, rdx, r8, r9, r10, r11, rsp;
    uint32_t xmm[6][4];
};
static_assert(sizeof(RegisterSnapshot) == 0xA0, "MASM 快照布局不一致");
static_assert(offsetof(RegisterSnapshot, rax) == 0x00 && offsetof(RegisterSnapshot, rcx) == 0x08 &&
              offsetof(RegisterSnapshot, rdx) == 0x10 && offsetof(RegisterSnapshot, r8) == 0x18 &&
              offsetof(RegisterSnapshot, r9) == 0x20 && offsetof(RegisterSnapshot, r10) == 0x28 &&
              offsetof(RegisterSnapshot, r11) == 0x30 && offsetof(RegisterSnapshot, rsp) == 0x38 &&
              offsetof(RegisterSnapshot, xmm) == 0x40, "C++ / MASM 寄存器偏移不一致");
extern "C" {
    RegisterSnapshot abiInput{}, abiTrampoline{}, abiAfter{};
    uint64_t abiCallbackArgs[6]{};
    uint64_t abiHelperStackModulo = 0;
    uint64_t abiCanaries[2]{};
    uint32_t abiCallbackAlpha = 0;
    float abiResultAlpha = 0;
    void AbiInvokeMapShim();
    void AbiMapReturnSite();
    void AbiCaptureTrampoline();
    void AbiPoisonVolatiles();
    void (*Sky2NextMapAlpha)() = &AbiCaptureTrampoline;

    // 传送桥在原函数返回后插入辅助调用，因此必须保存原函数的输出，而非入口状态。
    RegisterSnapshot abiTravelOriginalInput{}, abiTravelOutput{}, abiTravelAfter{};
    uint64_t abiTravelOriginalCalls = 0, abiTravelHelperCalls = 0;
    uint64_t abiTravelManager = 0, abiTravelSpotId = 0, abiTravelOriginalCallsAtHelper = 0;
    uint64_t abiTravelHelperStackModulo = 0, abiTravelCallerStack = 0;
    uint64_t abiTravelWantedFlags = 0, abiTravelOriginalFlags = 0, abiTravelAfterFlags = 0;
    void AbiInvokeTravelShim();
    void AbiOriginalRegisterSpot();
    void (*Sky2NextRegisterSpot)() = &AbiOriginalRegisterSpot;

    // 前置桥必须完全保留入口状态；四种原生入口使用不同目标桩，并区分两种 helper，
    // 防止新增 manager 构建桥误调用 menu 刷新 helper 时寄存器测试仍被当作通过。
    RegisterSnapshot abiRefreshTrampoline{}, abiRefreshAfter{};
    uint64_t abiRefreshHelperCalls = 0, abiRefreshTrampolineCalls = 0;
    uint64_t abiRefreshMenu = 0, abiRefreshTrampolineCallsAtHelper = 0;
    uint64_t abiRefreshHelperStackModulo = 0, abiRefreshCallerStack = 0;
    uint64_t abiRefreshWantedFlags = 0, abiRefreshInputFlags = 0;
    uint64_t abiRefreshTrampolineFlags = 0, abiRefreshAfterFlags = 0, abiRefreshTrampolineKind = 0;
    uint64_t abiRefreshHelperKind = 0;
    uint64_t abiRefreshHelperCaller = 0, abiRefreshTrampolineCaller = 0;
    bool abiRefreshShouldSkip = false;
    void Sky2MapBrowseShim();
    void Sky2SpotListShim();
    void Sky2AreaListShim();
    void Sky2BuildTravelShim();
    void AbiInvokeRefreshShim();
    void AbiRefreshReturnSite();
    void AbiCaptureMapBrowseTrampoline();
    void AbiCaptureSpotListTrampoline();
    void AbiCaptureAreaListTrampoline();
    void AbiCaptureBuildTravelTrampoline();
    void (*abiRefreshShim)() = nullptr;
    void (*Sky2NextMapBrowse)() = &AbiCaptureMapBrowseTrampoline;
    void (*Sky2NextSpotList)() = &AbiCaptureSpotListTrampoline;
    void (*Sky2NextAreaList)() = &AbiCaptureAreaListTrampoline;
    void (*Sky2NextBuildTravel)() = &AbiCaptureBuildTravelTrampoline;

    // 六参数使用 MSVC 编译器的标准 Windows x64 ABI。第四个浮点参数必须来自 XMM3，
    // 返回地址及原始 R9 必须从第 5、第 6 个参数的栈槽传递，不能混用原始 R9。
    __declspec(noinline) float Sky2MapAlpha(uint64_t a, uint64_t b, uint64_t c, float alpha,
                                           uint64_t caller, uint64_t originalR9) noexcept {
        abiHelperStackModulo = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) & 15;
        abiCallbackArgs[0] = a;
        abiCallbackArgs[1] = b;
        abiCallbackArgs[2] = c;
        std::memcpy(&abiCallbackAlpha, &alpha, sizeof(alpha));
        abiCallbackArgs[3] = abiCallbackAlpha;
        abiCallbackArgs[4] = caller;
        abiCallbackArgs[5] = originalR9;
        // 主动污染每个易失寄存器，防止测试因编译器碰巧没用某寄存器而漏掉保存错误。
        AbiPoisonVolatiles();
        return abiResultAlpha;
    }

    // 参数类型与生产辅助函数一致：管理器地址为 uintptr_t，点位编号为 32 位无符号值。
    // 记录原函数调用次数，可同时检验回调顺序，不能只验证最终累计次数。
    __declspec(noinline) void Sky2AfterRegisterSpot(uintptr_t manager, uint32_t id) noexcept {
        ++abiTravelHelperCalls;
        abiTravelOriginalCallsAtHelper = abiTravelOriginalCalls;
        abiTravelManager = manager;
        abiTravelSpotId = id;
        abiTravelHelperStackModulo = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) & 15;
        AbiPoisonVolatiles();
    }

    // 与正式前置 helper 完全相同的单参数 ABI。记录原函数尚未执行，随后故意破坏全部
    // 易失寄存器与标志，确保三个浏览桥不能靠 helper 恰好未使用某个寄存器而通过测试。
    __declspec(noinline) bool Sky2BeforeMapRefresh(uintptr_t menu) noexcept {
        ++abiRefreshHelperCalls;
        abiRefreshHelperKind = 1;
        abiRefreshMenu = menu;
        abiRefreshTrampolineCallsAtHelper = abiRefreshTrampolineCalls;
        abiRefreshHelperStackModulo = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) & 15;
        AbiPoisonVolatiles();
        return abiRefreshShouldSkip;
    }

    // 正式构建辅助接收 manager 和最外层原始返回地址，用后者核验是否已重算剧情状态；
    // 正式实现始终返回 false 继续原生 BuildDisplay。本桩额外允许
    // true，只用于覆盖共用汇编宏的另一条恢复路径，不能视为生产逻辑可以跳过构建。
    __declspec(noinline) bool Sky2BeforeBuildTravel(uintptr_t manager, uintptr_t caller) noexcept {
        ++abiRefreshHelperCalls;
        abiRefreshHelperKind = 2;
        abiRefreshMenu = manager;
        abiRefreshHelperCaller = caller;
        abiRefreshTrampolineCallsAtHelper = abiRefreshTrampolineCalls;
        abiRefreshHelperStackModulo = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) & 15;
        AbiPoisonVolatiles();
        return abiRefreshShouldSkip;
    }
}

// 统一结果收集器便于后续添加其他桥接入口。报告保留样本编号，便于定位边界值失败。
struct TestResult {
    unsigned failures = 0;
    unsigned checks = 0;
    void Check(bool value, const char* description, unsigned sample) {
        ++checks;
        if (!value) { ++failures; std::printf("FAIL sample=%u %s\n", sample, description); }
    }
};

static void TestMapAlphaShim(TestResult& result) {
    const auto check = [&](bool value, const char* description, unsigned sample) {
        result.Check(value, description, sample);
    };
    constexpr uint64_t canary0 = 0x1122334455667788ull;
    constexpr uint64_t canary1 = 0x8877665544332211ull;
    // 同时覆盖普通透明度、边界值以及无须进行运算的 NaN 位模式；其他寄存器每轮更换。
    const uint32_t alphas[] = {0x00000000, 0x3F800000, 0x3E800000, 0xBF800000, 0x7FC01234};
    for (unsigned sample = 0; sample < 100; ++sample) {
        const auto word = [sample](uint64_t index) {
            return 0xA1B2C3D4E5F60718ull ^ (uint64_t{sample} << 32) ^ (index * 0x102030405060708ull);
        };
        // 按成员赋值，避免把若干独立整数成员错误地当成数组跨界访问。
        abiInput = {word(0), word(1), word(2), word(3), word(4), word(5), word(6), 0, {}};
        for (unsigned reg = 0; reg < 6; ++reg)
            for (unsigned lane = 0; lane < 4; ++lane)
                abiInput.xmm[reg][lane] = 0x91000000u + sample * 256 + reg * 16 + lane;
        abiInput.xmm[3][0] = alphas[sample % 5];
        const uint32_t expectedAlpha = alphas[(sample + 1) % 5];
        std::memcpy(&abiResultAlpha, &expectedAlpha, sizeof(expectedAlpha));
        AbiInvokeMapShim();
        check(abiHelperStackModulo == 8, "helper 入口栈须为 16n+8", sample);
        check(abiCallbackArgs[0] == abiInput.rcx && abiCallbackArgs[1] == abiInput.rdx &&
              abiCallbackArgs[2] == abiInput.r8, "前三个整数参数原样传入 C++", sample);
        check(abiCallbackAlpha == abiInput.xmm[3][0], "第四个浮点参数从 XMM3 传入", sample);
        check(abiCallbackArgs[4] == reinterpret_cast<uintptr_t>(&AbiMapReturnSite), "第五参数为原始返回地址", sample);
        check(abiCallbackArgs[5] == abiInput.r9, "第六参数为原始 R9", sample);
        check(std::memcmp(&abiInput, &abiTrampoline, 7 * sizeof(uint64_t)) == 0,
              "trampoline 接收到全部原始易失整数寄存器，包括 R9/R10", sample);
        check((abiTrampoline.rsp & 15) == 8 && abiTrampoline.rsp + 8 == abiAfter.rsp,
              "尾跳 trampoline 恢复原始调用栈", sample);
        auto expected = abiInput;
        expected.xmm[3][0] = expectedAlpha;
        check(std::memcmp(expected.xmm, abiTrampoline.xmm, sizeof(expected.xmm)) == 0,
              "仅替换 XMM3 低 32 位，保留其高位以及其他 XMM0 至 XMM5", sample);
        check(std::memcmp(&abiInput, &abiAfter, 7 * sizeof(uint64_t)) == 0 &&
              std::memcmp(expected.xmm, abiAfter.xmm, sizeof(expected.xmm)) == 0,
              "最终返回至调用者后寄存器仍与 trampoline 快照一致", sample);
        check(abiCanaries[0] == canary0 && abiCanaries[1] == canary1,
              "调用者栈保护值未被桥接函数覆盖", sample);
    }
}

static void TestRegisterSpotShim(TestResult& result) {
    const auto check = [&](bool value, const char* description, unsigned sample) {
        result.Check(value, description, sample);
    };
    // 只控制常规算术状态位。禁止给测试设置 TF、DF、IF 等调试、方向或特权状态位；
    // ASM 原函数桩会保留这些位，再捕获完整 RFLAGS，最终进行完整位模式比较。
    constexpr uint64_t arithmeticFlags[] = {0x001, 0x004, 0x010, 0x040, 0x080, 0x800};
    constexpr uint64_t canary0 = 0x1122334455667788ull;
    constexpr uint64_t canary1 = 0x8877665544332211ull;
    for (unsigned sample = 0; sample < 64; ++sample) {
        const auto word = [sample](uint64_t index) {
            return 0x913579BDF02468ACull ^ (uint64_t{sample} << 32) ^ (index * 0x102030405060708ull);
        };
        abiInput = {word(0), word(1), word(2), word(3), word(4), word(5), word(6), 0, {}};
        // 每个返回寄存器都与入口不同，防止“错误恢复入口快照”也被测试当作通过。
        abiTravelOutput = {word(7), word(8), word(9), word(10), word(11), word(12), word(13), 0, {}};
        for (unsigned reg = 0; reg < 6; ++reg) {
            for (unsigned lane = 0; lane < 4; ++lane) {
                abiInput.xmm[reg][lane] = 0x91000000u + sample * 256 + reg * 16 + lane;
                abiTravelOutput.xmm[reg][lane] = 0x26000000u + sample * 256 + reg * 16 + lane;
            }
        }
        abiTravelWantedFlags = 0;
        for (unsigned bit = 0; bit < 6; ++bit)
            if (sample & (1u << bit)) abiTravelWantedFlags |= arithmeticFlags[bit];
        abiTravelOriginalCalls = abiTravelHelperCalls = 0;
        abiTravelOriginalCallsAtHelper = 0;
        AbiInvokeTravelShim();
        check(abiTravelOriginalCalls == 1 && abiTravelHelperCalls == 1 && abiTravelOriginalCallsAtHelper == 1,
              "传送原函数与后置辅助各调用一次，且原函数先执行", sample);
        check(std::memcmp(&abiInput, &abiTravelOriginalInput, 7 * sizeof(uint64_t)) == 0 &&
              std::memcmp(abiInput.xmm, abiTravelOriginalInput.xmm, sizeof(abiInput.xmm)) == 0,
              "传送原函数收到完整入口寄存器和原始参数", sample);
        check(abiTravelManager == abiInput.rcx && abiTravelSpotId == static_cast<uint32_t>(abiInput.rdx),
              "后置辅助收到原始管理器和点位编号，不误用原函数返回寄存器", sample);
        check((abiTravelOriginalInput.rsp & 15) == 8 && abiTravelHelperStackModulo == 8,
              "传送原函数及后置辅助均遵守 Windows x64 栈对齐", sample);
        check(std::memcmp(&abiTravelOutput, &abiTravelAfter, 7 * sizeof(uint64_t)) == 0,
              "后置辅助破坏 volatile 后，调用者仍得到原函数的全部整数返回态", sample);
        check(std::memcmp(abiTravelOutput.xmm, abiTravelAfter.xmm, sizeof(abiTravelOutput.xmm)) == 0,
              "调用者仍得到原函数 XMM0 至 XMM5 的全部 128 位返回态", sample);
        check((abiTravelOriginalFlags & 0x8D5) == abiTravelWantedFlags,
              "原函数桩实际产生所要求的六种算术状态位组合", sample);
        check(abiTravelAfterFlags == abiTravelOriginalFlags,
              "后置辅助执行后完整恢复原函数 RFLAGS，返回栈调整也不破坏标志", sample);
        check(abiTravelAfter.rsp == abiTravelCallerStack,
              "传送桥正常返回且调用者栈指针恢复原值", sample);
        check(abiCanaries[0] == canary0 && abiCanaries[1] == canary1,
              "传送桥及辅助函数未破坏调用者栈保护值", sample);
    }
}

static void TestRefreshShims(TestResult& result) {
    const auto check = [&](bool value, const char* description, unsigned sample) {
        result.Check(value, description, sample);
    };
    constexpr uint64_t arithmeticFlags[] = {0x001, 0x004, 0x010, 0x040, 0x080, 0x800};
    constexpr uint64_t canary0 = 0x1122334455667788ull;
    constexpr uint64_t canary1 = 0x8877665544332211ull;
    using Entry = void(*)();
    const Entry entries[] = {&Sky2MapBrowseShim, &Sky2SpotListShim, &Sky2AreaListShim, &Sky2BuildTravelShim};
    for (unsigned kind = 0; kind < sizeof(entries) / sizeof(entries[0]); ++kind) {
        abiRefreshShim = entries[kind];
        // 两条路径都必须保留输入：false 交给 trampoline，true 直接返回原调用者。
        for (unsigned branch = 0; branch < 2; ++branch) {
          abiRefreshShouldSkip = branch != 0;
          for (unsigned flags = 0; flags < 64; ++flags) {
            const unsigned sample = kind * 128 + branch * 64 + flags;
            const auto word = [sample](uint64_t index) {
                return 0x73519BDF02468ACFull ^ (uint64_t{sample} << 32) ^ (index * 0x213141516171819ull);
            };
            abiInput = {word(0), word(1), word(2), word(3), word(4), word(5), word(6), 0, {}};
            for (unsigned reg = 0; reg < 6; ++reg)
                for (unsigned lane = 0; lane < 4; ++lane)
                    abiInput.xmm[reg][lane] = 0x83000000u + sample * 256 + reg * 16 + lane;
            abiRefreshWantedFlags = 0;
            for (unsigned bit = 0; bit < 6; ++bit)
                if (flags & (1u << bit)) abiRefreshWantedFlags |= arithmeticFlags[bit];
            abiRefreshHelperCalls = abiRefreshTrampolineCalls = 0;
            abiRefreshTrampolineCallsAtHelper = 99;
            abiRefreshTrampolineKind = 0;
            abiRefreshHelperKind = 0;
            abiRefreshHelperCaller = abiRefreshTrampolineCaller = 0;
            abiRefreshTrampoline = {};
            abiRefreshTrampolineFlags = 0;
            AbiInvokeRefreshShim();
            check(abiRefreshHelperCalls == 1 && abiRefreshTrampolineCalls == (branch == 0 ? 1u : 0u) &&
                  abiRefreshTrampolineCallsAtHelper == 0, "helper 执行一次，返回值决定原函数执行一次或跳过", sample);
            check(abiRefreshTrampolineKind == (branch == 0 ? kind + 1 : 0),
                  "false 路由至对应 trampoline，true 不调用任一 trampoline", sample);
            check(abiRefreshHelperKind == (kind == 3 ? 2u : 1u),
                  "浏览桥调用 menu helper，构建桥调用独立 manager helper", sample);
            check(abiRefreshMenu == abiInput.rcx, "前置 helper 接收到入口 RCX 中原始 menu 或 manager", sample);
            check(abiRefreshHelperCaller == (kind == 3 ? reinterpret_cast<uintptr_t>(&AbiRefreshReturnSite) : 0),
                  "只有构建helper收到原始调用者返回地址，浏览helper仍采用单参数ABI", sample);
            check(abiRefreshHelperStackModulo == 8, "前置 helper 遵守 Windows x64 栈对齐", sample);
            if (branch == 0) {
                check((abiRefreshTrampoline.rsp & 15) == 8 &&
                      abiRefreshTrampoline.rsp + 8 == abiRefreshCallerStack,
                      "false 路径尾跳至原函数时精确恢复入口栈", sample);
                check(std::memcmp(&abiInput, &abiRefreshTrampoline, 7 * sizeof(uint64_t)) == 0,
                      "刷新 helper 破坏寄存器后原函数仍收到全部原始整数寄存器", sample);
                check(std::memcmp(abiInput.xmm, abiRefreshTrampoline.xmm, sizeof(abiInput.xmm)) == 0,
                      "原函数收到 XMM0 至 XMM5 完整 128 位入口值，包括 XMM1 参数", sample);
                check(abiRefreshTrampolineFlags == abiRefreshInputFlags,
                      "false 路径原函数收到完整原始 RFLAGS", sample);
                check(abiRefreshTrampolineCaller == reinterpret_cast<uintptr_t>(&AbiRefreshReturnSite) &&
                      (kind != 3 || abiRefreshHelperCaller == abiRefreshTrampolineCaller),
                      "构建helper的来源与尾跳原函数栈顶返回地址一致，原始RDX仍独立恢复", sample);
            } else {
                const RegisterSnapshot empty{};
                check(std::memcmp(&abiRefreshTrampoline, &empty, sizeof(empty)) == 0 &&
                      abiRefreshTrampolineFlags == 0, "true 路径未执行任何原函数采样代码", sample);
            }
            check((abiRefreshInputFlags & 0x8D5) == abiRefreshWantedFlags,
                  "调用器实际生成全部六种算术标志的组合", sample);
            check(abiRefreshAfterFlags == abiRefreshInputFlags,
                  "尾跳与直接返回都完整恢复入口 RFLAGS，不残留 bool 分支的 TEST 标志", sample);
            check(abiRefreshAfter.rsp == abiRefreshCallerStack, "两条返回路径都完全恢复原调用栈", sample);
            check(std::memcmp(&abiInput, &abiRefreshAfter, 7 * sizeof(uint64_t)) == 0 &&
                  std::memcmp(abiInput.xmm, abiRefreshAfter.xmm, sizeof(abiInput.xmm)) == 0,
                  "两条路径返回后整数与 XMM 状态均完整保留，RAX 不泄漏 helper 的 bool", sample);
            check(abiCanaries[0] == canary0 && abiCanaries[1] == canary1,
                  "跳过原函数或原函数写满 shadow space 均不破坏调用者保护值", sample);
          }
        }
    }
}

// 除了正常调用返回，还用 Windows 自己的展开器检查正式桥的栈帧描述。
// 所有栈内容均为本测试分配的合成数组；只计算调用者上下文，不执行其中任何地址。
// 覆盖函数体，以及尾跳、直接返回两种 epilogue 各自的三处边界，可发现
// “寄存器测试通过但异常展开元数据不正确”的问题。
static void TestRefreshUnwind(TestResult& result) {
    using Entry = void(*)();
    const Entry entries[] = {&Sky2MapBrowseShim, &Sky2SpotListShim, &Sky2AreaListShim, &Sky2BuildTravelShim};
    constexpr uint64_t savedFrame = 0x13792548ACBDEF60ull;
    const auto returnAddress = reinterpret_cast<uint64_t>(&AbiMapReturnSite);
    for (unsigned kind = 0; kind < sizeof(entries) / sizeof(entries[0]); ++kind) {
        DWORD64 imageBase = 0;
        const auto address = reinterpret_cast<DWORD64>(entries[kind]);
        auto* function = RtlLookupFunctionEntry(address, &imageBase, nullptr);
        result.Check(function != nullptr, "刷新桥具有 Windows 可发现的 RUNTIME_FUNCTION", kind);
        if (!function) continue;
        const auto* unwind = reinterpret_cast<const unsigned char*>(imageBase + function->UnwindData);
        result.Check((unwind[0] & 7) == 1 && (unwind[3] & 15) == 5,
                     "刷新桥使用版本 1 展开信息及 RBP 帧指针", kind);
        const auto begin = imageBase + function->BeginAddress;
        const auto end = imageBase + function->EndAddress;
        // 两条路径都以 LEA RSP,[RBP+E0h] / POP RBP 收尾，然后分别 JMP 或 RET。
        // 在当前函数界内定位实际机器码，避免靠分支长度推测偏移而让测试与代码脱节。
        constexpr unsigned char prefix[] = {0x48, 0x8D, 0xA5, 0xE0, 0, 0, 0, 0x5D};
        DWORD64 tailEpilogue = 0, returnEpilogue = 0;
        unsigned tailCount = 0, returnCount = 0;
        for (auto pc = begin; pc + 9 <= end; ++pc) {
            const auto* code = reinterpret_cast<const unsigned char*>(pc);
            if (std::memcmp(code, prefix, sizeof(prefix)) != 0) continue;
            if (code[8] == 0xC3) { returnEpilogue = pc; ++returnCount; }
            if (pc + 14 <= end && code[8] == 0xFF && code[9] == 0x25) {
                tailEpilogue = pc; ++tailCount;
            }
        }
        const bool epiloguesMatch = tailCount == 1 && returnCount == 1 && returnEpilogue + 9 == end;
        result.Check(epiloguesMatch, "刷新桥实际生成各一条规范尾跳和直接返回 epilogue", kind);
        if (!epiloguesMatch) continue;
        struct ControlPoint { DWORD64 pc, stackOffset; bool frameRestored; };
        const ControlPoint controlPoints[] = {
            {begin + unwind[1], 0, false},
            {tailEpilogue, 0, false}, {tailEpilogue + 7, 0xE0, false}, {tailEpilogue + 8, 0xE8, true},
            {returnEpilogue, 0, false}, {returnEpilogue + 7, 0xE0, false}, {returnEpilogue + 8, 0xE8, true}
        };
        for (unsigned phase = 0; phase < 7; ++phase) {
            alignas(16) uint64_t stack[40]{};
            const auto frame = reinterpret_cast<DWORD64>(stack);
            stack[0xD8 / 8] = 0x202; // 合成入口 RFLAGS 槽，只作为栈布局的一部分。
            stack[0xE0 / 8] = savedFrame;
            stack[0xE8 / 8] = returnAddress;
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            context.Rip = controlPoints[phase].pc;
            context.Rsp = frame + controlPoints[phase].stackOffset;
            context.Rbp = controlPoints[phase].frameRestored ? savedFrame : frame;
            PVOID handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context,
                             &handlerData, &establisherFrame, nullptr);
            result.Check(context.Rip == returnAddress && context.Rsp == frame + 0xF0 &&
                         context.Rbp == savedFrame, "Windows 从桥函数体或两种 epilogue 正确恢复调用者上下文",
                         kind * 7 + phase);
        }
    }
}

int main() {
    TestResult result;
    TestMapAlphaShim(result);
    TestRegisterSpotShim(result);
    TestRefreshShims(result);
    TestRefreshUnwind(result);
    std::printf("%u ABI checks, %u failure(s)\n", result.checks, result.failures);
    return result.failures ? 1 : 0;
}
