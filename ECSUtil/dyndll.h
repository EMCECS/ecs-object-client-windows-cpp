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

#include <afxwin.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <Ws2tcpip.h>
#include <Werapi.h>
#include <WinSvc.h>
#include "dllfunc.h"
#include <MAPI.h>



class CThreadDescription : public CAttachDll
{
public:
	CThreadDescription() noexcept
		: CAttachDll(TEXT("Kernel32.dll"))	// can't specify because it exists in win10 but NOT in Server 2016!
	{
		InitFunction(0, "SetThreadDescription");
		InitFunction(1, "GetThreadDescription");
		DoneInitFunction();
	}

	HRESULT WINAPI SetThreadDescription(
		_In_ HANDLE hThread,
		_In_ PCWSTR lpThreadDescription
	)
	{
		return ((HRESULT(WINAPI *)(HANDLE, PCWSTR))Proc(0))(hThread, lpThreadDescription);
	}


	HRESULT WINAPI GetThreadDescription(
		_In_ HANDLE hThread,
		_Outptr_result_z_ PWSTR * ppszThreadDescription
	)
	{
		return ((HRESULT(WINAPI *)(HANDLE, PWSTR *))Proc(1))(hThread, ppszThreadDescription);
	}
};
