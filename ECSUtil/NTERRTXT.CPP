/*
 * Copyright (c) 2017 - 2021, Dell Technologies, Inc. All Rights Reserved.
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

#include "stdafx.h"
#include <wbemcli.h>
#include <adserr.h>
#include <vss.h>
#include <vswriter.h>
#include <esent.h>
#include <list>

#include "fmtnum.h"
#include "nterrtxt.h"
#include "libhandle.h"

namespace ecs_sdk
{


LPCTSTR MsgDllListInit[] = {
	_T("NTDLL.DLL"),
	_T("WinHttp.dll"),
	_T("vsstrace.dll"),
	_T("activeds.dll"),
	_T("msxml6r.dll"),
	_T("wininet.dll")
};

bool FormatMessageLang(DWORD dwMsgID, HMODULE hLib, CString& sMsgText)
{
	LPTSTR pMsgBuf;
	DWORD dwFlags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER;
	if (hLib != nullptr)
		dwFlags |= FORMAT_MESSAGE_FROM_HMODULE;
	if (!::FormatMessage(dwFlags, hLib, dwMsgID, 0, (LPTSTR)&pMsgBuf, 0, nullptr))
		return false;
	// strip trailing new line characters
	int Len = lstrlen(pMsgBuf);
	while (Len && (pMsgBuf[Len-1] == TEXT('\n') ||
				pMsgBuf[Len-1] == TEXT('\r')))
		pMsgBuf[--Len] = NUL;
	sMsgText = FmtNum(dwMsgID) + TEXT(" - ") + pMsgBuf;
	// Free the buffer allocated by the system.
	LocalFree(pMsgBuf);
	return true;
}

bool GetNTSTATUSText(NTSTATUS NTStatusCode, CString& sMsgText)
{
	for (UINT i=0 ; i<_countof(MsgDllListInit) ; i++)
	{
		CLibraryHandle hLib;
		hLib.LoadLibrary(MsgDllListInit[i], true);
		if (hLib.IfOpen())
		{
			if (FormatMessageLang(NTStatusCode, hLib, sMsgText))
				return true;
			hLib.Close();
		}
	}
	return false;
}

CString GetNTErrorText(DWORD ec)
{
	CString sMsg;

	if (FormatMessageLang(ec, nullptr, sMsg))
		return sMsg;
	if (GetNTSTATUSText(ec, sMsg))
		return sMsg;

	return sMsg = FmtNum(ec) + TEXT(" - ");
}


CString GetNTLastErrorText(void)
{
	return (LPCTSTR)GetNTErrorText(GetLastError());
}

CString GetUnixErrorText(errno_t err)
{
	TCHAR ErrorText[1024];
	(void)_tcserror_s(ErrorText, err);
	return ErrorText;
}

} // end namespace ecs_sdk
