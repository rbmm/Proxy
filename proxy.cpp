#include "stdafx.h"
#include "resource.h"

_NT_BEGIN
#include "../UiConsole/GuiLog.h"
#include "../UiConsole/wlog.h"

PCWSTR WINAPI GetWindowName()
{
	return L"Proxy Demo";
}

PCWSTR WINAPI GetIconName()
{
	return MAKEINTRESOURCEW(IDI_ICON1);
}

HINSTANCE WINAPI GetIconModule()
{
	return (HINSTANCE)&__ImageBase;
}

void PrintInterfaceInfo(const IID *rgclsidProviders);

void PrintCA(PCSTR Name, PVOID pv = 0, PCSTR txt = "");

void PPL_Test();

extern const volatile UCHAR guz = 0;

MIDL_INTERFACE("7C936584-3368-4a57-A58B-C40EA12B646F") IDemoItf : public IUnknown 
{
	virtual HRESULT STDMETHODCALLTYPE SendReceive(
		_Out_ BSTR* ppszOut, 
		_Out_ ULONG* pdwThreadId, 
		_Out_ PCWSTR pszIn, 
		_In_ ULONG dwThreadId
		) = 0;
};

class CDemoItf : public IDemoItf
{
	HWND _hwndLog;
	LONG _dwRefCount = 1;

	virtual ~CDemoItf()
	{
		PrintCA(__FUNCTION__, this);
	}

	virtual HRESULT STDMETHODCALLTYPE SendReceive(
		_Out_ BSTR* ppszOut, 
		_Out_ ULONG* pdwThreadId, 
		_Out_ PCWSTR pszIn, 
		_In_ ULONG dwThreadId
		)
	{
		PrintCA(__FUNCTION__, this);

		DbgPrint("%s<%p>: %S from %04x\r\n", __FUNCTION__, this, pszIn, dwThreadId);

		alloca(0x8000);
		MSG msg;
		while( PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
		{
			DispatchMessageW(&msg);
		}
		//MessageBoxW(0,pszIn,0,MB_ICONWARNING);
		WNoBufLog log(_hwndLog);
		log(L"%04x> %S<%p>: %s from %04x\r\n", GetCurrentThreadId(), __FUNCTION__, this, pszIn, dwThreadId);

		*ppszOut = SysAllocString(L"[Server Data]");
		*pdwThreadId = GetCurrentThreadId();
		return RPC_NT_UUID_LOCAL_ONLY;
	}

	virtual HRESULT STDMETHODCALLTYPE QueryInterface( 
		/* [in] */ REFIID riid,
		/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject)
	{
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDemoItf))
		{
			*ppvObject = static_cast<IDemoItf*>(this);
			AddRef();
			return S_OK;
		}

		PrintInterfaceInfo(&riid);
		DbgPrint("\r\n");

		*ppvObject = 0;
		return E_NOINTERFACE;
	}

	ULONG CheckRef(PCSTR FuncName, ULONG dwRef)
	{
		if (!dwRef)
		{
			delete this;
		}
		CHAR buf[16];
		sprintf_s(buf, _countof(buf), "[%x]", dwRef);
		PrintCA(FuncName, this, buf);
		return dwRef;
	}

	//////////////////////////////////////////////////////////////////////////
public:
	CDemoItf(_In_ HWND hwndLog) : _hwndLog(hwndLog)
	{
		PrintCA(__FUNCTION__, this);
	}

	virtual ULONG STDMETHODCALLTYPE AddRef( )
	{
		return CheckRef(__FUNCTION__, InterlockedIncrementNoFence(&_dwRefCount));
	}

	virtual ULONG STDMETHODCALLTYPE Release( )
	{
		return CheckRef(__FUNCTION__, InterlockedDecrement(&_dwRefCount));
	}
};

#include "MProxy.h"

void DoWork(_In_ HWND hwndLog, _In_ IDemoItf* pItf)
{
	ULONG dwThreadId;
	BSTR pszOut;
	PrintCA(__FUNCTION__, pItf, "before call...");
	HRESULT hr = pItf->SendReceive(&pszOut, &dwThreadId, L"[Client Data]", GetCurrentThreadId());
	PrintCA(__FUNCTION__, pItf, "after call");
	DbgPrint("hr = %x: \"%S\" from %04x\n", hr, pszOut, dwThreadId);
	
	WNoBufLog log(hwndLog);
	log(L"%04x> hr = %x: \"%s\" from %04x\r\n", GetCurrentThreadId(), hr, pszOut, dwThreadId)[hr];

	SysFreeString(pszOut);
}

struct WorkCtx 
{
	FProxy* proxy;
	HWND hwndLog;
};

ULONG CALLBACK Worker(PVOID Param)
{
	FProxy* proxy = reinterpret_cast<WorkCtx*>(Param)->proxy;
	HWND hwndLog = reinterpret_cast<WorkCtx*>(Param)->hwndLog;

	delete reinterpret_cast<WorkCtx*>(Param);

	if (0 <= CoInitializeEx(0, COINIT_MULTITHREADED|COINIT_DISABLE_OLE1DDE))
	{
		DoWork(hwndLog, (IDemoItf*)proxy);

		CoUninitialize();
	}

	proxy->Release();

	return 0;
}

BOOL Start(_In_ HWND hwndLog, _In_ HWND hwndProxy)
{
	if (CDemoItf* p = new CDemoItf(hwndLog))
	{
		if (FProxy* proxy = new FProxy(p, hwndProxy))
		{
			if (WorkCtx* pwc = new WorkCtx)
			{
				pwc->hwndLog = hwndLog, pwc->proxy = proxy;

				if (QueueUserWorkItem(Worker, pwc, 0))
				{
					return TRUE;
				}

				delete pwc;
			}

			proxy->Release();
		}

		p->Release();
	}

	return FALSE;
}

BOOL WINAPI BeginML(_In_ HWND hwndLog, _Out_ void** pContext)
{
	if (0 <= CoInitializeEx(0, COINIT_DISABLE_OLE1DDE|COINIT_APARTMENTTHREADED))
	{
		PPL_Test();

		if (HWND hwndProxy = InitFProxy())
		{
			if (Start(hwndLog, hwndProxy))
			{
				*pContext = hwndProxy;
				return TRUE;
			}

			DestroyFProxy(hwndProxy);
		}
		CoUninitialize();
	}

	return FALSE;
}

void WINAPI EndML(_In_ void* Context)
{
	DestroyFProxy((HWND)Context);
	CoUninitialize();
}

_NT_END