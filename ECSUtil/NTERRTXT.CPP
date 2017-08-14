/*
 * Copyright (c) 1994 - 2017, EMC Corporation.
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * + Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * + The name of EMC Corporation may not be used to endorse or promote
 *   products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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
	if (hLib != NULL)
		dwFlags |= FORMAT_MESSAGE_FROM_HMODULE;
	if (!::FormatMessage(dwFlags, hLib, dwMsgID, 0, (LPTSTR)&pMsgBuf, 0, NULL))
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

	if (FormatMessageLang(ec, NULL, sMsg))
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
