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

#include "stdafx.h"

#include <Wsdapi.h>
#include <vector>
#include "generic_defs.h"
#include "cbuffer.h"
#include "widestring.h"
#include "UriUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#pragma comment(lib, "Wsdapi.lib")

// initialize strings: all special characters that are SAFE (not encoded)
// alphanumerics are always safe
const char *pSAFEInit = "-_./";		// naming rules: https://docs.aws.amazon.com/AmazonS3/latest/dev/UsingMetadata.html
const char *pSAFEAllInit = "/";
const char *pSAFEv4AuthInit = "-./_~";
const char *pSAFEv4AuthSlashInit = "-._~";

static std::vector<bool> SAFE, SAFEAll, SAFEv4Auth, SAFEv4AuthSlash;

static void InitUriEncode();
static CString EscapeChar(char ch);

// UriEncode
// translate from Unicode string to Percent encoded string
CString UriEncode(LPCWSTR pszInput, E_URI_ENCODE Type)
{
	static bool bUriEncodeInitialized = false;
	static CCriticalSection csUriEncodeInitialized;

	if (!bUriEncodeInitialized)
	{
		CSingleLock lock(&csUriEncodeInitialized, true);
		if (!bUriEncodeInitialized)
		{
			InitUriEncode();
			bUriEncodeInitialized = true;
		}
	}

	// can't use WSDUriEncode: it doesn't properly encode '#'
	const unsigned char *pSrc;
#ifdef _UNICODE
	CAnsiString Input;
	Input.Set(pszInput, -1, CP_UTF8);
	pSrc = (const unsigned char *)Input.GetData();
#else
	pSrc = pszInput;
#endif
	const std::vector<bool> *pSafe = nullptr;
	switch (Type)
	{
	case E_URI_ENCODE::StdSAFE:
		pSafe = &SAFE;
		break;
	case E_URI_ENCODE::AllSAFE:
		pSafe = &SAFEAll;
		break;
	case E_URI_ENCODE::V4Auth:
		pSafe = &SAFEv4Auth;
		break;
	case E_URI_ENCODE::V4AuthSlash:
		pSafe = &SAFEv4AuthSlash;
		break;
	default:
		ASSERT(false);
		return _T("");
	}

	const TCHAR DEC2HEX[16 + 1] = _T("0123456789ABCDEF");
	const int SRC_LEN = (int)strlen((const char *)pSrc);
	CString sResult;
	TCHAR *pEnd = sResult.GetBuffer(SRC_LEN * 4);			// even if every character is escaped, there will be enough room

	for (; *pSrc != '\0'; ++pSrc)
	{
		if ((*pSafe)[*pSrc])
			*pEnd++ = *pSrc;
		else
		{
			// escape this char
			*pEnd++ = L'%';
			*pEnd++ = DEC2HEX[*pSrc >> 4];
			*pEnd++ = DEC2HEX[*pSrc & 0x0F];
		}
	}
	*pEnd++ = L'\0';
	sResult.ReleaseBuffer();
	return sResult;
}

// UriDecode
// translate from percent encoded string to Unicode string
CString UriDecode(LPCTSTR pszInput)
{
	LPWSTR destOut = nullptr;
	DWORD cchDestOut = 0;
	HRESULT hr = WSDUriDecode(TO_UNICODE(pszInput), lstrlen(pszInput), &destOut, &cchDestOut);
	ASSERT(hr == S_OK);
	if (hr != S_OK)					//lint !e774 (Info -- Boolean within 'if' always evaluates to False [Reference: file E:\blds\GD\SRC\atemc_gen\lib\UriUtils.cpp: lines 69, 70])
		return _T("");
	CString sOutput(destOut, cchDestOut);
	WSDFreeLinkedMemory(destOut);
	return sOutput;
}

// EncodeSpecialChars
// percent encode characters such as '=', '#' and other characters that may cause problems
// used by shareable URL
// for instance, if '=' is the last character (as it always is for a shareable URL), Outlook won't include it
// as part of the URL
CString EncodeSpecialChars(const CString& sSource)
{
	TCHAR ch;
	CString sOutput;
	// start after the protocol part ('http://')
	int iStart = sSource.Find(_T("://"));
	if (iStart < 0)
		return sSource;
	iStart += 3;
	sOutput = sSource.Left(iStart);
	for ( ; iStart < sSource.GetLength() ; iStart++)
	{
		switch (ch = sSource[iStart])
		{
		case _T('#'):
            // escape this char
			sOutput += EscapeChar((char)ch);
			break;
		default:
			sOutput.AppendChar(sSource[iStart]);
			break;
		}
	}
	// if the last char is '=', change it to %3D
	if (!sOutput.IsEmpty() && (sOutput[sOutput.GetLength() - 1] == _T('=')))
	{
		sOutput.SetAt(sOutput.GetLength() - 1, _T('%'));
		sOutput.Append(_T("3D"));
	}
	return sOutput;
}

static CString EscapeChar(char ch)
{
	const char DEC2HEX[16 + 1] = "0123456789ABCDEF";

	// escape this char
	return CString(_T('%'))
		+ DEC2HEX[(unsigned char)ch >> 4]
		+ DEC2HEX[(unsigned char)ch & 0x0F];
}

static void InitUriEncodeArray(std::vector<bool>& FlagArray, const char *pSpecialChars)
{
	FlagArray.reserve(256);
	for (UINT i = 0; i < 256; ++i)
	{
		if (((i >= (UINT)'A') && (i <= (UINT)'Z'))
			|| ((i >= (UINT)'a') && (i <= (UINT)'z'))
			|| ((i >= (UINT)'0') && (i <= (UINT)'9')))
			FlagArray.push_back(true);
		else
			FlagArray.push_back(false);
	}
	if (pSpecialChars != nullptr)
	{
		for (; *pSpecialChars != '\0'; ++pSpecialChars)
		{
			FlagArray.at((unsigned int)(unsigned char)*pSpecialChars) = true;
		}
	}
}

static void InitUriEncode()
{
	InitUriEncodeArray(SAFE, pSAFEInit);
	InitUriEncodeArray(SAFEAll, pSAFEAllInit);
	InitUriEncodeArray(SAFEv4Auth, pSAFEv4AuthInit);
	InitUriEncodeArray(SAFEv4AuthSlash, pSAFEv4AuthSlashInit);
}
