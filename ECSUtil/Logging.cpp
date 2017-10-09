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
	CString sErrorMsg;
	sErrorMsg.FormatV(pszLogMessage, marker);
	if (pLogMessageCB == nullptr)
	{
		OutputDebugString(sErrorMsg + ((dwError == ERROR_SUCCESS) ? _T("") : _T(" - ") + GetNTErrorText(dwError)));
	}
	else
	{
		pLogMessageCB(pszFile, dwLine, sErrorMsg, dwError);
	}
}

void LogMessage(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, NTSTATUS dwError, ...)
{
	va_list marker;
	va_start(marker, pszLogMessage);     /* Initialize variable arguments. */
	LogMessageVa(pszFile, dwLine, pszLogMessage, dwError, marker);
	va_end(marker);              /* Reset variable arguments.      */
}

void DebugF(LPCTSTR format, ...)
{
	CString sErrorMsg;
	va_list marker;
	va_start(marker, format);     /* Initialize variable arguments. */

	sErrorMsg.FormatV(format, marker);
	if (sErrorMsg.GetLength() > 0)
	{
		if (sErrorMsg[sErrorMsg.GetLength() - 1] != TEXT('\n'))
		{
			sErrorMsg += TEXT('\n');
		}
	}
	OutputDebugString(sErrorMsg);
	va_end(marker);              /* Reset variable arguments.      */
}

