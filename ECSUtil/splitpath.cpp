//////////////////////////////////////////////////////////////////////////////////////////////
// Copyright © 1994 - 2022, Dell Technologies, Inc. All Rights Reserved.
//
// This software contains the intellectual property of Dell Inc. or is licensed to Dell Inc.
// from third parties. Use of this software and the intellectual property contained therein
// is expressly limited to the terms and conditions of the License Agreement under which it
// is provided by or on behalf of Dell Inc. or its subsidiaries.
//////////////////////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "splitpath.h"


// LCMapStringString
// wrapper for LCMapString
NTSTATUS LCMapStringString(
	CString& sStr,					// string to convert to lower case
	LCID Locale,					// local ID, such as LOCALE_INVARIANT
	DWORD dwMapFlags)				// flags (see LCMapStringEx)
{
	if (sStr.IsEmpty())
		return ERROR_SUCCESS;
	CString sTarget;
	int cchDest = sStr.GetLength() * 2;
	for (;;)
	{
		LPTSTR pTarget = sTarget.GetBuffer(cchDest);
		int iTargetSize = LCMapString(Locale, dwMapFlags, sStr, sStr.GetLength(),
			pTarget, cchDest);
		if (iTargetSize == 0)
		{
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				cchDest *= 2;				// make the buffer larger
				continue;
			}
			ASSERT(false);
			return GetLastError();
		}
		sTarget.ReleaseBuffer(iTargetSize);
		break;
	}
	sStr = sTarget;
	return ERROR_SUCCESS;
}

namespace ecs_sdk
{


	wchar_t MapCaseInvariant(wchar_t input, DWORD dwFlags)
	{
		wchar_t result;

		LONG lres = LCMapStringW(
			LOCALE_INVARIANT,
			dwFlags,
			&input,
			1,
			&result,
			1
		);

		if (lres == 0)
		{
			ASSERT(!"LCMapStringW failed to convert a character to upper case");
			result = input;
		}

		return result;
	}

	CString MapCaseInvariant(LPCTSTR pszInput, DWORD dwFlags)
	{
		if (pszInput == nullptr)
			return L"";
		const int iSlop = 10;
		int iLen = lstrlen(pszInput);
		if (iLen == 0)
			return L"";
		CString sOutput;
		LPTSTR pszOutput = sOutput.GetBuffer(iLen + iSlop);

		LONG lres = LCMapString(
			LOCALE_INVARIANT,
			dwFlags,
			pszInput,
			iLen + 1,			// include the NUL
			pszOutput,
			iLen + iSlop);
		if (lres == 0)
		{
			ASSERT(!"LCMapStringW failed to convert string to upper case");
			return pszInput;
		}
		sOutput.ReleaseBuffer();
		return sOutput;
	}

	void MapCaseInvariant(CString& sInputOutput, DWORD dwFlags)
	{
		NTSTATUS Status = LCMapStringString(sInputOutput, LOCALE_INVARIANT, dwFlags);
		if (Status != 0)
		{
			ASSERT(false);
			return;
		}
	}

} // end namespace ecs_sdk
