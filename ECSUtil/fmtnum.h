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

// FmtNum.h
// definition of FmtNum overloaded functions for formatting numeric strings
//


#pragma once

#include "exportdef.h"
#include <sstream>
#include <iomanip>

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
