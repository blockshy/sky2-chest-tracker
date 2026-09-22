; Windows x64 测试调用器：用已知值初始化易失寄存器，并在 trampoline 及返回处采样。
; 不向非易失寄存器写入测试值，因此调用器本身也遵守标准 ABI，能够被 C++ 正常调用。
; 本文件必须与生产 native/exploration_shims.asm 一起链接，不复制生产实现到测试中。
OPTION CASEMAP:NONE
EXTERN Sky2MapAlphaShim:PROC
EXTERN Sky2RegisterSpotShim:PROC
EXTERN abiInput:BYTE
EXTERN abiTrampoline:BYTE
EXTERN abiAfter:BYTE
EXTERN abiCanaries:QWORD
EXTERN abiTravelOriginalInput:BYTE
EXTERN abiTravelOutput:BYTE
EXTERN abiTravelAfter:BYTE
EXTERN abiTravelOriginalCalls:QWORD
EXTERN abiTravelCallerStack:QWORD
EXTERN abiTravelWantedFlags:QWORD
EXTERN abiTravelOriginalFlags:QWORD
EXTERN abiTravelAfterFlags:QWORD
EXTERN abiRefreshShim:QWORD
EXTERN abiRefreshTrampoline:BYTE
EXTERN abiRefreshAfter:BYTE
EXTERN abiRefreshCallerStack:QWORD
EXTERN abiRefreshWantedFlags:QWORD
EXTERN abiRefreshInputFlags:QWORD
EXTERN abiRefreshTrampolineFlags:QWORD
EXTERN abiRefreshAfterFlags:QWORD
EXTERN abiRefreshTrampolineKind:QWORD
EXTERN abiRefreshTrampolineCalls:QWORD
EXTERN abiRefreshTrampolineCaller:QWORD
PUBLIC AbiInvokeMapShim
PUBLIC AbiMapReturnSite
PUBLIC AbiCaptureTrampoline
PUBLIC AbiPoisonVolatiles
PUBLIC AbiInvokeTravelShim
PUBLIC AbiOriginalRegisterSpot
PUBLIC AbiInvokeRefreshShim
PUBLIC AbiRefreshReturnSite
PUBLIC AbiCaptureMapBrowseTrampoline
PUBLIC AbiCaptureSpotListTrampoline
PUBLIC AbiCaptureAreaListTrampoline
PUBLIC AbiCaptureBuildTravelTrampoline

; 快照偏移对应 C++ RegisterSnapshot，C++ 的 static_assert 会阻止布局被无意修改。
; 直接写入全局快照，采样过程中不用临时寄存器，以免掩盖原始寄存器遭到破坏的问题。
CAPTURE MACRO destination
    mov QWORD PTR [destination+00h], rax
    mov QWORD PTR [destination+08h], rcx
    mov QWORD PTR [destination+10h], rdx
    mov QWORD PTR [destination+18h], r8
    mov QWORD PTR [destination+20h], r9
    mov QWORD PTR [destination+28h], r10
    mov QWORD PTR [destination+30h], r11
    mov QWORD PTR [destination+38h], rsp
    movdqu XMMWORD PTR [destination+40h], xmm0
    movdqu XMMWORD PTR [destination+50h], xmm1
    movdqu XMMWORD PTR [destination+60h], xmm2
    movdqu XMMWORD PTR [destination+70h], xmm3
    movdqu XMMWORD PTR [destination+80h], xmm4
    movdqu XMMWORD PTR [destination+90h], xmm5
ENDM

; 装载已知寄存器内容；与 CAPTURE 一样只使用 MOV，不改变已经准备好的 RFLAGS。
LOAD_VOLATILES MACRO source
    mov rax, QWORD PTR [source+00h]
    mov rcx, QWORD PTR [source+08h]
    mov rdx, QWORD PTR [source+10h]
    mov r8, QWORD PTR [source+18h]
    mov r9, QWORD PTR [source+20h]
    mov r10, QWORD PTR [source+28h]
    mov r11, QWORD PTR [source+30h]
    movdqu xmm0, XMMWORD PTR [source+40h]
    movdqu xmm1, XMMWORD PTR [source+50h]
    movdqu xmm2, XMMWORD PTR [source+60h]
    movdqu xmm3, XMMWORD PTR [source+70h]
    movdqu xmm4, XMMWORD PTR [source+80h]
    movdqu xmm5, XMMWORD PTR [source+90h]
ENDM

.code
AbiInvokeMapShim PROC FRAME
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    ; 函数入口 RSP 为 16n+8，减去 38h 后在调用前变为 16 字节对齐。
    ; 预留完整 shadow space；在其后设置保护值检查桥接函数是否写出自己的栈帧。
    mov rax, 1122334455667788h
    mov [rsp+20h], rax
    mov rax, 8877665544332211h
    mov [rsp+28h], rax
    LOAD_VOLATILES abiInput
    call Sky2MapAlphaShim
AbiMapReturnSite LABEL NEAR
    CAPTURE abiAfter
    mov rax, [rsp+20h]
    mov [abiCanaries], rax
    mov rax, [rsp+28h]
    mov [abiCanaries+8], rax
    add rsp, 38h
    ret
AbiInvokeMapShim ENDP

AbiCaptureTrampoline PROC
    ; 尾跳目的地必须看到原始参数和仅被替换低位的 alpha；返回时不再污染状态。
    CAPTURE abiTrampoline
    ret
AbiCaptureTrampoline ENDP

AbiInvokeTravelShim PROC FRAME
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov rax, 1122334455667788h
    mov [rsp+20h], rax
    mov rax, 8877665544332211h
    mov [rsp+28h], rax
    mov [abiTravelCallerStack], rsp
    LOAD_VOLATILES abiInput
    call Sky2RegisterSpotShim
    ; 必须在任何 ADD / CMP 等改标志指令之前采样；PUSHFQ / POP / MOV 均不改标志。
    pushfq
    pop QWORD PTR [abiTravelAfterFlags]
    CAPTURE abiTravelAfter
    mov rax, [rsp+20h]
    mov [abiCanaries], rax
    mov rax, [rsp+28h]
    mov [abiCanaries+8], rax
    add rsp, 38h
    ret
AbiInvokeTravelShim ENDP

AbiOriginalRegisterSpot PROC
    CAPTURE abiTravelOriginalInput
    inc QWORD PTR [abiTravelOriginalCalls]
    ; 原函数可合法使用调用者准备的全部 32 字节 shadow space。
    ; 全部写入可发现桥错误地把原始参数备份放在 shadow space 内的情况。
    mov [rsp+8], rax
    mov [rsp+10h], rax
    mov [rsp+18h], rax
    mov [rsp+20h], rax
    ; 仅替换 CF/PF/AF/ZF/SF/OF。保留其他状态位，尤其不能打开方向标志或单步标志。
    pushfq
    pop rax
    and rax, -2262             ; ~0x8D5 的有符号立即数。
    or rax, [abiTravelWantedFlags]
    push rax
    popfq
    LOAD_VOLATILES abiTravelOutput
    pushfq
    pop QWORD PTR [abiTravelOriginalFlags]
    ret
AbiOriginalRegisterSpot ENDP

; 同一调用器通过函数指针遍历四个正式入口，输入寄存器装载之后不再使用临时寄存器。
; 在调用前设置并采样全部常规算术标志，检查桥是否错误地保存了 SUB 后的标志。
AbiInvokeRefreshShim PROC FRAME
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov rax, 1122334455667788h
    mov [rsp+20h], rax
    mov rax, 8877665544332211h
    mov [rsp+28h], rax
    mov [abiRefreshCallerStack], rsp
    pushfq
    pop rax
    and rax, -2262
    or rax, [abiRefreshWantedFlags]
    push rax
    popfq
    LOAD_VOLATILES abiInput
    pushfq
    pop QWORD PTR [abiRefreshInputFlags]
    call QWORD PTR [abiRefreshShim]
AbiRefreshReturnSite LABEL NEAR
    pushfq
    pop QWORD PTR [abiRefreshAfterFlags]
    CAPTURE abiRefreshAfter
    mov rax, [rsp+20h]
    mov [abiCanaries], rax
    mov rax, [rsp+28h]
    mov [abiCanaries+8], rax
    add rsp, 38h
    ret
AbiInvokeRefreshShim ENDP

; 使用不同目标编号核对四个桥各自跳入正确 trampoline；采样期间不污染返回寄存器。
; 原函数桩主动写满调用者 shadow space，防止桥错误地把寄存器备份留在那里。
CAPTURE_REFRESH_TRAMPOLINE MACRO functionName:REQ, kindValue:REQ
functionName PROC
    CAPTURE abiRefreshTrampoline
    ; 直接采样原函数入口栈中的真实返回地址。暂借RAX前先保存，确保后续返回寄存器
    ; 及RFLAGS仍与入口一致，并独立证明helper获得的并非桥内部某次CALL的返回地址。
    push rax
    mov rax, [rsp+8]
    mov [abiRefreshTrampolineCaller], rax
    pop rax
    pushfq
    pop QWORD PTR [abiRefreshTrampolineFlags]
    mov QWORD PTR [abiRefreshTrampolineKind], kindValue
    mov [rsp+8], rax
    mov [rsp+10h], rax
    mov [rsp+18h], rax
    mov [rsp+20h], rax
    pushfq
    inc QWORD PTR [abiRefreshTrampolineCalls]
    popfq
    ret
functionName ENDP
ENDM

CAPTURE_REFRESH_TRAMPOLINE AbiCaptureMapBrowseTrampoline, 1
CAPTURE_REFRESH_TRAMPOLINE AbiCaptureSpotListTrampoline, 2
CAPTURE_REFRESH_TRAMPOLINE AbiCaptureAreaListTrampoline, 3
CAPTURE_REFRESH_TRAMPOLINE AbiCaptureBuildTravelTrampoline, 4

AbiPoisonVolatiles PROC
    ; 模拟普通 C++ 辅助函数可合法破坏的全部易失整数寄存器与 XMM0 至 XMM5。
    ; 返回值由 C++ 测试桩另外装入 XMM0；桥必须自行恢复其他上下文。
    xor eax, eax
    stc                       ; 主动改变算术标志，不能因辅助函数碰巧未修改标志而假通过。
    mov rax, 0DEAD000000000001h
    mov rcx, 0DEAD000000000002h
    mov rdx, 0DEAD000000000003h
    mov r8, 0DEAD000000000004h
    mov r9, 0DEAD000000000005h
    mov r10, 0DEAD000000000006h
    mov r11, 0DEAD000000000007h
    pcmpeqd xmm0, xmm0
    pcmpeqd xmm1, xmm1
    pcmpeqd xmm2, xmm2
    pcmpeqd xmm3, xmm3
    pcmpeqd xmm4, xmm4
    pcmpeqd xmm5, xmm5
    ret
AbiPoisonVolatiles ENDP
END
