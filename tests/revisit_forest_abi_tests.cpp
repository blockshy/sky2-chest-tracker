// 直接链接生产 MASM 桥，独立验证参数捕获、RFLAGS、易失寄存器和尾跳返回；不需游戏。
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

struct GuardSnapshot {
    uint64_t rax, rcx, rdx, r8, r9, r10, r11, rsi, rbp, rsp, flags;
    uint8_t xmm[6][16];
};
static_assert(offsetof(GuardSnapshot, xmm) == 0x58);
static_assert(sizeof(GuardSnapshot) == 0xB8);
extern "C" {
GuardSnapshot forestGuardInput{}, forestGuardSeen{};
uint64_t forestGuardFlags = 0, forestGuardCallerStack = 0, forestGuardStackCount = 0;
void* Sky2NextForestEnable = nullptr;
void Sky2ForestEnableShim();
void ForestGuardTrampoline();
void ForestGuardPoison();
uint64_t ForestGuardInvoke();
extern const unsigned char ForestGuardReturnSite;
}
static uintptr_t observed = 0;
static unsigned called = 0;
extern "C" void Sky2ForestBeforeEnable(uintptr_t context) noexcept {
    observed = context;
    ++called;
    ForestGuardPoison();
}
static unsigned failures = 0;
static void Check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}

int main() {
    // CF/PF/AF/ZF/SF/OF 覆盖多种组合；始终保留 bit1，不设置 DF/TF 等控制位。
    for (const uint64_t arithmetic : {uint64_t{0}, uint64_t{0xD5}, uint64_t{0x800}, uint64_t{0x8D5}}) {
        auto* words = reinterpret_cast<uint64_t*>(&forestGuardInput);
        for (size_t i = 0; i < 9; ++i) words[i] = 0x1020304050607080ull + i;
        for (size_t i = 0; i < sizeof(forestGuardInput.xmm); ++i)
            reinterpret_cast<uint8_t*>(forestGuardInput.xmm)[i] = static_cast<uint8_t>(i * 7 + 3);
        forestGuardFlags = 0x202 | arithmetic;
        Sky2NextForestEnable = reinterpret_cast<void*>(&ForestGuardTrampoline);
        const auto returned = ForestGuardInvoke();
        Check(returned == 0x0123456789ABCDEFull, "native return value preserved");
        Check(!std::memcmp(&forestGuardInput, &forestGuardSeen, 9 * sizeof(uint64_t)), "all integer registers preserved");
        Check(!std::memcmp(forestGuardInput.xmm, forestGuardSeen.xmm, sizeof(forestGuardInput.xmm)), "all volatile XMM registers preserved");
        Check((forestGuardSeen.flags & 0x8D5) == arithmetic, "original arithmetic flags preserved");
        Check(forestGuardSeen.rsp == forestGuardCallerStack - 8, "tail jump preserves native entry stack");
        Check(forestGuardStackCount == 0xA1B2C3D4, "native fifth argument preserved");
        Check(observed == forestGuardInput.rdx, "helper sees original VM context from RDX");
    }
    Check(called == 4, "one helper invocation per native call");
    DWORD64 image = 0;
    auto* unwind = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&Sky2ForestEnableShim), &image, nullptr);
    Check(unwind != nullptr, "production bridge has Windows unwind entry");
    if (unwind) {
        const auto* info = reinterpret_cast<const uint8_t*>(image + unwind->UnwindData);
        Check((info[0] & 7) == 1 && (info[3] & 15) == 5, "unwind uses version one and RBP frame");
    }
    return failures ? 1 : 0;
}
