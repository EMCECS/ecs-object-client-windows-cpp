/*
 * Copyright (c) 2017 - 2019, Dell Technologies, Inc. All Rights Reserved.
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

/****************************************************************************
*
*
*    PROGRAM: WideString.h
*
*    PURPOSE: CWideString/CAnsiString definitions
*
*
****************************************************************************/

#pragma once


#include "cbuffer.h"


class ECSUTIL_EXT_CLASS CWideString : public CBuffer
{
public:
	CWideString(LPCSTR s = nullptr, int inputlen=-1)
	{
		if (s != nullptr)
			Set(s, inputlen);
	}

	CWideString(LPCWSTR s)
	{
		if (s == nullptr)
			Empty();
		else
			Load(s, (DWORD)((wcslen(s)+1) * sizeof(WCHAR)));
	}

	CWideString& operator =(LPCSTR s)
	{
		if (s == nullptr)
			Empty();
		else
			Set(s);
		return(*this);
	}

	CWideString& operator =(LPCWSTR s)
	{
		if (s == nullptr)
			Empty();
		else
			Load(s, (DWORD)((wcslen(s)+1) * sizeof(WCHAR)));
		return(*this);
	}

	operator LPCWSTR() const        // as a Wide string
	{
		return((LPCWSTR)GetData());
	}

	void Set(LPCSTR s, int inputlen=-1, UINT uCodePage=CP_ACP)
	{
		// translate from Mbs to UNICODE
		if ((s == nullptr) || (inputlen == 0) || (*s == '\0'))
		{
			SetBufSize((DWORD)sizeof(WCHAR));			// room for just a NUL
			WCHAR *pOutput = (WCHAR *)GetData();
			pOutput[0] = L'\0';
		}
		else
		{
			int nSize = inputlen;
			if (nSize < 0)
				nSize = (int)strlen(s);
			nSize *= 4;					// leave plenty of room for UTF-8 to UTF-16 conversion
			for (;;)
			{
				SetBufSize((DWORD)(sizeof(WCHAR)*(nSize + 1)));
				WCHAR *pOutput = (WCHAR *)GetData();
				// allocate 1 more char than what we tell WideCharToMultiByte so there is room for the NUL
				int ret = MultiByteToWideChar(uCodePage, uCodePage == CP_UTF8 ? 0 : MB_PRECOMPOSED, s, inputlen, pOutput, (int)nSize);
				if (ret == 0)
				{
					DWORD dwError = GetLastError();
					if (dwError == ERROR_INSUFFICIENT_BUFFER)
					{
						nSize *= 2;					// try a bigger buffer
						continue;
					}
					else
					{
						ASSERT(false);
						Empty();
						break;
					}
				}
				pOutput[ret] = 0;
				SetBufSize((DWORD)(sizeof(WCHAR) * (ret + 1)));
				break;
			}
		}
	}
};


class ECSUTIL_EXT_CLASS CAnsiString : public CBuffer
{
public:
	CAnsiString(LPCWSTR s = nullptr, UINT uCodePage = CP_ACP)
	{
		if (s != nullptr)
			Set(s, -1, uCodePage);
	}

	CAnsiString(LPCSTR s)
	{
		if (s == nullptr)
			Empty();
		else
			Load(s, (DWORD)((strlen(s)+1) * sizeof(char)));
	}

	CAnsiString& operator =(LPCWSTR s)
	{
		if (s == nullptr)
			Empty();
		else
			Set(s);
		return(*this);
	}

	CAnsiString& operator =(LPCSTR s)
	{
		if (s == nullptr)
			Empty();
		else
			Load(s, (DWORD)((strlen(s)+1) * sizeof(char)));
		return(*this);
	}

	operator LPCSTR() const
	{
		return((LPCSTR)GetData());
	}

	// Set
	// be sure the resultant string is NUL terminated
	void Set(LPCWSTR s, int inputlen=-1, UINT uCodePage = CP_ACP)
	{
		// translate from Mbs to UNICODE
		if ((s == nullptr) || (inputlen == 0) || (*s == L'\0'))
		{
			SetBufSize((DWORD)sizeof(CHAR));			// room for just a NUL
			CHAR *pOutput = (CHAR *)GetData();
			pOutput[0] = '\0';
		}
		else
		{
			int nSize = inputlen;
			if (nSize < 0)
				nSize = (int)wcslen(s);
			nSize *= sizeof(WCHAR);		// number of bytes for the original string
			nSize *= 2;					// leave plenty of room for UTF-16 to UTF-8 conversion
			for (;;)
			{
				SetBufSize((DWORD)(sizeof(WCHAR)*(nSize + 1)));
				CHAR *pOutput = (CHAR *)GetData();
				// allocate 1 more char than what we tell WideCharToMultiByte so there is room for the NUL
				int ret = WideCharToMultiByte(uCodePage, 0, s, inputlen, (char *)GetData(), (int)nSize, nullptr, nullptr);
				if (ret == 0)
				{
					DWORD dwError = GetLastError();
					if (dwError == ERROR_INSUFFICIENT_BUFFER)
					{
						nSize *= 2;					// try a bigger buffer
						continue;
					}
					else
					{
						ASSERT(false);
						Empty();
						break;
					}
				}
				pOutput[ret] = L'\0';
				SetBufSize(ret + 1);
				break;
			}
		}
	}
};

inline CStringW ConvertToUnicode(LPCSTR pszStr)
{
	CWideString Str(pszStr);
	return CStringW((LPCWSTR)Str.GetData());
}

inline CStringA ConvertToAnsi(LPCWSTR pszStr)
{
	CAnsiString Str(pszStr, CP_UTF8);
	return CStringA((LPCSTR)Str.GetData());
}

#ifdef _UNICODE
#define TO_UNICODE(pszStr) (pszStr)
#else
#define TO_UNICODE(pszStr) ConvertToUnicode(pszStr)
#endif

#ifdef _UNICODE
#define TO_ANSI(pszStr) ConvertToAnsi(pszStr)
#else
#define TO_ANSI(pszStr) (pszStr)
#endif

#ifdef _UNICODE
#define FROM_UNICODE(pszStr) (pszStr)
#else
#define FROM_UNICODE(pszStr) ConvertToAnsi(pszStr)
#endif

#ifdef _UNICODE
#define FROM_ANSI(pszStr) ConvertToUnicode(pszStr)
#else
#define FROM_ANSI(pszStr) (pszStr)
#endif

