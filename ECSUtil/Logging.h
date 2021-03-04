/*
 * Copyright (c) 2017 - 2021, Dell Technologies, Inc. All Rights Reserved.
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

#include "exportdef.h"
#include "stdafx.h"

#pragma once

namespace ecs_sdk
{


	// logging functions

	typedef void (ECSUTIL_LOG_MESSAGE_PROTO)(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, DWORD dwError);

	extern ECSUTIL_EXT_API void RegisterLogMessageCallback(ECSUTIL_LOG_MESSAGE_PROTO* pLogMessageCB);
	extern ECSUTIL_EXT_API void LogMessageVa(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, NTSTATUS dwError, va_list marker);
	extern ECSUTIL_EXT_API void LogMessage(LPCTSTR pszFile, DWORD dwLine, LPCTSTR pszLogMessage, NTSTATUS dwError, ...);

#ifdef DEBUG
#define DEBUGF DebugF
#else
#define DEBUGF 1 ? (void)0 : DebugF
#endif

	extern ECSUTIL_EXT_API void DebugF(LPCTSTR format, ...);

	// alternative logging if callback doesn't work
	// derive a class from this base class and define the logging call
	class ECSUTIL_EXT_CLASS CECSLoggingBase
	{
	private:
		bool bTraceEnabled;			// tracing is enabled

	protected:
		virtual void LogMessageCB(DWORD dwLogLevel, LPCTSTR pszMsg, DWORD dwError, LPCTSTR pszErrorText) = 0;
		virtual void TraceMessageCB(LPCTSTR pszMsg) = 0;

	public:
		CECSLoggingBase(bool bTraceEnabledParam = true)
		{
			bTraceEnabled = bTraceEnabledParam;
		}

		virtual ~CECSLoggingBase()
		{
		}

		virtual void EnableTrace(bool bTraceEnabledParam)
		{
			bTraceEnabled = bTraceEnabledParam;
		}

		void LogMsg(DWORD dwLogLevel, LPCTSTR pszLogMessage, NTSTATUS dwError, ...);
		void TraceMsg(LPCTSTR pszLogMessage, ...);
	};


} // end namespace ecs_sdk
