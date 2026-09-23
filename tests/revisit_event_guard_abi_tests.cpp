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
GuardSnapshot revisitGuardInput{}, revisitGuardSeen{};
uint64_t revisitGuardFlags = 0, revisitGuardCallerStack = 0, revisitGuardStackCount = 0;
void* Sky2NextRevisitScriptStart = nullptr;
void Sky2RevisitScriptStartShim();
void RevisitGuardTrampoline();
void RevisitGuardPoison();
uint64_t RevisitGuardInvoke();
extern const unsigned char RevisitGuardReturnSite;
}
static uintptr_t observed[6]{};
static unsigned called = 0;
extern "C" void Sky2GuardTBoxScriptStart(uintptr_t caller, uintptr_t row, uintptr_t function,
                                         uintptr_t params, uint32_t count, uintptr_t originalRsp) noexcept {
    observed[0] = caller; observed[1] = row; observed[2] = function;
    observed[3] = params; observed[4] = count; observed[5] = originalRsp;
    ++called;
    RevisitGuardPoison();
}
static unsigned failures = 0;
static void Check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}

int main() {
    // CF/PF/AF/ZF/SF/OF 覆盖多种组合；始终保留 bit1，不设置 DF/TF 等控制位。
    for (const uint64_t arithmetic : {uint64_t{0}, uint64_t{0xD5}, uint64_t{0x800}, uint64_t{0x8D5}}) {
        auto* words = reinterpret_cast<uint64_t*>(&revisitGuardInput);
        for (size_t i = 0; i < 9; ++i) words[i] = 0x1020304050607080ull + i;
        for (size_t i = 0; i < sizeof(revisitGuardInput.xmm); ++i)
            reinterpret_cast<uint8_t*>(revisitGuardInput.xmm)[i] = static_cast<uint8_t>(i * 7 + 3);
        revisitGuardFlags = 0x202 | arithmetic;
        Sky2NextRevisitScriptStart = reinterpret_cast<void*>(&RevisitGuardTrampoline);
        const auto returned = RevisitGuardInvoke();
        Check(returned == 0x0123456789ABCDEFull, "native return value preserved");
        Check(!std::memcmp(&revisitGuardInput, &revisitGuardSeen, 9 * sizeof(uint64_t)), "all integer registers preserved");
        Check(!std::memcmp(revisitGuardInput.xmm, revisitGuardSeen.xmm, sizeof(revisitGuardInput.xmm)), "all volatile XMM registers preserved");
        Check((revisitGuardSeen.flags & 0x8D5) == arithmetic, "original arithmetic flags preserved");
        Check(revisitGuardSeen.rsp == revisitGuardCallerStack - 8, "tail jump preserves native entry stack");
        Check(revisitGuardStackCount == 0xA1B2C3D4, "native fifth argument preserved");
        Check(observed[0] == reinterpret_cast<uintptr_t>(&RevisitGuardReturnSite), "helper sees original return address");
        Check(observed[1] == revisitGuardInput.rsi, "helper sees original RSI row");
        Check(observed[2] == revisitGuardInput.r8 && observed[3] == revisitGuardInput.r9, "helper sees original script arguments");
        Check(observed[4] == 0xA1B2C3D4 && observed[5] == revisitGuardCallerStack - 8, "helper sees original count and stack");
    }
    Check(called == 4, "one helper invocation per native call");
    DWORD64 image = 0;
    auto* unwind = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&Sky2RevisitScriptStartShim), &image, nullptr);
    Check(unwind != nullptr, "production bridge has Windows unwind entry");
    if (unwind) {
        const auto* info = reinterpret_cast<const uint8_t*>(image + unwind->UnwindData);
        Check((info[0] & 7) == 1 && (info[3] & 15) == 5, "unwind uses version one and RBP frame");
    }
    return failures ? 1 : 0;
}
