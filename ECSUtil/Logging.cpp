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
		OutputDebugString(sErrorMsg + ((dwError == ERROR_SUCCESS) ? L"" : L" - " + GetNTErrorText(dwError)));
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

