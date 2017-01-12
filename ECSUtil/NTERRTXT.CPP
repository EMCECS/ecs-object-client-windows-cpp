//
// Copyright (C) 1994 - 2011 EMC Corporation
// All rights reserved.
//
//

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
	L"NTDLL.DLL",
	L"WinHttp.dll",
	L"vsstrace.dll",
	L"activeds.dll",
	L"msxml6r.dll",
	L"wininet.dll"
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
	WCHAR ErrorText[1024];
	(void)_wcserror_s(ErrorText, err);
	return ErrorText;
}
