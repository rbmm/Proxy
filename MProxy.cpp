#include "stdafx.h"

_NT_BEGIN

//#define _PRINT_CPP_NAMES_
#include "../inc/asmfunc.h"
#include "MProxy.h"

#if 1

void CheckPoint(PCSTR File, ULONG Line, ULONG iPoint, PCSTR Function)
{
	PNT_TIB tib = (PNT_TIB)NtCurrentTeb();
	PVOID stack = _AddressOfReturnAddress();

	if (tib->StackLimit >= stack || stack > tib->StackBase)
	{
		__debugbreak();
	}

	DbgPrint("%s(%u): #%uC: %s\r\n\t%04x %p-%x\r\n", File, Line, iPoint, Function, 
		GetCurrentThreadId(), tib->StackBase, RtlPointerToOffset(stack, tib->StackBase));
}

#define CP(i) CheckPoint(__FILE__, __LINE__, i, __FUNCTION__)

#else

#defin CP(i)

#endif

EXTERN_C extern PVOID g_ProxyVtable[];

FProxy::FProxy(IUnknown* pUnk, HWND hwnd) : _M_hwnd(hwnd), _M_pUnk(pUnk)
{
	_M_vtable = g_ProxyVtable;
	pUnk->AddRef();
}

struct MiniFiber 
{
	PVOID StackLimit;
	PVOID Stack;
	PVOID StackBase;
};

void __fastcall SwitchToStack( _In_ PVOID lpNewStack, _Out_ PVOID* lpCurStack )ASM_FUNCTION;

VOID SwitchToMiniFiber( _In_ MiniFiber* lpNewFiber, _Out_ MiniFiber* lpCurFiber )
{
	PNT_TIB tib = (PNT_TIB)NtCurrentTeb();
	lpCurFiber->StackBase = tib->StackBase;
	lpCurFiber->StackLimit = tib->StackLimit;
	tib->StackLimit = lpNewFiber->StackLimit;
	tib->StackBase = lpNewFiber->StackBase;
	SwitchToStack(lpNewFiber->Stack, &lpCurFiber->Stack);
}

struct RET_CODE 
{
	union {
		ULONG int3;
		struct {
			UCHAR nop[3];
			UCHAR e8;
			ULONG offset;
		};
	};
};

struct RET_CONTEXT 
{
	PVOID retAddr;
	MiniFiber* ServerFiber;
	MiniFiber* TempFiber;
	MiniFiber* ClientFiber;
};

struct RET_DATA 
{
	RET_CONTEXT* _M_Context;

	static void OnRetStub()ASM_FUNCTION;

	void __fastcall OnRet(void** NewStack);
};

SLIST_HEADER gRetHead;

struct DECLSPEC_ALIGN(__alignof(SLIST_ENTRY)) RET_INFO : public RET_CODE, RET_DATA 
{
	void* operator new(size_t)
	{
		union {
			PSLIST_ENTRY Entry;
			RET_INFO* pri;
		};

		if (Entry = InterlockedPopEntrySList(&gRetHead)) 
		{
			pri->int3 = 0xcccccccc;
			pri->e8 = 0xe8;
			pri->offset = RtlPointerToOffset(static_cast<RET_DATA*>(pri), RET_DATA::OnRetStub);
			return pri;
		}

		return 0;
	}

	void operator delete(void* pv)
	{
		InterlockedPushEntrySList(&gRetHead, (PSLIST_ENTRY)pv);
	}

	RET_INFO(RET_CONTEXT* Context)
	{
		_M_Context = Context;
	}

	RET_INFO() = default;
};

C_ASSERT(sizeof(RET_INFO)==8 + sizeof(PVOID));

// executable!!
#pragma bss_seg(".retinfo")
RET_INFO gRI[PAGE_SIZE / sizeof(RET_INFO)];
#pragma bss_seg()

#pragma comment(linker, "/SECTION:.retinfo,RWE")

void InitRetInfo()
{
	union {
		PVOID pv;
		PSLIST_ENTRY Entry;
		RET_INFO* pri;
	};

	pri = gRI;
	ULONG n = _countof(gRI);

	InitializeSListHead(&gRetHead);
	do 
	{
		InterlockedPushEntrySList(&gRetHead, Entry);
	} while (pri++, --n);
}

struct CSData 
{
	void** stack;
	MiniFiber* TempFiber;
};

struct MF2 
{
	MiniFiber TempFiber;
	MiniFiber ClientFiber;
	UCHAR stack[];

	void* operator new(size_t s, ULONG cb)
	{
		return LocalAlloc(LMEM_FIXED, s + cb);
	}

	void operator delete(void* p)
	{
		LocalFree(p);
	}
};

VOID WINAPI FProxy::FiberProc(FiberData* pfd)
{
	CP(2);
	// #2C: ( client thread, temp fiber stack )
	MiniFiber* ClientFiber = pfd->ClientFiber;
	MiniFiber* TempFiber = pfd->TempFiber;

	CSData cs { pfd->stack, pfd->TempFiber };
	// call server
	SendMessageW(pfd->hwnd, WM_USER, (WPARAM)&cs, (LPARAM)ClientFiber); // goto #3C

	// !! pfd already point to overwritten location
	CP(7);
	// #7C: ( client thread, temp fiber stack, same as in #2C )
	// return to client normal stack
	SwitchToMiniFiber(ClientFiber, TempFiber); // goto #8C

	// never must return here
	__debugbreak();
}

void __fastcall FProxy::ClientCall(void** stack)
{
	CPP_FUNCTION;
	CP(1);
	// #1C: ( client thread, client stack)

	stack[1] = _M_pUnk; // set this pointer

	enum { tempStackSize = 0x1000 };

	if (MF2* temp = new(tempStackSize) MF2)
	{
		FiberData fd = { stack, &temp->TempFiber, &temp->ClientFiber, _M_hwnd };

		void** StackBase = (void**)(~0xF & (ULONG_PTR)&temp->stack[tempStackSize]);
		void** tempStack = StackBase - (6 + sizeof(PVOID));
		
		tempStack[sizeof(PVOID) + 0] = FiberProc;
		tempStack[sizeof(PVOID) + 1] = DbgUserBreakPoint;
		tempStack[sizeof(PVOID) + 2] = &fd;

		temp->TempFiber.StackBase = StackBase;
		temp->TempFiber.StackLimit = temp->stack;
		temp->TempFiber.Stack = tempStack;
		
		SwitchToMiniFiber(fd.TempFiber, fd.ClientFiber); // goto #2C
		CP(4);
		// #4C: ( server thread, client stack)
		// return to #4A
	}
}

void __fastcall RET_DATA::OnRet(void** NewStack)
{
	CPP_FUNCTION;

	CP(5);
	// #5C: ( server thread, client stack  )

	// restore original return address 
	*NewStack = _M_Context->retAddr;

	MiniFiber* ServerFiber = _M_Context->ServerFiber;
	MiniFiber* TempFiber = _M_Context->TempFiber;
	MiniFiber* ClientFiber = _M_Context->ClientFiber;

	delete static_cast<RET_INFO*>(this);

	// return to server stack
	SwitchToMiniFiber(ServerFiber, ClientFiber); // goto #6C
	CP(8);
	// #8C: (client thread, client stack)

	LocalFree(CONTAINING_RECORD(TempFiber, MF2, TempFiber));
	// return to method caller
}

LRESULT WINAPI FProxy::MsgWindowProc(
									 HWND hwnd, 
									 UINT uMsg, 
									 WPARAM wParam, 
									 LPARAM lParam
									 )
{
	switch (uMsg)
	{
	case WM_USER:
		CP(3);
		// #3C: ( server thread, server stack)
		// wParam -> CSData
		// lParam -> Client Fiber

		MiniFiber ServerFiber;
		void** stack = reinterpret_cast<CSData*>(wParam)->stack;

		// save return address from method call 
		RET_CONTEXT rc { *stack, &ServerFiber, reinterpret_cast<CSData*>(wParam)->TempFiber, (MiniFiber*)lParam };

		if (RET_INFO* pri = new RET_INFO(&rc))
		{
			// replace return address from method call
			*stack = &pri->e8;
			// switch to client stack
			SwitchToMiniFiber((MiniFiber*)lParam, &ServerFiber); // goto #4C
			CP(6);
			// #6C: ( server thread, server stack, the same as in #3C )
			// return to #7C
		}
		return 0;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

namespace {
	const WCHAR ClassName[] = L"1F262E55949B48f6B3489B3F653C3FBA";
};

HWND FProxy::CreateMessageWindow()
{
	const static WNDCLASSW wcls = { 0, MsgWindowProc, 0, 0, (HINSTANCE)&__ImageBase, 0, 0, 0, 0, ClassName };
	return RegisterClassW(&wcls) ? CreateWindowExW(0, ClassName, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0) : 0;
}

HWND InitFProxy()
{
	InitRetInfo();
	return FProxy::CreateMessageWindow();
}

void DestroyFProxy(HWND hwnd)
{
	DestroyWindow(hwnd);
	UnregisterClassW(ClassName, (HINSTANCE)&__ImageBase);
}

_NT_END