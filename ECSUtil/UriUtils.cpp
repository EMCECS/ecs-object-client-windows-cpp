/*
 * Copyright (c) 1994 - 2017, EMC Corporation. All Rights Reserved.
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


static std::string strUriDecode(const std::string & sSrc);
static std::string strUriEncode(const std::string & sSrc, bool bEncodeAll);
static CString strUriEncode(LPCTSTR pszInput, bool bEncodeAll);
static CString strUriDecode(LPCTSTR pszInput);
static CString EscapeChar(char ch);

// UriEncode
// translate from Unicode string to Percent encoded string
//If bEncodeAll is true, we will encode almost all the special characters
CString UriEncode(LPCTSTR pszInput, bool bEncodeAll)
{
#if 0
	LPWSTR destOut = NULL;
	DWORD cchDestOut = 0;
	HRESULT hr = WSDUriEncode(pszInput, lstrlen(pszInput), &destOut, &cchDestOut);
	ASSERT(hr == S_OK);
	if (hr != S_OK)				//lint !e774 (Info -- Boolean within 'if' always evaluates to False [Reference: file E:\blds\GD\SRC\atemc_gen\lib\UriUtils.cpp: lines 38, 39])
		return _T("");
	CString sOutput(destOut, cchDestOut);
	WSDFreeLinkedMemory(destOut);
#ifdef DEBUG
	CString sTestOutput = strUriEncode(pszInput);
	ASSERT(sTestOutput == sOutput);
	if (sTestOutput != sOutput)
	{
		for (UINT i=0 ; i<(UINT)sOutput.GetLength() ; i++)
			if (sTestOutput[i] != sOutput[i])
			{
				_tprintf(L"TestOutput=%c, Output=%c\n", (wchar_t)sTestOutput[i], (wchar_t)sOutput[i]);
			}
	}
#endif
	return sOutput;
#endif
	return strUriEncode(pszInput, bEncodeAll);
}

// UriDecode
// translate from percent encoded string to Unicode string
CString UriDecode(LPCTSTR pszInput)
{
	LPWSTR destOut = NULL;
	DWORD cchDestOut = 0;
	HRESULT hr = WSDUriDecode(TO_UNICODE(pszInput), lstrlen(pszInput), &destOut, &cchDestOut);
	ASSERT(hr == S_OK);
	if (hr != S_OK)					//lint !e774 (Info -- Boolean within 'if' always evaluates to False [Reference: file E:\blds\GD\SRC\atemc_gen\lib\UriUtils.cpp: lines 69, 70])
		return _T("");
	CString sOutput(destOut, cchDestOut);
	WSDFreeLinkedMemory(destOut);
#ifdef DEBUG
	CString sTestOutput = strUriDecode(pszInput);
	ASSERT(sTestOutput == sOutput);
#endif
	return sOutput;
}


static CString strUriEncode(LPCTSTR pszInput, bool bEncodeAll)
{
#ifdef _UNICODE
	CAnsiString Input;
	Input.Set(pszInput, -1, CP_UTF8);
	std::string strInput(Input);
#else
	std::string strInput(pszInput);
#endif
	std::string strOutput = strUriEncode(strInput, bEncodeAll);
#ifdef _UNICODE
	CString sOutput(CWideString(strOutput.c_str()));
	return sOutput;
#else
	return strOutput.c_str();
#endif
}

static CString strUriDecode(LPCTSTR pszInput)
{
#ifdef _UNICODE
	CAnsiString Input;
	Input.Set(pszInput);		// shouldn't have any Unicode characters in it, ANSI is good enough
	std::string strInput(Input);
#else
	std::string strInput(pszInput);
#endif
	std::string strOutput = strUriDecode(strInput);
#ifdef _UNICODE
	CWideString Output;
	Output.Set(strOutput.c_str(), -1, CP_UTF8);
	CString sOutput(Output);
	return sOutput;
#else
	return strOutput.c_str();
#endif
}


const char HEX2DEC[256] = 
{
    /*       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F */
    /* 0 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 1 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 2 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 3 */  0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,
    
    /* 4 */ -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 5 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 6 */ -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 7 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    
    /* 8 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 9 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* A */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* B */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    
    /* C */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* D */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* E */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* F */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};
    
static std::string strUriDecode(const std::string & sSrc)
{
    // Note from RFC1630:  "Sequences which start with a percent sign
    // but are not followed by two hexadecimal characters (0-9, A-F) are reserved
    // for future extension"
    
    const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
	const int SRC_LEN = (int)sSrc.length();
    const unsigned char * const SRC_END = pSrc + SRC_LEN;
    const unsigned char * const SRC_LAST_DEC = SRC_END - 2;   // last decodable '%' 

    char * const pStart = new char[SRC_LEN];
    char * pEnd = pStart;

    while (pSrc < SRC_LAST_DEC)
	{
		if (*pSrc == '%')
        {
            char dec1, dec2;
            if (-1 != (dec1 = HEX2DEC[*(pSrc + 1)])
                && -1 != (dec2 = HEX2DEC[*(pSrc + 2)]))
            {
                *pEnd++ = (char)((unsigned char)(dec1) << 4) + dec2;
                pSrc += 3;
                continue;
            }
        }

        *pEnd++ = *pSrc++;
	}

    // the last 2- chars
    while (pSrc < SRC_END)
        *pEnd++ = *pSrc++;

    std::string sResult(pStart, pEnd);
    delete [] pStart;
	return sResult;
}

// Only alphanum is safe.
const char SAFE[256] =
{
    /*      0 1 2 3  4 5 6 7  8 9 A B  C D E F */
    /* 0 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* 1 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* 2 */ 0,1,0,0, 1,0,1,1, 1,1,1,0, 1,1,1,1,
    /* 3 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 0,1,0,1,
    
    /* 4 */ 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
    /* 5 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,1,
    /* 6 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
    /* 7 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,1,0,
    
    /* 8 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* 9 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* A */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* B */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    
    /* C */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* D */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* E */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* F */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
};

//Encode all the characters except alphanum characters and '/'
const char ALLSAFE[256] =
{
	/*      0 1 2 3  4 5 6 7  8 9 A B  C D E F */
	/* 0 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 1 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 2 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1,
	/* 3 */ 1,1,1,1, 1,1,1,1, 1,1,0,0, 0,0,0,0,

	/* 4 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
	/* 5 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,
	/* 6 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
	/* 7 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,

	/* 8 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* 9 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* A */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* B */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,

	/* C */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* D */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* E */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	/* F */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
};

static std::string strUriEncode(const std::string & sSrc, bool bEncodeAll)
{
	const char DEC2HEX[16 + 1] = "0123456789ABCDEF";
    const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
    const int SRC_LEN = (int)sSrc.length();
    unsigned char * const pStart = new unsigned char[SRC_LEN * 3];
    unsigned char * pEnd = pStart;
    const unsigned char * const SRC_END = pSrc + SRC_LEN;

	if (bEncodeAll)
	{
		for (; pSrc < SRC_END; ++pSrc)
		{
			if (ALLSAFE[*pSrc])
				*pEnd++ = *pSrc;
			else
			{
				// escape this char
				*pEnd++ = '%';
				*pEnd++ = DEC2HEX[*pSrc >> 4];
				*pEnd++ = DEC2HEX[*pSrc & 0x0F];
			}
		}
	}
	else
	{
		for (; pSrc < SRC_END; ++pSrc)
		{
			if (SAFE[*pSrc])
				*pEnd++ = *pSrc;
			else
			{
				// escape this char
				*pEnd++ = '%';
				*pEnd++ = DEC2HEX[*pSrc >> 4];
				*pEnd++ = DEC2HEX[*pSrc & 0x0F];
			}
		}
	}

    std::string sResult((char *)pStart, (char *)pEnd);
    delete [] pStart;
    return sResult;
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

CString UriEncodeS3(const CString& sSource, bool bEncodeSlash)
{
	CString sResult;
	for (int i = 0; i < sSource.GetLength(); i++)
	{
		TCHAR ch = sSource.GetAt(i);
		if ((ch >= _T('A') && ch <= _T('Z')) || (ch >= _T('a') && ch <= _T('z')) || (ch >= _T('0') && ch <= _T('9')) || ch == _T('_') || ch == _T('-') || ch == _T('~') || ch == _T('.'))
			sResult += ch;
		else if ((ch == '/') && !bEncodeSlash)
			sResult += ch;
		else
			sResult += EscapeChar((char)ch);
	}
	return sResult;
}

static CString EscapeChar(char ch)
{
	const char DEC2HEX[16 + 1] = "0123456789ABCDEF";

	// escape this char
	return CString(_T('%'))
		+ DEC2HEX[(unsigned char)ch >> 4]
		+ DEC2HEX[(unsigned char)ch & 0x0F];
}
