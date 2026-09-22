; 原生地图内部调用使用了编译器推导的寄存器保留约定，不能直接跳进普通 C++ 回调。
; 特别是区块循环在返回后继续使用 R9 / R10；这里保存全部易失寄存器，再调用标准 ABI。
; 仅替换浮点 alpha 的低 32 位，其他参数和寄存器保持原值，最后尾跳转原生 trampoline。
OPTION CASEMAP:NONE
EXTERN Sky2MapAlpha:PROC
EXTERN Sky2NextMapAlpha:QWORD
EXTERN Sky2AfterRegisterSpot:PROC
EXTERN Sky2NextRegisterSpot:QWORD
EXTERN Sky2BeforeMapRefresh:PROC
EXTERN Sky2NextMapBrowse:QWORD
EXTERN Sky2NextSpotList:QWORD
EXTERN Sky2NextAreaList:QWORD
EXTERN Sky2BeforeBuildTravel:PROC
EXTERN Sky2NextBuildTravel:QWORD
PUBLIC Sky2MapAlphaShim
PUBLIC Sky2RegisterSpotShim
PUBLIC Sky2MapBrowseShim
PUBLIC Sky2SpotListShim
PUBLIC Sky2AreaListShim
PUBLIC Sky2BuildTravelShim

.code
Sky2MapAlphaShim PROC FRAME
    sub rsp, 0E8h
    .allocstack 0E8h
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
    ; 第五参数为原始返回地址，第六参数为调用点的当前区块（R9）。
    ; C++ 必须先验证返回地址、数组范围和节点匹配，不能把递归调用中的 R9 当区块。
    mov rax, [rsp+0E8h]
    mov [rsp+20h], rax
    mov [rsp+28h], r9
    call Sky2MapAlpha
    movss DWORD PTR [rsp+0A0h], xmm0
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
    add rsp, 0E8h
    jmp QWORD PTR [Sky2NextMapAlpha]
Sky2MapAlphaShim ENDP

; 旧登记桥仅保留用于独立 ABI 回归，生产不安装此挂钩，也不在逐点登记时修改菜单。
; 测试中先完整执行原函数，再调用回调，以核对原生返回后的寄存器保留约定。
; 保存“原函数返回后的”寄存器和标志，而非入口值：内部调用者可能依赖原函数的输出状态。
; 原函数没有栈上传入参数，D8/E0 为本跳板自己的原始参数备份，不与 shadow space 重叠。
Sky2RegisterSpotShim PROC FRAME
    sub rsp, 108h
    .allocstack 108h
    .endprolog
    mov [rsp+0D8h], rcx
    mov [rsp+0E0h], rdx
    call QWORD PTR [Sky2NextRegisterSpot]
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
    pushfq
    pop rax
    mov [rsp+0D0h], rax
    mov rcx, [rsp+0D8h]
    mov rdx, [rsp+0E0h]
    call Sky2AfterRegisterSpot
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
    push QWORD PTR [rsp+0D0h]
    popfq
    ; LEA 不改变刚恢复的标志位；不能在这里使用 ADD。
    lea rsp, [rsp+108h]
    ret
Sky2RegisterSpotShim ENDP

; 地图浏览与显示构建入口共用前置桥：只在原生调用线程插入辅助，再继续原函数。
; 必须在 SUB 改变算术标志之前 PUSHFQ，且为这 8 字节栈空间登记展开信息。
; 入口 RSP=16n+8，保存 RBP、RFLAGS 后减 D8h，调用 C++ 时保持 16 字节对齐。
; RBP 提供规范帧指针，使恢复标志后还能通过合法的 LEA / POP / JMP epilogue 退栈；
; 不能把 POPFQ 放进 epilogue，否则 Windows 在该指令附近无法可靠识别展开过程。
; 两个分支共享完全相同的上下文恢复。分支判断必须放在调用本宏之前，不能在恢复
; RFLAGS 后再 TEST helper 返回值；否则虽然寄存器正确，原始算术标志仍会遭到破坏。
RESTORE_REFRESH_CONTEXT MACRO
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
    ; 后续必须紧跟 RET 或允许的 JMP，形成 Windows x64 可识别的完整 epilogue。
    lea rsp, [rbp+0E0h]
    pop rbp
ENDM

MAP_REFRESH_BRIDGE MACRO bridgeName:REQ, nextFunction:REQ, beforeFunction:REQ, captureReturn:REQ
LOCAL skipOriginal
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
    ; RCX 在保存期间保持原值：浏览入口是 menu，构建入口是 manager；各自交给指定
    ; helper，不从渲染线程传递游戏指针，也不以旧的 menu 类型错误解释 manager。
    IF captureReturn
        ; 原始调用者返回地址位于入口栈顶；本帧保存 RBP/RFLAGS 并分配 D8h 后位于
        ; RSP+E8h。仅向构建helper增加第二参数，用于拒绝没有重算剧情状态的调用源。
        ; 原始RDX已经保存在+40h，尾跳原函数前仍须完整恢复，不能泄漏这个新增参数。
        mov rdx, [rsp+0E8h]
    ENDIF
    call beforeFunction
    ; helper 的 bool 返回值位于 AL。成功刷新或已请求安全关闭后，跳过本帧状态处理，
    ; 防止仍然按下的确认键作用于刚刚变化的选中项；false 才继续原来的状态函数。
    test al, al
    jnz skipOriginal
    RESTORE_REFRESH_CONTEXT
    jmp QWORD PTR [nextFunction]
skipOriginal:
    RESTORE_REFRESH_CONTEXT
    ret
bridgeName ENDP
ENDM

MAP_REFRESH_BRIDGE Sky2MapBrowseShim, Sky2NextMapBrowse, Sky2BeforeMapRefresh, 0
MAP_REFRESH_BRIDGE Sky2SpotListShim, Sky2NextSpotList, Sky2BeforeMapRefresh, 0
MAP_REFRESH_BRIDGE Sky2AreaListShim, Sky2NextAreaList, Sky2BeforeMapRefresh, 0
; 构建 helper 在生产实现中始终返回 false：它仅在原生登记和最终灰态完成后评估
; 传送规则，绝不跳过 BuildDisplay。复用桥的恢复和展开路径，保证 manager 及全部
; 内部调用约定保持不变；ABI 测试仍覆盖宏的两个分支，防止未来修改破坏通用桥。
MAP_REFRESH_BRIDGE Sky2BuildTravelShim, Sky2NextBuildTravel, Sky2BeforeBuildTravel, 1
END
