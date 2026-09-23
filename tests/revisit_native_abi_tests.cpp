// 直接测试两个生产 MASM 入口及跳转取消路径，测试替身不运行任何游戏代码。
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

struct NativeSnapshot {
    uint64_t rax, rcx, rdx, r8, r9, r10, r11, rsi, rbp, rsp, flags;
    uint8_t xmm[6][16];
};
static_assert(offsetof(NativeSnapshot, xmm) == 0x58 && sizeof(NativeSnapshot) == 0xB8);
extern "C" {
NativeSnapshot revisitNativeInput{}, revisitNativeAtTrampoline{}, revisitNativeAfter{};
uint64_t revisitNativeFlags = 0, revisitNativeCallerStack = 0, revisitNativeTrampolineCalls = 0;
void* revisitNativeShim = nullptr;
void* Sky2NextRevisitUpdate = nullptr;
void* Sky2NextRevisitJump = nullptr;
void Sky2RevisitUpdateShim();
void Sky2RevisitJumpShim();
void RevisitNativeTrampoline();
void RevisitNativePoison();
uint64_t RevisitNativeInvoke();
extern const unsigned char RevisitNativeReturnSite;
}
static uintptr_t observed[3]{};
static unsigned updateCalls = 0, jumpCalls = 0;
static bool rejectJump = false;
extern "C" void Sky2BeforeRevisitUpdate(uintptr_t minimap) noexcept {
    observed[0] = minimap;
    ++updateCalls;
    RevisitNativePoison();
}
extern "C" bool Sky2BeforeRevisitJump(uintptr_t field, uint32_t target, uintptr_t caller) noexcept {
    observed[0] = field; observed[1] = target; observed[2] = caller;
    ++jumpCalls;
    RevisitNativePoison();
    return rejectJump;
}
static unsigned failures = 0;
static void Check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
static void CheckUnwind(void (*entry)()) {
    DWORD64 image = 0;
    auto* unwind = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entry), &image, nullptr);
    Check(unwind != nullptr, "native bridge has unwind metadata");
    if (unwind) {
        const auto* info = reinterpret_cast<const uint8_t*>(image + unwind->UnwindData);
        Check((info[0] & 7) == 1 && (info[3] & 15) == 5, "native bridge has RBP frame metadata");
    }
}

int main() {
    // 每组标志分别验证 Update、Jump 允许、Jump 拒绝；检查一轮前总是清空采样结果。
    for (const uint64_t arithmetic : {uint64_t{0}, uint64_t{0xD5}, uint64_t{0x800}, uint64_t{0x8D5}}) {
        for (unsigned mode = 0; mode < 3; ++mode) {
            const bool jump = mode != 0, reject = mode == 2;
            auto* words = reinterpret_cast<uint64_t*>(&revisitNativeInput);
            for (size_t i = 0; i < 9; ++i) words[i] = 0x1020304050607080ull + i;
            for (size_t i = 0; i < sizeof(revisitNativeInput.xmm); ++i)
                reinterpret_cast<uint8_t*>(revisitNativeInput.xmm)[i] = static_cast<uint8_t>(i * 7 + 3);
            revisitNativeAtTrampoline = {}; revisitNativeAfter = {};
            revisitNativeTrampolineCalls = 0;
            revisitNativeFlags = 0x202 | arithmetic;
            rejectJump = reject;
            revisitNativeShim = reinterpret_cast<void*>(jump ? &Sky2RevisitJumpShim : &Sky2RevisitUpdateShim);
            Sky2NextRevisitUpdate = Sky2NextRevisitJump = reinterpret_cast<void*>(&RevisitNativeTrampoline);
            const auto returned = RevisitNativeInvoke();
            Check(returned == (reject ? 0 : 0x0123456789ABCDEFull), "native return or cancellation false preserved");
            Check(revisitNativeTrampolineCalls == (reject ? 0u : 1u), "only admitted paths call original exactly once");
            Check(!std::memcmp(&revisitNativeInput.rcx, &revisitNativeAfter.rcx, 8 * sizeof(uint64_t)), "caller integer registers preserved on both branches");
            Check(!std::memcmp(revisitNativeInput.xmm, revisitNativeAfter.xmm, sizeof(revisitNativeInput.xmm)), "caller XMM registers preserved on both branches");
            Check((revisitNativeAfter.flags & 0x8D5) == arithmetic, "original flags preserved on both branches");
            Check(revisitNativeAfter.rsp == revisitNativeCallerStack, "caller stack restored on both branches");
            Check(observed[0] == revisitNativeInput.rcx, "helper receives original context RCX");
            if (jump) {
                Check(observed[1] == static_cast<uint32_t>(revisitNativeInput.rdx), "jump helper receives target EDX");
                Check(observed[2] == reinterpret_cast<uintptr_t>(&RevisitNativeReturnSite), "jump helper receives native return address in R8");
            }
            if (!reject) {
                Check(!std::memcmp(&revisitNativeInput, &revisitNativeAtTrampoline, 9 * sizeof(uint64_t)), "original sees all original integer registers");
                Check(revisitNativeAtTrampoline.rsp == revisitNativeCallerStack - 8, "tail jump preserves original entry stack");
                Check((revisitNativeAtTrampoline.flags & 0x8D5) == arithmetic, "original sees unchanged flags");
                Check(!std::memcmp(revisitNativeInput.xmm, revisitNativeAtTrampoline.xmm, sizeof(revisitNativeInput.xmm)), "original sees unchanged XMM registers");
            }
        }
    }
    Check(updateCalls == 4 && jumpCalls == 8, "each invocation enters one expected helper");
    CheckUnwind(&Sky2RevisitUpdateShim);
    CheckUnwind(&Sky2RevisitJumpShim);
    return failures ? 1 : 0;
}
