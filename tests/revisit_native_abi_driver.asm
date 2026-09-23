; 回访换图桥的独立 ABI 测试调用器，不加载游戏或提交真实传送。
OPTION CASEMAP:NONE
EXTERN revisitNativeShim:QWORD
EXTERN revisitNativeInput:BYTE
EXTERN revisitNativeAtTrampoline:BYTE
EXTERN revisitNativeAfter:BYTE
EXTERN revisitNativeFlags:QWORD
EXTERN revisitNativeCallerStack:QWORD
EXTERN revisitNativeTrampolineCalls:QWORD
PUBLIC RevisitNativeInvoke
PUBLIC RevisitNativeReturnSite
PUBLIC RevisitNativeTrampoline
PUBLIC RevisitNativePoison

CAPTURE_NATIVE MACRO target:REQ
    mov QWORD PTR [target+00h], rax
    mov QWORD PTR [target+08h], rcx
    mov QWORD PTR [target+10h], rdx
    mov QWORD PTR [target+18h], r8
    mov QWORD PTR [target+20h], r9
    mov QWORD PTR [target+28h], r10
    mov QWORD PTR [target+30h], r11
    mov QWORD PTR [target+38h], rsi
    mov QWORD PTR [target+40h], rbp
    mov QWORD PTR [target+48h], rsp
    pushfq
    pop QWORD PTR [target+50h]
    movdqu XMMWORD PTR [target+58h], xmm0
    movdqu XMMWORD PTR [target+68h], xmm1
    movdqu XMMWORD PTR [target+78h], xmm2
    movdqu XMMWORD PTR [target+88h], xmm3
    movdqu XMMWORD PTR [target+98h], xmm4
    movdqu XMMWORD PTR [target+0A8h], xmm5
ENDM

.code
RevisitNativeInvoke PROC FRAME
    push rbp
    .pushreg rbp
    push rsi
    .pushreg rsi
    sub rsp, 58h
    .allocstack 58h
    .endprolog
    mov revisitNativeCallerStack, rsp
    movdqu xmm0, XMMWORD PTR [revisitNativeInput+58h]
    movdqu xmm1, XMMWORD PTR [revisitNativeInput+68h]
    movdqu xmm2, XMMWORD PTR [revisitNativeInput+78h]
    movdqu xmm3, XMMWORD PTR [revisitNativeInput+88h]
    movdqu xmm4, XMMWORD PTR [revisitNativeInput+98h]
    movdqu xmm5, XMMWORD PTR [revisitNativeInput+0A8h]
    mov rax, QWORD PTR [revisitNativeInput+00h]
    mov rcx, QWORD PTR [revisitNativeInput+08h]
    mov rdx, QWORD PTR [revisitNativeInput+10h]
    mov r8, QWORD PTR [revisitNativeInput+18h]
    mov r9, QWORD PTR [revisitNativeInput+20h]
    mov r10, QWORD PTR [revisitNativeInput+28h]
    mov r11, QWORD PTR [revisitNativeInput+30h]
    mov rsi, QWORD PTR [revisitNativeInput+38h]
    mov rbp, QWORD PTR [revisitNativeInput+40h]
    push revisitNativeFlags
    popfq
    call QWORD PTR [revisitNativeShim]
RevisitNativeReturnSite LABEL BYTE
    ; 返回后立即采样：拒绝路径必须返回 false，同时保留其它寄存器和算术标志。
    CAPTURE_NATIVE revisitNativeAfter
    lea rsp, [rsp+58h]
    pop rsi
    pop rbp
    ret
RevisitNativeInvoke ENDP

RevisitNativeTrampoline PROC
    CAPTURE_NATIVE revisitNativeAtTrampoline
    ; 不通过 INC/XOR 改动标志，确保测试能区分桥破坏标志与测试替身自己改动。
    mov rax, 1
    mov revisitNativeTrampolineCalls, rax
    mov rax, 0123456789ABCDEFh
    ret
RevisitNativeTrampoline ENDP

RevisitNativePoison PROC
    mov rax, -1
    mov rcx, -1
    mov rdx, -1
    mov r8, -1
    mov r9, -1
    mov r10, -1
    mov r11, -1
    pcmpeqb xmm0, xmm0
    pcmpeqb xmm1, xmm1
    pcmpeqb xmm2, xmm2
    pcmpeqb xmm3, xmm3
    pcmpeqb xmm4, xmm4
    pcmpeqb xmm5, xmm5
    test rax, rax
    ret
RevisitNativePoison ENDP
END
