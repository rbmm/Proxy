#include "stdafx.h"
#include <ctxtcall.h>

_NT_BEGIN
//This IID is exported by uuid.lib too
static GUID const IID_ICallbackWithNoReentrancyToApplicationSTA = {
	0x0A299774, 0x3E4E, 0xFC42, {0x1D, 0x9D, 0x72, 0xCE, 0xE1, 0x05, 0xCA, 0x57}
};

void PrintCA(PCSTR Name, PVOID pv = 0, PCSTR txt = "");

class __declspec(novtable) _Threadpool_chore
{
	PTP_WORK _M_work = 0;
	HMODULE _M_hmod = 0;
	LONG _M_dwRefCount = 1;

	static void CALLBACK _Task_scheduler_callback(PTP_CALLBACK_INSTANCE pci, PVOID _Args, PTP_WORK)  
	{
		FreeLibraryWhenCallbackReturns(pci, reinterpret_cast<_Threadpool_chore*>(_Args)->_M_hmod);
		reinterpret_cast<_Threadpool_chore*>(_Args)->schedule();
		reinterpret_cast<_Threadpool_chore*>(_Args)->Release();
	}

protected:

	virtual ~_Threadpool_chore()
	{
		if (_M_work)
		{
			CloseThreadpoolWork(_M_work);
		}
	}

	virtual void schedule() = 0;

public:

	void AddRef()
	{
		InterlockedIncrementNoFence(&_M_dwRefCount);
	}

	void Release()
	{
		if (!InterlockedDecrement(&_M_dwRefCount))
		{
			delete this;
		}
	}

	ULONG _Create(_In_opt_ PTP_CALLBACK_ENVIRON pcbe = 0)
	{
		if (_M_work)
		{
			__debugbreak();
		}

		if (PTP_WORK pwk = CreateThreadpoolWork(_Task_scheduler_callback, this, pcbe))
		{
			_M_work = pwk;
			return NOERROR;
		}

		return GetLastError();
	}

	ULONG _Schedule()
	{
		// Adds a reference to the DLL with the code to execute on async; the callback will
		// FreeLibraryWhenCallbackReturns this DLL once it starts running.
		HMODULE hmod;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<PCWSTR>(_Task_scheduler_callback), &hmod))
		{
			PVOID _hmod = InterlockedCompareExchangePointer((void**)&_M_hmod, hmod, 0);

			if (!_hmod || _hmod == hmod)
			{
				AddRef();
				SubmitThreadpoolWork(_M_work);
				return NOERROR;
			}

			FreeLibrary(hmod);

			return ERROR_NOT_SAME_OBJECT;
		}

		return GetLastError();
	}
};

class __declspec(novtable) _ContextCallback : public _Threadpool_chore
{
	IContextCallback *_M_pContextCallback = 0;

	static HRESULT CALLBACK _PPLTaskContextCallbackBridge(ComCallData *pParam)
	{
		PrintCA(__FUNCTION__);
		return reinterpret_cast<_ContextCallback*>(pParam->pUserDefined)->schedule_in_context();
	}

protected:

	virtual void schedule()
	{
		PrintCA(__FUNCTION__, this);

		if (_M_pContextCallback)
		{
			ComCallData callData = { 0, 0, this };

			HRESULT hr = _M_pContextCallback->ContextCallback(_PPLTaskContextCallbackBridge, 
				&callData, IID_ICallbackWithNoReentrancyToApplicationSTA, 5, 0);

			if (0 > hr) 
			{
				ULONG_PTR arg = (ULONG_PTR)"Context callback failed.";
				RaiseException(hr, 0, 1, &arg);
			}
		}
		else
		{
			schedule_in_context();
		}
	}

	virtual ~_ContextCallback()
	{
		if (_M_pContextCallback)
		{
			_M_pContextCallback->Release();
		}
	}

	virtual HRESULT schedule_in_context() = 0;

public:

	void _Capture() 
	{
		PrintCA(__FUNCTION__);

		if (0 > CoGetObjectContext(IID_PPV_ARGS(&_M_pContextCallback))) 
		{
			_M_pContextCallback = 0;
		}
	}
};

class MyDemo : public _ContextCallback
{
	HWND _M_hwnd;
	IAgileReference* _M_AgileStream = 0;
	ULONG _M_cb = 0;

	virtual ~MyDemo()
	{
		PrintCA(__FUNCTION__, this);
		PostMessageW(_M_hwnd, WM_QUIT, 0, 0);
	}

	virtual HRESULT schedule_in_context()
	{
		PrintCA(__FUNCTION__, this);

		IStream* pStm;
		if (0 <= _M_AgileStream->Resolve(IID_PPV_ARGS(&pStm)))
		{
			if (ULONG cb = _M_cb)
			{
				PSTR psz = (PSTR)alloca(cb);

				if (0 <= pStm->Read(psz, cb, &cb))
				{
					DbgPrint("%.*s\n", cb, psz);
				}
			}

			pStm->Release();
		}

		return S_OK;
	}

	virtual void schedule()
	{
		PrintCA(__FUNCTION__, this);

		IStream* pStm;

		if (0 <= CreateStreamOnHGlobal(0, TRUE, &pStm))
		{
			char buf[64];
			int cb = sprintf_s(buf, _countof(buf), "data written in %x context\n", GetCurrentThreadId());

			if (0 < cb)
			{
				if (0 <= pStm->Write(buf, cb, &_M_cb))
				{
					::LARGE_INTEGER dlibMove = {};
					if (0 <= pStm->Seek(dlibMove, STREAM_SEEK_SET, 0))
					{
						if (0 <= RoGetAgileReference(AGILEREFERENCE_DEFAULT, __uuidof(IStream), pStm, &_M_AgileStream))
						{
							__super::schedule();

							_M_AgileStream->Release();
						}
					}
				}
			}

			pStm->Release();
		}
	}

public:

	MyDemo(HWND hwnd) : _M_hwnd(hwnd)
	{
		PrintCA(__FUNCTION__, this);
	}
};

struct MessageLoop
{
	HWND _M_hwnd = 0;

	inline static const WCHAR My[]= L"{452A5BE4-F789-411e-ABCE-ACA97183B302}";

public:

	~MessageLoop()
	{
		if (_M_hwnd)
		{
			DestroyWindow(_M_hwnd);
			UnregisterClassW(My, (HINSTANCE)&__ImageBase);
		}
	}

	HWND Init()
	{
		const static WNDCLASSW wcls = { 0, DefWindowProcW, 0, 0, (HINSTANCE)&__ImageBase, 0, 0, 0, 0, My };

		if (RegisterClassW(&wcls))
		{
			if (HWND hwnd = CreateWindowExW(0, My, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0))
			{
				_M_hwnd = hwnd;
				return hwnd;
			}
		}

		return 0;
	}

	void Run()
	{
		MSG Msg;
		DbgPrint("%x> Enter message Loop\n", GetCurrentThreadId());
		while (GetMessageW(&Msg, 0, 0, 0))
		{
			DispatchMessageW(&Msg);
		}
		DbgPrint("%x> Exit message Loop\n", GetCurrentThreadId());
	}
};

void PPL_Test(_In_ HWND hwnd, _In_opt_ PTP_CALLBACK_ENVIRON pcbe = 0)
{
	if (MyDemo* demo = new MyDemo(hwnd))
	{
		if (NOERROR == demo->_Create(pcbe))
		{
			demo->_Capture();
			demo->_Schedule();
		}

		demo->Release();
	}
}

ULONG WINAPI ActivateMTA(HANDLE hEvent)
{
	HRESULT hr = CoInitializeEx(0, COINIT_DISABLE_OLE1DDE|COINIT_MULTITHREADED);
	PrintCA(__FUNCTION__, (PVOID)hr);

	return SetEvent(hEvent);
}

void PPL_Test()
{
	PrintCA(__FUNCTION__);
	MessageLoop ml;
	if (HWND hwnd = ml.Init())
	{
		if (HANDLE hEvent = CreateEvent(0, FALSE, FALSE, 0))
		{
			if (QueueUserWorkItem(ActivateMTA, hEvent, 0))
			{
				WaitForSingleObject(hEvent, INFINITE);
			}

			NtClose(hEvent);

			TP_CALLBACK_ENVIRON_V3 cbe = { 3 };

			if (cbe.Pool = CreateThreadpool(0))
			{
				cbe.CallbackPriority = TP_CALLBACK_PRIORITY_LOW;
				cbe.Size = sizeof(TP_CALLBACK_ENVIRON);
				SetThreadpoolThreadMaximum(cbe.Pool, 1);

				PPL_Test(hwnd, &cbe);
				ml.Run();
			}
		}
	}
}

_NT_END