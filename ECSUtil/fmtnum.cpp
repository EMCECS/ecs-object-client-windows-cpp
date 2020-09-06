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

/****************************************************************************
*    FmtNum.cpp
*    PURPOSE: implementation of the numeric formatting routines to use with CString
*
****************************************************************************/


#include "stdafx.h"

#include "FmtNum.h"
#include "cbuffer.h"

namespace ecs_sdk
{


	//
	// DateTimeStr
	// format date/time string from either file time or system time
	// if bShow24HourTime, then display in military time
	// if bSeconds, then show seconds and millisec
	//
	CString DateTimeStr(const SYSTEMTIME* pST, bool bSeconds, bool bDate, bool bTime, bool bLongDate, bool bMilliSec, LCID Locale)
	{
		CString sDate, sTime;
		DWORD dwFlags;
		int iStrLen;
		LPTSTR pBuf;

		if (bDate)
		{
			dwFlags = 0;
			if (bLongDate)
				dwFlags |= DATE_LONGDATE;
			else
				dwFlags |= DATE_SHORTDATE;
			iStrLen = GetDateFormat(Locale, dwFlags, pST, nullptr, nullptr, 0);
			pBuf = sDate.GetBuffer(iStrLen + 2);
			(void)GetDateFormat(Locale, dwFlags, pST, nullptr, pBuf, iStrLen + 2);
			sDate.ReleaseBuffer();
		}
		if (bTime)
		{
			dwFlags = 0;
			if (!bSeconds)
				dwFlags |= TIME_NOSECONDS;
			iStrLen = GetTimeFormat(Locale, dwFlags, pST, nullptr, nullptr, 0);
			pBuf = sTime.GetBuffer(iStrLen + 2);
			(void)GetTimeFormat(Locale, dwFlags, pST, nullptr, pBuf, iStrLen + 2);
			sTime.ReleaseBuffer();
			if (bMilliSec)
				sTime += TEXT(" ") + FmtNum(pST->wMilliseconds, 3, true) + TEXT(" ms");
		}
		if (bDate && bTime)
			return sDate + TEXT(" ") + sTime;
		if (bDate)
			return sDate;
		if (bTime)
			return sTime;
		return (TEXT(""));
	}

	CString DateTimeStr(const FILETIME* pFT, bool bSeconds, bool bDate, bool bTime, bool bLongDate, bool bLocalTime, bool bMilliSec, LCID Locale)
	{
		SYSTEMTIME  st{ 0,0,0,0,0,0,0,0 };
		FILETIME ftTime;

		//if (FTtoI64(*pFT) != 0)
		if (!IfFTZero(*pFT))
		{
			if (bLocalTime)
			{
				VERIFY(FileTimeToLocalFileTime(pFT, &ftTime));
				FileTimeToSystemTime(&ftTime, &st);
			}
			else
				FileTimeToSystemTime(pFT, &st);
			return DateTimeStr(&st, bSeconds, bDate, bTime, bLongDate, bMilliSec, Locale);
		}
		return(TEXT(""));
	}


	// BinaryToHex
	// output 2 hex characters for each byte (uppercase)
	CString BinaryToHexString(const BYTE* pData, ULONG uData)
	{
		const WCHAR DEC2HEX[16 + 1] = L"0123456789abcdef";
		CString sOut;
		sOut.Preallocate(uData * 2 + 1);				// save some time by allocating it all in one shot

		for (ULONG i = 0; i < uData; i++)
		{
			sOut += DEC2HEX[pData[i] >> 4];
			sOut += DEC2HEX[pData[i] & 0x0F];
		}
		return sOut;
	}

	CString BinaryToHexString(const CBuffer& Data)
	{
		return BinaryToHexString(Data.GetData(), Data.GetBufSize());
	}

	//
	// InsertCommas
	// insert commas to make the number readable
	//
	CString InsertCommas(LPCTSTR szNum)
	{
		static CCriticalSection csLocale;
		CSingleLock lock(&csLocale, true);
		CString sFmtNum;
		LPTSTR pBuf;
		int iStrLen;
		TCHAR Digits[10], NoDigits[] = TEXT("0");

		(void)GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_IDIGITS, Digits, _countof(Digits));
		(void)SetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_IDIGITS, NoDigits);
		// first figure out the size of the buffer;
		iStrLen = GetNumberFormat(LOCALE_USER_DEFAULT, 0, szNum, NULL, NULL, 0);
		pBuf = sFmtNum.GetBuffer(iStrLen + 2);
		(void)GetNumberFormat(LOCALE_USER_DEFAULT, 0, szNum, NULL, pBuf, iStrLen + 2);
		sFmtNum.ReleaseBuffer();
		(void)SetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_IDIGITS, Digits);
		return sFmtNum;
	}


} // end namespace ecs_sdk
