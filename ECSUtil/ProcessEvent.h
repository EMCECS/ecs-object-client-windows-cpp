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
*    processevent.h
*    PURPOSE: definitions/prototypes for CProcessEvent
*
****************************************************************************/

#pragma once

#include "stdafx.h"
#include "exportdef.h"

namespace ecs_sdk
{


	class ECSUTIL_EXT_CLASS CProcessEvent : public CSyncObject
	{
	private:
		DECLARE_DYNAMIC(CProcessEvent)

		// Constructor
	public:
		CProcessEvent()
			: CSyncObject(nullptr)
		{
		}

		DWORD Create(DWORD dwProcessId)
		{
			if (m_hObject != nullptr)
			{
				(void)::CloseHandle(m_hObject);
				m_hObject = nullptr;
			}
			m_hObject = OpenProcess(SYNCHRONIZE, FALSE, dwProcessId);
			if (m_hObject == nullptr)
				return GetLastError();
			return ERROR_SUCCESS;
		}

		void CloseProcess(void)
		{
			if (m_hObject != nullptr)
			{
				(void)::CloseHandle(m_hObject);
				m_hObject = nullptr;
			}
		}

		// Operations
	public:
		BOOL SetEvent()
		{
			ASSERT(m_hObject != nullptr); return ::SetEvent(m_hObject);
		}
		BOOL PulseEvent()
		{
			ASSERT(m_hObject != nullptr); return ::PulseEvent(m_hObject);
		}
		BOOL ResetEvent()
		{
			ASSERT(m_hObject != nullptr); return ::ResetEvent(m_hObject);
		}
		BOOL Unlock()
		{
			return TRUE;
		}

		// Implementation
	public:
		virtual ~CProcessEvent()
		{
			if (m_hObject != nullptr)
			{
				::CloseHandle(m_hObject);
				m_hObject = nullptr;
			}
		}
	};

	class ECSUTIL_EXT_CLASS CThreadEvent : public CSyncObject
	{
	private:
		DECLARE_DYNAMIC(CThreadEvent)

		// Constructor
	public:
		CThreadEvent()
			: CSyncObject(nullptr)
		{
		}

		DWORD Create(DWORD dwThreadId)
		{
			if (m_hObject != nullptr)
			{
				(void)::CloseHandle(m_hObject);
				m_hObject = nullptr;
			}
			m_hObject = OpenThread(SYNCHRONIZE, FALSE, dwThreadId);
			if (m_hObject == nullptr)
				return GetLastError();
			return ERROR_SUCCESS;
		}

		void CloseThread(void)
		{
			if (m_hObject != nullptr)
			{
				(void)::CloseHandle(m_hObject);
				m_hObject = nullptr;
			}
		}

		// Operations
	public:
		BOOL SetEvent()
		{
			ASSERT(m_hObject != nullptr); return ::SetEvent(m_hObject);
		}
		BOOL PulseEvent()
		{
			ASSERT(m_hObject != nullptr); return ::PulseEvent(m_hObject);
		}
		BOOL ResetEvent()
		{
			ASSERT(m_hObject != nullptr); return ::ResetEvent(m_hObject);
		}
		BOOL Unlock()
		{
			return TRUE;
		}

		// Implementation
	public:
		virtual ~CThreadEvent()
		{
			if (m_hObject != nullptr)
			{
				::CloseHandle(m_hObject);
				m_hObject = nullptr;
			}
		}
	};


} // end namespace ecs_sdk
