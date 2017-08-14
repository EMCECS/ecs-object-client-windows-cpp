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
	CWideString(LPCSTR s = NULL, int inputlen=-1)
	{
		if (s != NULL)
			Set(s, inputlen);
	}

	CWideString(LPCWSTR s)
	{
		if (s == NULL)
			Empty();
		else
			Load(s, (DWORD)((wcslen(s)+1) * sizeof(WCHAR)));
	}

	CWideString& operator =(LPCSTR s)
	{
		if (s == NULL)
			Empty();
		else
			Set(s);
		return(*this);
	}

	CWideString& operator =(LPCWSTR s)
	{
		if (s == NULL)
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
	CAnsiString(LPCWSTR s = NULL, UINT uCodePage = CP_ACP)
	{
		if (s != NULL)
			Set(s, -1, uCodePage);
	}

	CAnsiString(LPCSTR s)
	{
		if (s == NULL)
			Empty();
		else
			Load(s, (DWORD)((strlen(s)+1) * sizeof(char)));
	}

	CAnsiString& operator =(LPCWSTR s)
	{
		if (s == NULL)
			Empty();
		else
			Set(s);
		return(*this);
	}

	CAnsiString& operator =(LPCSTR s)
	{
		if (s == NULL)
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
				int ret = WideCharToMultiByte(uCodePage, 0, s, inputlen, (char *)GetData(), (int)nSize, NULL, NULL);
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

