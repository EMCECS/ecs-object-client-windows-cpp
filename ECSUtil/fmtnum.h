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

// FmtNum.h
// definition of FmtNum overloaded functions for formatting numeric strings
//


#pragma once

#include "exportdef.h"
#include <sstream>
#include <iomanip>
#include "cbuffer.h"

//
// formatting functions to be used with the CString library
//
template <class T, class chartype>
inline std::basic_string<chartype> numToTString(T t)
{
	std::basic_ostringstream<chartype> o;
	o << t;
	return o.str();
}
template <class T>
inline std::string numToString(T t)
{
	return numToTString<T,char>(t);
}
template <class T>
inline std::wstring numToString(T t)
{
	return numToTString<T,wchar_t>(t);
}

template <class T>
inline CString FmtNum(
	T t,							// value to translate
	int width = 0,					// minimum field width
	bool lead_zero = false,			// true pad with leading zeroes, false pad with spaces
	bool hex_flag = false)			// true hex, false decimal
{
#ifdef _UNICODE
	wchar_t fillch = lead_zero ? L'0' : L' ';
	std::basic_ostringstream<wchar_t> o;
#else
	char fillch = lead_zero ? '0' : ' ';
	std::basic_ostringstream<char> o;
#endif
	o << std::setw(width) << std::setfill(fillch) << std::setbase(hex_flag ? 16 : 10) << t;
	return CString(o.str().c_str());
}

//
// DateTimeStr
// format date/time string from either file time or system time
// if bShow24HourTime, then display in military time
// if bSeconds, then show seconds and millisec
//
ECSUTIL_EXT_API CString DateTimeStr(const SYSTEMTIME *pST, bool bSeconds=false, bool bDate=true, bool bTime=true, bool bLongDate=false, bool bMilliSec=false, LCID Locale=LOCALE_USER_DEFAULT);
ECSUTIL_EXT_API CString DateTimeStr(const FILETIME *pFT, bool bSeconds=false, bool bDate=true, bool bTime=true, bool bLongDate=false, bool bLocalTime=false, bool bMilliSec=false, LCID Locale=LOCALE_USER_DEFAULT);

// BinaryToHex
// output 2 hex characters for each byte (uppercase)
ECSUTIL_EXT_API CString BinaryToHexString(const BYTE *pData, ULONG uData);
ECSUTIL_EXT_API CString BinaryToHexString(const CBuffer& Data);
