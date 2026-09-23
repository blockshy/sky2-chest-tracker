; 独立测试调用器：原生桥前后采样寄存器，测试数据均属于测试进程。
OPTION CASEMAP:NONE
EXTERN Sky2ForestEnableShim:PROC
EXTERN forestGuardInput:BYTE
EXTERN forestGuardSeen:BYTE
EXTERN forestGuardFlags:QWORD
EXTERN forestGuardCallerStack:QWORD
EXTERN forestGuardStackCount:QWORD
PUBLIC ForestGuardInvoke
PUBLIC ForestGuardReturnSite
PUBLIC ForestGuardTrampoline
PUBLIC ForestGuardPoison

CAPTURE_GUARD MACRO target:REQ
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
ForestGuardInvoke PROC FRAME
    push rbp
    .pushreg rbp
    push rsi
    .pushreg rsi
    sub rsp, 58h
    .allocstack 58h
    .endprolog
    mov forestGuardCallerStack, rsp
    mov DWORD PTR [rsp+20h], 0A1B2C3D4h
    movdqu xmm0, XMMWORD PTR [forestGuardInput+58h]
    movdqu xmm1, XMMWORD PTR [forestGuardInput+68h]
    movdqu xmm2, XMMWORD PTR [forestGuardInput+78h]
    movdqu xmm3, XMMWORD PTR [forestGuardInput+88h]
    movdqu xmm4, XMMWORD PTR [forestGuardInput+98h]
    movdqu xmm5, XMMWORD PTR [forestGuardInput+0A8h]
    mov rax, QWORD PTR [forestGuardInput+00h]
    mov rcx, QWORD PTR [forestGuardInput+08h]
    mov rdx, QWORD PTR [forestGuardInput+10h]
    mov r8, QWORD PTR [forestGuardInput+18h]
    mov r9, QWORD PTR [forestGuardInput+20h]
    mov r10, QWORD PTR [forestGuardInput+28h]
    mov r11, QWORD PTR [forestGuardInput+30h]
    mov rsi, QWORD PTR [forestGuardInput+38h]
    mov rbp, QWORD PTR [forestGuardInput+40h]
    push forestGuardFlags
    popfq
    call Sky2ForestEnableShim
ForestGuardReturnSite LABEL BYTE
    ; 不改原生桥返回值，用合法尾声还原调用器自己借用的非易失寄存器。
    lea rsp, [rsp+58h]
    pop rsi
    pop rbp
    ret
ForestGuardInvoke ENDP

ForestGuardTrampoline PROC
    CAPTURE_GUARD forestGuardSeen
    mov eax, DWORD PTR [rsp+28h]
    mov forestGuardStackCount, rax
    mov rax, 0123456789ABCDEFh
    ret
ForestGuardTrampoline ENDP

; 辅助回调可合法破坏所有易失寄存器，桥必须恢复而不能依赖编译器恰好未使用它们。
ForestGuardPoison PROC
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
ForestGuardPoison ENDP
END
