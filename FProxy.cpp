#include "stdafx.h"

_NT_BEGIN

//#define _PRINT_CPP_NAMES_
#include "../inc/asmfunc.h"
#include "FProxy.h"

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

PVOID GetCurrentFiberEx()
{
	if (PVOID Fiber = ConvertThreadToFiberEx(0, 0))
	{
		return Fiber;
	}

	if (GetLastError() == ERROR_ALREADY_FIBER)
	{
		return GetCurrentFiber();
	}

	return 0;
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
	PVOID ServerFiber;
	PVOID TempFiber;
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
	PVOID TempFiber;
};

struct FiberData 
{
	void** stack;
	PVOID TempFiber;
	PVOID ClientFiber;
	HWND hwnd;
};

VOID WINAPI FProxy::FiberProc(PVOID lpParameter)
{
	CP(2);
	// #2C: ( client thread, temp fiber stack )
	PVOID ClientFiber = reinterpret_cast<FiberData*>(lpParameter)->ClientFiber;

	CSData cs { reinterpret_cast<FiberData*>(lpParameter)->stack, reinterpret_cast<FiberData*>(lpParameter)->TempFiber };
	// call server
	SendMessageW(reinterpret_cast<FiberData*>(lpParameter)->hwnd, WM_USER, (WPARAM)&cs, (LPARAM)ClientFiber); // goto #3C

	CP(7);
	// #7C: ( client thread, temp fiber stack, same as in #2C )
	// return to client normal stack
	SwitchToFiber(ClientFiber); // goto #8C

	// never must return here
	__debugbreak();
}

void FProxy::ClientCall(void** stack)
{
	CPP_FUNCTION;
	CP(1);
	// #1C: ( client thread, client stack)

	stack[1] = _M_pUnk; // set this pointer

	FiberData fd;

	if (fd.ClientFiber = GetCurrentFiberEx())
	{
		if (fd.TempFiber = CreateFiberEx(0x1000, 0x1000, 0, FiberProc, &fd))
		{
			fd.hwnd = _M_hwnd;
			fd.stack = stack;
			SwitchToFiber(fd.TempFiber); // goto #2C
			CP(4);
			// #4C: ( server thread, client main fiber stack)
			// return to #4A
		}
	}
}

void RET_DATA::OnRet(void** NewStack)
{
	CPP_FUNCTION;

	CP(5);
	// #5C: ( server thread, client stack  )

	// restore original return address 
	*NewStack = _M_Context->retAddr;

	PVOID ServerFiber = _M_Context->ServerFiber;
	PVOID TempFiber = _M_Context->TempFiber;

	delete static_cast<RET_INFO*>(this);

	// return to server stack
	SwitchToFiber(ServerFiber); // goto #6C
	CP(8);
	// #8C: (client thread, client stack)

	DeleteFiber(TempFiber);
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

		RET_CONTEXT rc;

		if (rc.ServerFiber = GetCurrentFiberEx())
		{
			if (RET_INFO* pri = new RET_INFO(&rc))
			{
				rc.TempFiber = reinterpret_cast<CSData*>(wParam)->TempFiber;
				void** stack = reinterpret_cast<CSData*>(wParam)->stack;
				// save return address from method call 
				rc.retAddr = *stack;
				// replace return address from method call
				*stack = &pri->e8;
				// switch to client stack
				SwitchToFiber((PVOID)lParam); // goto #4C
				CP(6);
				// #6C: ( server thread, server stack, the same as in #3C )
				// return to #7C
			}
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