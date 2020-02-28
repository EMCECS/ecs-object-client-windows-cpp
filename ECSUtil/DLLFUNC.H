/*
 * Copyright (c) 2017 - 2020, Dell Technologies, Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 * http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */


#pragma once

#include <afxmt.h>
#include <memory>
#include <vector>

struct DLL_FUNC
{
	LPCSTR szFuncName;
	PVOID pFuncAddr;
};

typedef DLL_FUNC *PDLL_FUNC;

// class alternative to InitFunctPtrs
class CAttachDll
{
private:
	class CAttachDllCS
	{
		friend CAttachDll;
		CCriticalSection csAttachDll;

		CAttachDllCS& operator =(const CAttachDllCS &Rec)
		{
			return(*this);
		}
	};
	CString sDllName;
	CString sErrorProcs;
	HINSTANCE hMod;
	bool bDontFreeLibrary;
	DWORD dwLastError;
	vector<FARPROC> DllList;
	bool bDoneInitFunction;

protected:
	mutable CAttachDllCS CS;

protected:
	void InitFunction(DWORD i, LPCSTR pszFuncName)
	{
		CSingleLock lock(&CS.csAttachDll, true);
		FARPROC pProc;

		ASSERT(pszFuncName != NULL);
		if (hMod == NULL)
			return;
#pragma warning(suppress: 6387)
		pProc = GetProcAddress(hMod, pszFuncName);
		if (pProc == NULL)
		{
			dwLastError = GetLastError();
			if (!sErrorProcs.IsEmpty())
				sErrorProcs += TEXT(" ,");
			sErrorProcs += pszFuncName;
		}
		else
		{
			while (DllList.size() <= i)
				DllList.push_back(pProc);
			DllList[i] = pProc;
		}
	}

	void DoneInitFunction(void)
	{
		bDoneInitFunction = true;
	}

	FARPROC Proc(DWORD i) const
	{
		ASSERT(IsInitialized());
		ASSERT(i < DllList.size());
		ASSERT(DllList[i] != NULL);
		return DllList[i];
	}

public:
	CAttachDll(LPCTSTR pszDllName = NULL) noexcept
	{
		CSingleLock lock(&CS.csAttachDll, true);
		bDoneInitFunction = false;
		dwLastError = ERROR_INVALID_FUNCTION;
		bDontFreeLibrary = false;
		hMod = NULL;
		if (pszDllName != NULL)
			LoadDll(pszDllName);
	}

	virtual ~CAttachDll()
	{
		CSingleLock lock(&CS.csAttachDll, true);
		if ((hMod != NULL) && !bDontFreeLibrary)
		{
#ifndef _AFXDLL
			VERIFY(FreeLibrary(hMod));
#else
			(void)AfxFreeLibrary(hMod);
#endif
			hMod = NULL;
		}
	}

	void LoadDll(LPCTSTR pszDllName)
	{
		CSingleLock lock(&CS.csAttachDll, true);
		ASSERT(pszDllName != NULL);
		sDllName = pszDllName;
		if (sDllName.CompareNoCase(TEXT("KERNEL32.DLL")) == 0)
		{
			hMod = GetModuleHandle(sDllName);
			bDontFreeLibrary = true;
		}
		else
		{
#ifndef _AFXDLL
			hMod = ::LoadLibrary(sDllName);
#else
			hMod = AfxLoadLibrary(sDllName);
#endif
			bDontFreeLibrary = false;
		}
		if (hMod == NULL)
			dwLastError = GetLastError();
		else
			dwLastError = ERROR_SUCCESS;
	}

	bool IsInitialized(void) const
	{
		CSingleLock lock(&CS.csAttachDll, true);
		bool bInitialized = bDoneInitFunction && (hMod != NULL) && sErrorProcs.IsEmpty();
		return bInitialized;
	}

	DWORD GetError(CString *psErrorProcs = NULL)
	{
		CSingleLock lock(&CS.csAttachDll, true);
		if (psErrorProcs != NULL)
			*psErrorProcs = sErrorProcs;
		return dwLastError;
	}

	HINSTANCE DllModuleHandle(void) const
	{
		return hMod;
	}
};

//
// how to use:
// first derive a class from CAttachDll
// the following sample is used to attach to the NetServerComputer... API
//
//	class CNetApiDll : public CAttachDll
//	{
//	public:
//		CNetApiDll()
//			: CAttachDll(OS_TYPE_WIN2000, TEXT("NETAPI32.DLL"))
//		{
//			InitFunction(0, TEXT("NetServerComputerNameAdd"));
//			InitFunction(1, TEXT("NetServerComputerNameDel"));
//			DoneInitFunction();
//		}
//	
//		NET_API_STATUS NET_API_FUNCTION NetServerComputerNameAdd(
//			IN LPWSTR ServerName         OPTIONAL,
//			IN LPWSTR EmulatedDomainName OPTIONAL,
//			IN LPWSTR EmulatedServerName)
//		{
//			return ((NET_API_STATUS (NET_API_FUNCTION *)(LPWSTR, LPWSTR, LPWSTR))Proc(0))(
//				ServerName, EmulatedDomainName, EmulatedServerName);
//		}
//	
//		NET_API_STATUS NET_API_FUNCTION NetServerComputerNameDel(
//			IN LPWSTR ServerName        OPTIONAL,
//			IN LPWSTR EmulatedServerName)
//		{
//			return ((NET_API_STATUS (NET_API_FUNCTION *)(LPWSTR, LPWSTR))Proc(1))(ServerName, EmulatedServerName);
//		}
//	};
//
// define a variable of type CNetApiDll:
//	
//	EXTERN CNetApiDll NetApi;
//
// to use:
//
//		if (!NetApi.IsInitialized())
//			return NetApi.GetError();
//		dwError = NetApi.NetServerComputerNameAdd(NULL, NULL, (LPWSTR)(LPCTSTR)pRec->sName);
//	
