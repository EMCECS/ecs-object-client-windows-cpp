
#include "exportdef.h"
#include "stdafx.h"

#pragma once

// logging functions

typedef void (ECSUTIL_LOG_MESSAGE_PROTO)(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, DWORD dwError);

extern ECSUTIL_EXT_API void RegisterLogMessageCallback(ECSUTIL_LOG_MESSAGE_PROTO *pLogMessageCB);
extern ECSUTIL_EXT_API void LogMessage(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, NTSTATUS dwError, ...);

#ifdef DEBUG
#define DEBUGF DebugF
#else
#define DEBUGF 1 ? (void)0 : DebugF
#endif

extern ECSUTIL_EXT_API void DebugF(LPCTSTR format, ...);
