#include "stdafx.h"

_NT_BEGIN

PCSTR GetApartmentType(_In_ PSTR buf, _In_ ULONG cb)
{
	APTTYPE AptType;
	APTTYPEQUALIFIER AptQualifier;
	HRESULT hr = CoGetApartmentType(&AptType, &AptQualifier);

	if (0 > hr)
	{
		return 0 < sprintf_s(buf, cb, "[%x]", hr) ? buf : "?";
	}

	CHAR tmp1[16], tmp2[16];
	PCSTR pszAptType, pszAptQualifier;

	switch (AptQualifier)
	{
	case APTTYPEQUALIFIER_NONE: 
		pszAptQualifier = "NONE";
		break;
	case APTTYPEQUALIFIER_IMPLICIT_MTA: 
		pszAptQualifier = "IMPLICIT_MTA";
		break;
	case APTTYPEQUALIFIER_NA_ON_MTA: 
		pszAptQualifier = "NA_ON_MTA";
		break;
	case APTTYPEQUALIFIER_NA_ON_STA: 
		pszAptQualifier = "NA_ON_STA";
		break;
	case APTTYPEQUALIFIER_NA_ON_IMPLICIT_MTA: 
		pszAptQualifier = "NA_ON_IMPLICIT_MTA";
		break;
	case APTTYPEQUALIFIER_NA_ON_MAINSTA: 
		pszAptQualifier = "NA_ON_MAINSTA";
		break;
	case APTTYPEQUALIFIER_APPLICATION_STA: 
		pszAptQualifier = "APPLICATION_STA";
		break;
	case APTTYPEQUALIFIER_RESERVED_1: 
		pszAptQualifier = "RESERVED_1";
		break;
	default:
		if (0 < sprintf_s(tmp2, _countof(tmp2), "%x", AptQualifier))
		{
			pszAptQualifier = tmp2;
		}
		else
		{
			pszAptQualifier = "?";
		}
	}

	switch (AptType)
	{
	case APTTYPE_CURRENT: 
		pszAptType = "CURRENT";
		break;
	case APTTYPE_STA: 
		pszAptType = "STA    ";
		break;
	case APTTYPE_MTA: 
		pszAptType = "MTA    ";
		break;
	case APTTYPE_NA: 
		pszAptType = "NA     ";
		break;
	case APTTYPE_MAINSTA: 
		pszAptType = "MAINSTA";
		break;
	default:
		if (0 < sprintf_s(tmp1, _countof(tmp1), "%x", AptType))
		{
			pszAptType = tmp1;
		}
		else
		{
			pszAptType = "?";
		}
	}

	return 0 < sprintf_s(buf, cb, "%s [ %s ]", pszAptType, pszAptQualifier) ? buf : "?";
}

BOOL ifRegSz(PKEY_VALUE_PARTIAL_INFORMATION pkvpi)
{
	switch (pkvpi->Type)
	{
	case REG_SZ:
	case REG_EXPAND_SZ:
		ULONG DataLength = pkvpi->DataLength;
		return DataLength && !(DataLength & (sizeof(WCHAR) - 1));
	}
	return FALSE;
}

extern volatile const UCHAR guz;

void DumpValue(HANDLE hKey, PCUNICODE_STRING ValueName)
{
	NTSTATUS status;
	union {
		PVOID buf;
		PKEY_VALUE_PARTIAL_INFORMATION pkvpi;
	};
	PVOID stack = alloca(guz);
	ULONG cb = 0, rcb = 0x80;
	do 
	{
		if (cb < rcb)
		{
			cb = RtlPointerToOffset(buf = alloca(rcb - cb), stack);
		}

		status = ZwQueryValueKey(hKey, ValueName, KeyValuePartialInformation, buf, cb, &rcb);

	} while (STATUS_BUFFER_OVERFLOW == status);

	if (0 <= status && ifRegSz(pkvpi))
	{
		DbgPrint(" \"%.*S\"", pkvpi->DataLength / sizeof(WCHAR), pkvpi->Data);
	}
}

void PrintInterfaceInfo(const IID *rgclsidProviders)
{
	DbgPrint("{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
		rgclsidProviders->Data1, rgclsidProviders->Data2, rgclsidProviders->Data3,
		rgclsidProviders->Data4[0], rgclsidProviders->Data4[1], rgclsidProviders->Data4[2], rgclsidProviders->Data4[3], 
		rgclsidProviders->Data4[4], rgclsidProviders->Data4[5], rgclsidProviders->Data4[6], rgclsidProviders->Data4[7]);

	WCHAR buf[_countof("\\REGISTRY\\MACHINE\\SOFTWARE\\Classes\\Interface\\{12345678-1234-1234-1234-1234567890AB}")];
	if (0 < swprintf_s(buf, _countof(buf),
		L"\\REGISTRY\\MACHINE\\SOFTWARE\\Classes\\Interface\\{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", 
		rgclsidProviders->Data1, rgclsidProviders->Data2, rgclsidProviders->Data3,
		rgclsidProviders->Data4[0], rgclsidProviders->Data4[1], rgclsidProviders->Data4[2], rgclsidProviders->Data4[3], 
		rgclsidProviders->Data4[4], rgclsidProviders->Data4[5], rgclsidProviders->Data4[6], rgclsidProviders->Data4[7]))
	{
		UNICODE_STRING ObjectName;
		OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };

		RtlInitUnicodeString(&ObjectName, buf);

		if (0 <= ZwOpenKey(&oa.RootDirectory, KEY_READ, &oa))
		{
			static const UNICODE_STRING Empty = {};

			DumpValue(oa.RootDirectory, &Empty);

			STATIC_UNICODE_STRING_(ProxyStubClsid32);

			HANDLE hKey;
			oa.ObjectName = const_cast<PUNICODE_STRING>(&ProxyStubClsid32);
			NTSTATUS status = ZwOpenKey(&hKey, KEY_READ, &oa);

			NtClose(oa.RootDirectory);

			if (0 <= status)
			{
				DumpValue(hKey, &Empty);
				NtClose(hKey);
			}
		}
	}
}

void PrintCA(PCSTR Name, PVOID pv = 0, PCSTR txt = "")
{
	CHAR buf[64];
	PNT_TIB tib = (PNT_TIB)NtCurrentTeb();
	DbgPrint("\t%04x %p-%x %s > %s<%p> (%s)\r\n", GetCurrentThreadId(), 
		tib->StackBase, RtlPointerToOffset(_AddressOfReturnAddress(), tib->StackBase), 
		GetApartmentType(buf, _countof(buf)), Name, pv, txt);
}

_NT_END