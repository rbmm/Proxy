.686

.MODEL FLAT

public _g_ProxyVtable

; void __fastcall NT::FProxy::ClientCall(void **)
extern ?ClientCall@FProxy@NT@@AAIXPAPAX@Z : PROC

; void __fastcall NT::RET_DATA::OnRet(void **)
extern ?OnRet@RET_DATA@NT@@QAIXPAPAX@Z : PROC

.code

CommonEntryI proc
	
	REPT 256
	int 3
	int 3
	int 3
	call @@0
	ENDM
	
@@0:
	pop eax
	sub eax,offset CommonEntryI
	shr eax,3
	dec eax
	mov edx,esp
	mov ecx,[esp + 4]                           ; #1A: ( client thread, client stack)
	push eax                                    ; interface index
	call ?ClientCall@FProxy@NT@@AAIXPAPAX@Z
	pop eax                                     
	mov ecx,[esp + 4]                           ; #4A: ( server thread, client stack)
	mov ecx,[ecx]
	jmp dword ptr [ecx + 4*eax]                 ; jmp to original method
	
CommonEntryI endp

; void __fastcall NT::RET_DATA::OnRetStub(void)

; void __stdcall NT::RET_DATA::OnRetStub(void)
?OnRetStub@RET_DATA@NT@@SGXXZ proc
	mov ecx,[esp]                               ; #5A: ( server thread, client stack - return from method )
	mov edx,esp
	push eax                                    ; save return value
	call ?OnRet@RET_DATA@NT@@QAIXPAPAX@Z        ; goto #5C
	                                            ; #8A: (client thread, client stack)
	pop eax                                     ; restore return value
	ret
?OnRetStub@RET_DATA@NT@@SGXXZ endp

; void __fastcall NT::SwitchToStack(void *,void **)
?SwitchToStack@NT@@YIXPAXPAPAX@Z proc
	push ebx
	push edi
	push esi
	push ebp
	mov [edx],esp
	mov esp,ecx
	pop ebp
	pop esi
	pop edi
	pop ebx
	ret
?SwitchToStack@NT@@YIXPAXPAPAX@Z endp

.CONST

	ALIGN 4
_g_ProxyVtable LABEL DWORD
	N = 3 + offset CommonEntryI
	REPT 256
	DD N
	N = N + 8
	ENDM

END