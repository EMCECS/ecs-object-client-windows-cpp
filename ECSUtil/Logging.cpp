/*
 * Copyright (c) 2017, EMC Corporation. All Rights Reserved.
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

// logging functions

#include "stdafx.h"

#include "Logging.h"
#include "NTERRTXT.H"

static ECSUTIL_LOG_MESSAGE_PROTO *pLogMessageCB = nullptr;

// RegisterLogMessageCallback
// register callback to receive log messages
void RegisterLogMessageCallback(ECSUTIL_LOG_MESSAGE_PROTO *pLogMessageCBParam)
{
	pLogMessageCB = pLogMessageCBParam;
}

void LogMessageVa(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, NTSTATUS dwError, va_list marker)
{
	CString sMsg;
	sMsg.FormatMessageV(pszLogMessage, &marker);
	if (pLogMessageCB == nullptr)
	{
		OutputDebugString(sMsg + ((dwError == ERROR_SUCCESS) ? _T("") : _T(" - ") + GetNTErrorText(dwError)));
	}
	else
	{
		pLogMessageCB(pszFile, dwLine, sMsg, dwError);
	}
}

void LogMessage(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, NTSTATUS dwError, ...)
{
	va_list marker;
	va_start(marker, dwError);     /* Initialize variable arguments. */
	LogMessageVa(pszFile, dwLine, pszLogMessage, dwError, marker);
	va_end(marker);              /* Reset variable arguments.      */
}

void DebugF(LPCTSTR format, ...)
{
	CString sMsg;
	va_list marker;
	va_start(marker, format);     /* Initialize variable arguments. */

	sMsg.FormatV(format, marker);
	if (sMsg.GetLength() > 0)
	{
		if (sMsg[sMsg.GetLength() - 1] != TEXT('\n'))
		{
			sMsg += TEXT('\n');
		}
	}
	OutputDebugString(sMsg);
	va_end(marker);              /* Reset variable arguments.      */
}

void CECSLoggingBase::LogMsg(
	DWORD dwLogLevel,						// EVENTLOG_ERROR_TYPE or EVENTLOG_WARNING_TYPE
	LPCTSTR pszLogMessage,					// log message
	NTSTATUS dwError,						// error code, if any
	...)									// sprintf parameters for pszLogMessage, if any
{
	va_list marker;
	va_start(marker, dwError);     /* Initialize variable arguments. */
	CString sMsg, sErrorText;
	sMsg.FormatV(pszLogMessage, marker);
	if (dwError != ERROR_SUCCESS)
		sErrorText = GetNTErrorText(dwError);
	LogMessageCB(dwLogLevel, sMsg, dwError, sErrorText);
}

void CECSLoggingBase::TraceMsg(
	LPCTSTR pszLogMessage,					// log message
	...)									// sprintf parameters for pszLogMessage, if any
{
	va_list marker;
	va_start(marker, pszLogMessage);     /* Initialize variable arguments. */
	if (bTraceEnabled)
	{
		CString sMsg;
		sMsg.FormatV(pszLogMessage, marker);
		TraceMessageCB(sMsg);
		DEBUGF(_T("%s"), (LPCTSTR)sMsg);
	}
}
