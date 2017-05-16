//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//
//
/****************************************************************************
*    processevent.h
*    PURPOSE: definitions/prototypes for CProcessEvent
*
****************************************************************************/

#pragma once

#include "stdafx.h"
#include "exportdef.h"


class ECSUTIL_EXT_CLASS CProcessEvent : public CSyncObject
{
private:
	DECLARE_DYNAMIC(CProcessEvent)

// Constructor
public:
	CProcessEvent()
	: CSyncObject(NULL)
	{
	}

	DWORD Create(DWORD dwProcessId)
	{
		if (m_hObject != NULL)
		{
			(void)::CloseHandle(m_hObject);
			m_hObject = NULL;
		}
		m_hObject = OpenProcess(SYNCHRONIZE, FALSE, dwProcessId);
		if (m_hObject == NULL)
			return GetLastError();
		return ERROR_SUCCESS;
	}

	void CloseProcess(void)
	{
		if (m_hObject != NULL)
		{
			(void)::CloseHandle(m_hObject);
			m_hObject = NULL;
		}
	}

// Operations
public:
	BOOL SetEvent()
		{ ASSERT(m_hObject != NULL); return ::SetEvent(m_hObject); }
	BOOL PulseEvent()
		{ ASSERT(m_hObject != NULL); return ::PulseEvent(m_hObject); }
	BOOL ResetEvent()
		{ ASSERT(m_hObject != NULL); return ::ResetEvent(m_hObject); }
	BOOL Unlock()
		{ return TRUE; }

// Implementation
public:
	virtual ~CProcessEvent()
	{
		if (m_hObject != NULL)
		{
			::CloseHandle(m_hObject);
			m_hObject = NULL;
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
	: CSyncObject(NULL)
	{
	}

	DWORD Create(DWORD dwThreadId)
	{
		if (m_hObject != NULL)
		{
			(void)::CloseHandle(m_hObject);
			m_hObject = NULL;
		}
		m_hObject = OpenThread(SYNCHRONIZE, FALSE, dwThreadId);
		if (m_hObject == NULL)
			return GetLastError();
		return ERROR_SUCCESS;
	}

	void CloseThread(void)
	{
		if (m_hObject != NULL)
		{
			(void)::CloseHandle(m_hObject);
			m_hObject = NULL;
		}
	}

// Operations
public:
	BOOL SetEvent()
		{ ASSERT(m_hObject != NULL); return ::SetEvent(m_hObject); }
	BOOL PulseEvent()
		{ ASSERT(m_hObject != NULL); return ::PulseEvent(m_hObject); }
	BOOL ResetEvent()
		{ ASSERT(m_hObject != NULL); return ::ResetEvent(m_hObject); }
	BOOL Unlock()
		{ return TRUE; }

// Implementation
public:
	virtual ~CThreadEvent()
	{
		if (m_hObject != NULL)
		{
			::CloseHandle(m_hObject);
			m_hObject = NULL;
		}
	}
};

