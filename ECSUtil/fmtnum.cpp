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
*    FmtNum.cpp
*    PURPOSE: implementation of the numeric formatting routines to use with CString
*
****************************************************************************/


#include "stdafx.h"

#include "FmtNum.h"


//
// DateTimeStr
// format date/time string from either file time or system time
// if bShow24HourTime, then display in military time
// if bSeconds, then show seconds and millisec
//
CString DateTimeStr(const SYSTEMTIME *pST, bool bSeconds, bool bDate, bool bTime, bool bLongDate, bool bMilliSec, LCID Locale)
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
		iStrLen = GetDateFormat(Locale, dwFlags, pST, NULL, NULL, 0);
		pBuf = sDate.GetBuffer(iStrLen + 2);
		(void)GetDateFormat(Locale, dwFlags, pST, NULL, pBuf, iStrLen+2);
		sDate.ReleaseBuffer();
	}
	if (bTime)
	{
		dwFlags = 0;
		if (!bSeconds)
			dwFlags |= TIME_NOSECONDS;
        iStrLen = GetTimeFormat(Locale, dwFlags, pST, NULL, NULL, 0);
		pBuf = sTime.GetBuffer(iStrLen + 2);
		(void)GetTimeFormat(Locale, dwFlags, pST, NULL, pBuf, iStrLen+2);
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

CString DateTimeStr(const FILETIME *pFT, bool bSeconds, bool bDate, bool bTime, bool bLongDate, bool bLocalTime, bool bMilliSec, LCID Locale)
{
	SYSTEMTIME  st;
	FILETIME ftTime;

	//if (FTtoI64(*pFT) != 0)
	if(!IfFTZero(*pFT))
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

