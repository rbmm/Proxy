#pragma once

struct MiniFiber;

class FProxy
{
	void** _M_vtable;
	IUnknown* _M_pUnk;
	HWND _M_hwnd;
	LONG _M_dwRefCount = 1;

	~FProxy()
	{
		_M_pUnk->Release();
	}

	struct FiberData 
	{
		void** stack;
		MiniFiber* TempFiber;
		MiniFiber* ClientFiber;
		HWND hwnd;
	};

	static VOID WINAPI FiberProc(FiberData* pfd);

	void __fastcall ClientCall(void** stack);

	static LRESULT WINAPI MsgWindowProc(
		HWND hwnd, 
		UINT uMsg, 
		WPARAM wParam, 
		LPARAM lParam 
		);
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

	FProxy(IUnknown* pUnk, HWND hwnd);

	static HWND CreateMessageWindow();
};

HWND InitFProxy();
void DestroyFProxy(HWND hwnd);
