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

#pragma once
#include "exportdef.h"

#include <afxmt.h>
#include "CRWLock.h"

using namespace std;



////////////////////////////////////////////////////////////////////
//
// To use CSimpleWorkerThread:
// derive a class from CSimpleWorkerThread, such as:
//
//	struct CThreadClass : public CSimpleWorkerThread
//	{
//		// define here any thread-specific variables, if any
//
//		CThreadClass() {/* put constructors for thread specific variables */};
//		~CThreadClass() {/* put destructors for thread specific variables */};
//		void DoWork();	// the implementation of this is the actual thread that is created
//		bool InitInstance(void);	// run once before DoWork is executed
//		void ExitInstance(void);	// run once just before the thread terminates
//	};
//
//	Constructor, destructor, InitInstance, ExitInstance are optional.
//
//	A destructor must always be used.
//  KillThreadWait() must be the first thing called. If not,
//  the destructor can run the destructors on the thread-specific variables while the thread
//	is still running.
//
//	If used, InitInstance should look like:
//	bool CThreadClass::InitInstance()
//	{
//		/* do whatever processing is required */
//		return true;		// return false to abort the thread
//	}
//
//	If used, ExitInstance should look like:
//	void CThreadClass::ExitInstance()
//	{
//		/* do whatever processing is required */
//	}
//
//	To create the thread, run ThreadVar->CreateThread()
//	DoWork will only be run when there is something to do. Exiting DoWork does NOT terminate
//	the thread. DoWork should be designed to handle an event sent to the thread. There is one
//	built-in event that is triggered with the StartWork() call. Each call to StartWork by any
//	thread will trigger one execution of DoWork.
//
//	The timeout is initially set to INFINITE. To get DoWork to get executed even if no event
//	has occurred, a timeout can be set, using SetCycleTime(). This can be set from any thread.
//	To get DoWork to execute every second, use SetCycleTime(SECONDS(1)).
//
//	To have the thread triggered by more than the one built-in event, additional events can
//	be registered with the thread. Each event is a variable of type CEvent, and an address of
//	that variable is pushed onto the EventList member variable (type CQueue). This can be done
//	before the thread is started, during InitInstance, or during execution of the thread in
//	DoWork.
//
//	If events are pushed onto EventList, the event can be determined as follows:
//		void CThreadClass::DoWork()
//		{
//			switch (dwEventRet)
//			{
//			case WAIT_OBJECT_0:
//				//	the first event pushed on EventList
//				break;
//			case WAIT_OBJECT_0+1:
//				//	the second event pushed on EventList
//				break;
//			...
//			case WAIT_OBJECT_0+n:
//				//	the built-in event (triggered by StartWork)
//				break;
//			case WAIT_TIMEOUT:
//				// a timeout occurred
//				break;
//			default:
//				ASSERT(false);
//				break;
//			}
//		}
//	If two events are pushed onto EventList, then the following events are possible:
//		WAIT_OBJECT_0			first event pushed
//		WAIT_OBJECT_0 + 1		second event pushed
//		WAIT_OBJECT_0 + 2		built-in event
//		WAIT_TIMEOUT			if SetCycleTime was called with something other than INFINITE
//
//	Use WaitFailed to catch event waiting errors (usually invalid handle - if a waitable object becomes invalid for some reason)
//		bool WaitFailed(DWORD dwError)
//		{
//			// dwError = error code from wait API
//			// return true to log error, false if error is not to be logged
//			return true;
//		}
//
//	To kill a thread, use KillThread() (from any thread). It will set a flag indicating that the
//	thread should kill itself and send an event to wake it up if it is blocked. KillThread
//	exits immediately. The thread will terminate at some future time.
//
//	To kill a thread and wait for it to terminate, use KillThreadWait(). DO NOT call this from
//	within the thread!
//

class CSimpleWorkerThread;

class CSimpleWorkerThreadEvents
{
	friend CSimpleWorkerThread;
private:
	CCriticalSection csKillThread;
	CCriticalSection csWorkEvent;
	CEvent* m_pWorkEvent;				// do work event
public:
	CSimpleWorkerThreadEvents()
	{
		m_pWorkEvent = nullptr;
	}

	~CSimpleWorkerThreadEvents()
	{
		CSingleLock lock(&csWorkEvent, true);
		if (m_pWorkEvent != nullptr)
		{
			delete m_pWorkEvent;
			m_pWorkEvent = nullptr;
		}
	}

	const CSimpleWorkerThreadEvents& operator =(const CSimpleWorkerThreadEvents& src)
	{
		if (&src == this)			// check for assignment over self
			return(*this);
		m_pWorkEvent = nullptr;
		return *this;
	};

	// copy constructor
	CSimpleWorkerThreadEvents(const CSimpleWorkerThreadEvents& src)
	{
		(void)src;
		m_pWorkEvent = nullptr;
	};
};

class ECSUTIL_EXT_CLASS CSimpleWorkerThread
{
private:
	CSimpleWorkerThreadEvents Events;
	CWinThread *pThread;
	FILETIME ftEndThreadTime;		// when a KillThread was received
	DWORD dwThreadID;
	HANDLE hThread;					// open handle for thread
	DWORD dwWaitTime;
	bool bRunning;
	bool bTerminate;
	bool bAlertable;
	bool bThreadInitialized;
	list<CEvent *> TermEventList;	// events to signal during process termination
	static UINT ThreadProc(LPVOID pParam);

	static CCriticalSection *pcsGlobalThreadSet;
	static set<CSimpleWorkerThread *> *pGlobalThreadSetActive;
	static set<CSimpleWorkerThread *> *pGlobalThreadSet;

protected:
	deque<CSyncObject *> EventList;
	DWORD dwEventRet;					// wait on event return
	void SignalTermination(void);

public:
	CSimpleWorkerThread();
	virtual ~CSimpleWorkerThread();
	bool CreateThread(
		UINT nStackSize = 0,
		LPSECURITY_ATTRIBUTES lpSecurityAttrs = nullptr,
		int nPriority = THREAD_PRIORITY_NORMAL);
	void StartWork();
	bool IfActive() const;
	virtual void DoWork(void) = 0;
	virtual bool WaitFailed(DWORD dwError);	// over-ride to catch any error after Multi-event wait
	virtual bool InitInstance(void);
	virtual void ExitInstance(void);
	void KillThread() throw();
	void KillThreadWait(bool bDontKillWaitOnly = false) throw();
	DWORD GetCurrentThreadID(void) const;
	void SetAlertable(bool bAlertableParam = true);
	bool GetExitFlag(void) const;
	UINT GetCycleTime(void) const;
	void SetCycleTime(UINT nMilliSecs);
	CString Format(void) const;
	CString GetThreadType(void) const;
	void GetTerminateTime(FILETIME *pftEndThreadTime) const;
	bool AddTermEvent(CEvent *pEvent);							// if return true, then event was queued, otherwise thread is dead and event not queued
	void RemoveTermEvent(CEvent *pEvent);
	void RemoveAllTermEvent(void);
	bool GetThreadInitialized(void) const;

	static void AllTerminate();
	static void DumpHandles(CString& sHandleMsg);
	static CSimpleWorkerThread *CurrentThread(void);
	static CCriticalSection *GetGlobalListCriticalSection(void);
};

template<class T> inline void KillThreadQueueSRW(list<T> &ThreadList, CRWLock& rwlThreadList, bool bDontKillThis = false)
{
	struct THREAD_EVENT
	{
		CEvent Event;
		CSimpleWorkerThread *pThread;
		THREAD_EVENT(CSimpleWorkerThread *pThreadParam = nullptr)
			: pThread(pThreadParam)
		{}
	};

	const UINT WaitLoopMax = 1000;
	DWORD dwThisThreadID = GetCurrentThreadId();
	UINT iWaitLoop;
	bool bAllStopped;
	for (;;)
	{
		// first notify all threads to kill themselves
		{
			CRWLockAcquire lock(&rwlThreadList, true);

			for (typename list<T>::iterator it = ThreadList.begin(); it != ThreadList.end(); ++it)
				if (!bDontKillThis || (dwThisThreadID != it->GetCurrentThreadID()))
					it->KillThread();
		}
		// next wait for them all to die
		iWaitLoop = 0;
		for (;;)
		{
			// set up event list to wait on
			HANDLE Events[60];								// wait on at most the first 60 entries in the list
			DWORD dwEvents = 0;
			list<THREAD_EVENT> InitEventList;					// initialized event list
			bAllStopped = false;
			iWaitLoop++;
			if (iWaitLoop > WaitLoopMax)
				break;
			bAllStopped = true;								//lint !e838 : previous value assigned to 'bAllStopped' not used
			{
				CRWLockAcquire lock(&rwlThreadList, true);

				for (typename list<T>::iterator it = ThreadList.begin(); it != ThreadList.end(); ++it)
				{
					if (!bDontKillThis || (dwThisThreadID != it->GetCurrentThreadID()))
					{
						if (it->IfActive())
						{
							bAllStopped = false;
							InitEventList.emplace_front(&(*it));
							if (it->AddTermEvent(&InitEventList.front().Event))
							{
								Events[dwEvents++] = InitEventList.front().Event.m_hObject;
								if (dwEvents >= _countof(Events))
									break;
							}
							else
								(void)InitEventList.erase(InitEventList.begin());				// thread died before queuing the event
						}
					}
				}
			}
			if (bAllStopped)
			{
				// remove the events from the thread objects
				for (typename list<THREAD_EVENT>::iterator it = InitEventList.begin(); it != InitEventList.end(); ++it)
					it->pThread->RemoveAllTermEvent();
				break;
			}
			if (dwEvents > 0)
			{
				// wait on ALL events
				// since this is testing at most 60 there is a chance that even if all threads die, there may be additional ones
				DWORD dwResult = ::WaitForMultipleObjects(dwEvents, Events, TRUE, SECONDS(1));
				// now remove the events
				for (typename list<THREAD_EVENT>::iterator it = InitEventList.begin(); it != InitEventList.end(); ++it)
					it->pThread->RemoveAllTermEvent();
				if (dwResult == WAIT_FAILED)
					break;									// something is seriously wrong
			}
		}
		if (bAllStopped)
			break;
	}
	if (!bDontKillThis)
	{
		CRWLockAcquire lock(&rwlThreadList, true);
		ThreadList.clear();
	}
}
