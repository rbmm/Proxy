
public g_ProxyVtable

; void __cdecl NT::RET_DATA::OnRet(void **)
extern ?OnRet@RET_DATA@NT@@QEAAXPEAPEAX@Z : PROC

; void __cdecl NT::FProxy::ClientCall(void **)
extern ?ClientCall@FProxy@NT@@AEAAXPEAPEAX@Z : PROC

.code

CommonEntryI proc
	
	REPT 256
	int 3
	int 3
	int 3
	call @@0
	ENDM
	
@@0:
	pop rax
	mov r10,offset CommonEntryI
	sub rax,r10
	shr eax,3
	dec eax
	mov [rsp+32],r9                             ; arg3
	mov [rsp+24],r8                             ; arg2
	mov [rsp+16],rdx                            ; arg1
	mov rdx,rsp
	push rax                                    ; interface index
	sub rsp,20h                                 ; #1A: ( client thread, client stack)
	call ?ClientCall@FProxy@NT@@AEAAXPEAPEAX@Z
	add rsp,20h                                 ; #4A: ( server thread, client stack)
	pop rax
	mov rcx,[rsp+8]                             ; this
	mov rdx,[rsp+16]                            ; agr1
	mov r8,[rsp+24]                             ; arg2
	mov r9,[rsp+32]                             ; arg3
	mov r10,[rcx]
	jmp qword ptr [r10 + 8*rax]                 ; jmp to original method
	
CommonEntryI endp

; void __cdecl NT::RET_DATA::OnRetStub(void)

?OnRetStub@RET_DATA@NT@@SAXXZ proc
	mov rcx,[rsp]                               ; #5A: ( server thread, client stack - return from method )
	mov rdx,rsp
	push rax                                    ; save return value
	sub rsp,20h
	call ?OnRet@RET_DATA@NT@@QEAAXPEAPEAX@Z     ; goto #5C
	add rsp,20h                                 ; #8A: (client thread, client stack)
	pop rax                                     ; restore return value
	ret
?OnRetStub@RET_DATA@NT@@SAXXZ endp

; void __cdecl NT::SwitchToStack(void *,void **)
?SwitchToStack@NT@@YAXPEAXPEAPEAX@Z proc
	push rbx
	push rdi
	push rsi
	push rbp
	push r12
	push r13
	push r14
	push r15
	mov [rdx],rsp
	mov rsp,rcx
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rsi
	pop rdi
	pop rbx
	mov rcx,[rsp + 10h]
	ret
?SwitchToStack@NT@@YAXPEAXPEAPEAX@Z endp

.CONST

	ALIGN 8
g_ProxyVtable: 
	N = 3 + offset CommonEntryI
	REPT 256
	DQ N
	N = N + SIZEOF QWORD
	ENDM

END