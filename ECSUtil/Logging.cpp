/*
 * Copyright (c) 2017, EMC Corporation.
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

void LogMessage(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, NTSTATUS dwError, ...)
{
	CString sErrorMsg;
	va_list marker;
	va_start(marker, pszLogMessage);     /* Initialize variable arguments. */

	sErrorMsg.FormatV(pszLogMessage, marker);
	va_end(marker);              /* Reset variable arguments.      */
	if (pLogMessageCB == nullptr)
	{
		OutputDebugString(sErrorMsg + ((dwError == ERROR_SUCCESS) ? _T("") : _T(" - ") + GetNTErrorText(dwError)));
	}
	else
	{
		pLogMessageCB(pszFile, dwLine, sErrorMsg, dwError);
	}
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

