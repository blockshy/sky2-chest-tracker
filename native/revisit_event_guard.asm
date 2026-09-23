; 通用脚本启动的透明前置桥。只向保护函数提供原调用点证据，再尾跳原生入口。
; 只有返回地址为 2E176C 时，RSI 才被解释为 t_tbox 行；其它脚本完全保留。
OPTION CASEMAP:NONE
EXTERN Sky2GuardTBoxScriptStart:PROC
EXTERN Sky2NextRevisitScriptStart:QWORD
PUBLIC Sky2RevisitScriptStartShim

.code
Sky2RevisitScriptStartShim PROC FRAME
    ; 在 SUB 前保存原始 RFLAGS；RBP 帧指针让恢复标志后的 LEA/POP/JMP 成为
    ; Windows x64 可识别的 epilogue。总占用仍为 F8，C++ 调用时满足 16 字节对齐。
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
    ; 原生启动函数的第五参数 count 位于原 RSP+28；参数6提供原入口栈指针，
    ; 用于核对 TBoxProcess 参数恰在原调用方局部槽 +44，而不是任意游戏地址。
    mov eax, DWORD PTR [rsp+120h]
    mov [rsp+20h], eax
    lea rax, [rsp+0F8h]
    mov [rsp+28h], rax
    mov rcx, [rsp+0F8h]
    mov rdx, rsi
    ; R8、R9 已是原函数名和参数数组；全部非易失寄存器由 C++ ABI 保留。
    call Sky2GuardTBoxScriptStart
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
    lea rsp, [rbp+0F0h]
    pop rbp
    ; 原生函数会自己读取参数、启动协程并返回。桥不接管返回值、物品或收尾。
    jmp QWORD PTR [Sky2NextRevisitScriptStart]
Sky2RevisitScriptStartShim ENDP
END
