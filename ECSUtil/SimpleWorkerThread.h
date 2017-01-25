//
// Copyright (C) 1994 - 2012 EMC Corporation
// All rights reserved.
//

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
		m_pWorkEvent = NULL;
	}

	~CSimpleWorkerThreadEvents()
	{
		CSingleLock lock(&csWorkEvent, true);
		if (m_pWorkEvent != NULL)
		{
			delete m_pWorkEvent;
			m_pWorkEvent = NULL;
		}
	}

	const CSimpleWorkerThreadEvents& operator =(const CSimpleWorkerThreadEvents& src)
	{
		if (&src == this)			// check for assignment over self
			return(*this);
		m_pWorkEvent = NULL;
		return *this;
	};

	// copy constructor
	CSimpleWorkerThreadEvents(const CSimpleWorkerThreadEvents& src)
	{
		(void)src;
		m_pWorkEvent = NULL;
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
	static UINT ThreadProc(LPVOID pParam);

	static CCriticalSection *pcsGlobalThreadSet;
	static set<CSimpleWorkerThread *> *pGlobalThreadSetActive;
	static set<CSimpleWorkerThread *> *pGlobalThreadSet;

protected:
	deque<CSyncObject *> EventList;
	DWORD dwEventRet;					// wait on event return

public:
	CSimpleWorkerThread();
	virtual ~CSimpleWorkerThread();
	bool CreateThread(
		UINT nStackSize = 0,
		LPSECURITY_ATTRIBUTES lpSecurityAttrs = NULL,
		int nPriority = THREAD_PRIORITY_NORMAL);
	void StartWork();
	bool IfActive() const;
	virtual void DoWork(void) = 0;
	virtual bool WaitFailed(DWORD dwError);	// over-ride to catch any error after Multi-event wait
	virtual bool InitInstance(void);
	virtual void ExitInstance(void);
	void KillThread() throw();
	void KillThreadWait() throw();
	DWORD GetCurrentThreadID(void) const;
	void SetAlertable(bool bAlertableParam = true);
	bool GetExitFlag(void) const;
	UINT GetCycleTime(void) const;
	void SetCycleTime(UINT nMilliSecs);
	CString Format(void) const;
	CString GetThreadType(void) const;
	void GetTerminateTime(FILETIME *pftEndThreadTime) const;

	static void DumpHandles(CString& sHandleMsg);
	static CSimpleWorkerThread *CurrentThread(void);
	static CCriticalSection *GetGlobalListCriticalSection(void);
};

template<class T> inline void KillThreadQueueSRW(list<T> &ThreadList, CRWLock& rwlThreadList, bool bDontKillThis = false)
{
	const UINT WaitLoopMax = 1000;
	DWORD dwThisThreadID = GetCurrentThreadId();
	UINT iWaitLoop;
	bool bAllStopped;
	for (;;)
	{
		// first notify all threads to kill themselves
		{
			CRWLockAcquire lock(&rwlThreadList, true);

			for (list<T>::iterator itCfg = ThreadList.begin(); itCfg != ThreadList.end(); ++itCfg)
				if (!bDontKillThis || (dwThisThreadID != itCfg->GetCurrentThreadID()))
					itCfg->KillThread();
		}
		// next wait for them all to die
		iWaitLoop = 0;
		for (;;)
		{
			bAllStopped = false;
			iWaitLoop++;
			if (iWaitLoop > WaitLoopMax)
				break;
			bAllStopped = true;
			{
				CRWLockAcquire lock(&rwlThreadList, true);

				for (list<T>::iterator itCfg = ThreadList.begin(); itCfg != ThreadList.end(); ++itCfg)
					if (!bDontKillThis || (dwThisThreadID != itCfg->GetCurrentThreadID()))
						if (itCfg->IfActive())
						{
							bAllStopped = false;
							break;
						}
			}
			if (bAllStopped)
				break;
			Sleep(10);					// give it a tiny bit of time
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
