/*
 * Copyright (c) 2017 - 2022, Dell Technologies, Inc. All Rights Reserved.
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
#include <type_traits>
#include "cbuffer.h"

namespace ecs_sdk
{


	ECSUTIL_EXT_API CString InsertCommas(LPCTSTR szNum);	// insert commas into the number dependent on the current locale

	template <class T>
	inline CString FmtNum(
		T t,							// value to translate
		int width = 0,					// minimum field width
		bool lead_zero = false,			// true pad with leading zeroes, false pad with spaces
		bool hex_flag = false,			// true hex, false decimal
		bool comma_flag = false)		// true: insert comma's dependent on the locale
	{
		if (!std::is_integral<T>::value)
			return _T("Bad Type");
		if ((sizeof(T) == 1) && (sizeof(T) == 2) && (sizeof(T) == 4) && (sizeof(T) == 8))
			return _T("Bad Size");
		// %[flags][width][size]type
		CString sOut;
		TCHAR FmtStr[20];
		TCHAR WidthStr[20];
		WidthStr[0] = _T('\0');
		if (width > 0)
			_itot_s(width, WidthStr, 10);
		_tcscpy_s(FmtStr, _T("%"));
		if (lead_zero && (width > 0))
			_tcscat_s(FmtStr, _T("0"));
		_tcscat_s(FmtStr, WidthStr);
		_tcscat_s(FmtStr, (sizeof(T) == 1) ? _T("hh") : (sizeof(T) == 2) ? _T("h") : (sizeof(T) == 4) ? _T("l") : _T("ll"));
		_tcscat_s(FmtStr, hex_flag ? _T("x") : (std::is_signed<T>::value) ? _T("d") : _T("u"));
		sOut.Format(FmtStr, t);

		if (comma_flag)
			return InsertCommas(sOut);
		return sOut;
	}

	//
	// DateTimeStr
	// format date/time string from either file time or system time
	// if bShow24HourTime, then display in military time
	// if bSeconds, then show seconds and millisec
	//
	ECSUTIL_EXT_API CString DateTimeStr(const SYSTEMTIME* pST, bool bSeconds = false, bool bDate = true, bool bTime = true, bool bLongDate = false, bool bMilliSec = false, LCID Locale = LOCALE_USER_DEFAULT);
	ECSUTIL_EXT_API CString DateTimeStr(const FILETIME* pFT, bool bSeconds = false, bool bDate = true, bool bTime = true, bool bLongDate = false, bool bLocalTime = false, bool bMilliSec = false, LCID Locale = LOCALE_USER_DEFAULT);

	// BinaryToHex
	// output 2 hex characters for each byte (uppercase)
	ECSUTIL_EXT_API CString BinaryToHexString(const BYTE* pData, ULONG uData);
	ECSUTIL_EXT_API CString BinaryToHexString(const CBuffer& Data);


} // end namespace ecs_sdk
