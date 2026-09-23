; Cmd_map_03 的透明前置桥。只让 C++ 过滤器检查临时启用参数，不跳过原生 builtin。
; 游戏内部调用可能保留超出常规 C++ ABI 的易失寄存器假设，故显式保存所有这些值。
OPTION CASEMAP:NONE
EXTERN Sky2ForestBeforeEnable:PROC
EXTERN Sky2NextForestEnable:QWORD
PUBLIC Sky2ForestEnableShim
.code
Sky2ForestEnableShim PROC FRAME
    push rbp
    .pushreg rbp
    pushfq
    .allocstack 8
    sub rsp, 0E8h
    .allocstack 0E8h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov [rsp+38h], rax
    mov [rsp+40h], rcx
    mov [rsp+48h], rdx
    mov [rsp+50h], r8
    mov [rsp+58h], r9
    mov [rsp+60h], r10
    mov [rsp+68h], r11
    movdqu [rsp+80h], xmm0
    movdqu [rsp+90h], xmm1
    movdqu [rsp+0A0h], xmm2
    movdqu [rsp+0B0h], xmm3
    movdqu [rsp+0C0h], xmm4
    movdqu [rsp+0D0h], xmm5
    ; 原始 RDX 是 VM context；其余参数和调用者栈始终保持原样。
    mov rcx, rdx
    call Sky2ForestBeforeEnable
    movdqu xmm0, [rsp+80h]
    movdqu xmm1, [rsp+90h]
    movdqu xmm2, [rsp+0A0h]
    movdqu xmm3, [rsp+0B0h]
    movdqu xmm4, [rsp+0C0h]
    movdqu xmm5, [rsp+0D0h]
    mov rax, [rsp+38h]
    mov rcx, [rsp+40h]
    mov rdx, [rsp+48h]
    mov r8, [rsp+50h]
    mov r9, [rsp+58h]
    mov r10, [rsp+60h]
    mov r11, [rsp+68h]
    push QWORD PTR [rsp+0E8h]
    popfq
    ; 用不改变标志的合法 Windows x64 尾声恢复原入口栈，然后尾跳原生 trampoline。
    lea rsp, [rbp+0F0h]
    pop rbp
    jmp QWORD PTR [Sky2NextForestEnable]
Sky2ForestEnableShim ENDP
END
