; 回访入口保留游戏内部调用约定：C++只能读取/校验/提交临时结果，原函数始终
; 在自己的原始栈布局和完整寄存器状态下继续执行。不能使用普通C++ detour替代。
OPTION CASEMAP:NONE
EXTERN Sky2BeforeRevisitUpdate:PROC
EXTERN Sky2BeforeRevisitJump:PROC
EXTERN Sky2NextRevisitUpdate:QWORD
EXTERN Sky2NextRevisitJump:QWORD
PUBLIC Sky2RevisitUpdateShim
PUBLIC Sky2RevisitJumpShim

.code
RESTORE_REVISIT_CONTEXT MACRO
    movdqu xmm0, [rsp+70h]
    movdqu xmm1, [rsp+80h]
    movdqu xmm2, [rsp+90h]
    movdqu xmm3, [rsp+0A0h]
    movdqu xmm4, [rsp+0B0h]
    movdqu xmm5, [rsp+0C0h]
    mov rax, [rsp+30h]
    mov rcx, [rsp+38h]
    mov rdx, [rsp+40h]
    mov r8, [rsp+48h]
    mov r9, [rsp+50h]
    mov r10, [rsp+58h]
    mov r11, [rsp+60h]
    push QWORD PTR [rsp+0D8h]
    popfq
    lea rsp, [rbp+0E0h]
    pop rbp
ENDM

REVISIT_BEFORE_BRIDGE MACRO bridgeName:REQ, nextFunction:REQ, helper:REQ, isJump:REQ
LOCAL skipNative
bridgeName PROC FRAME
    push rbp
    .pushreg rbp
    pushfq
    .allocstack 8
    sub rsp, 0D8h
    .allocstack 0D8h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov [rsp+30h], rax
    mov [rsp+38h], rcx
    mov [rsp+40h], rdx
    mov [rsp+48h], r8
    mov [rsp+50h], r9
    mov [rsp+58h], r10
    mov [rsp+60h], r11
    movdqu [rsp+70h], xmm0
    movdqu [rsp+80h], xmm1
    movdqu [rsp+90h], xmm2
    movdqu [rsp+0A0h], xmm3
    movdqu [rsp+0B0h], xmm4
    movdqu [rsp+0C0h], xmm5
    IF isJump
        ; 第三参数是原生调用者返回地址；不依赖C++ helper自己的_ReturnAddress。
        mov r8, [rsp+0E8h]
    ENDIF
    call helper
    IF isJump
        test al, al
        jnz skipNative
    ENDIF
    RESTORE_REVISIT_CONTEXT
    jmp QWORD PTR [nextFunction]
    IF isJump
skipNative:
        ; 被撤销的回访返回bool false，其他易失寄存器和原标志仍完全保留。
        mov QWORD PTR [rsp+30h], 0
        RESTORE_REVISIT_CONTEXT
        ret
    ENDIF
bridgeName ENDP
ENDM

REVISIT_BEFORE_BRIDGE Sky2RevisitUpdateShim, Sky2NextRevisitUpdate, Sky2BeforeRevisitUpdate, 0
REVISIT_BEFORE_BRIDGE Sky2RevisitJumpShim, Sky2NextRevisitJump, Sky2BeforeRevisitJump, 1
END
