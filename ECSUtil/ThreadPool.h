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

#pragma once


#include "exportdef.h"

#include "cbuffer.h"
#include "widestring.h"
#include "CSharedQueue.h"
#include "SimpleWorkerThread.h"

namespace ecs_sdk
{


// used for WaitForWorkFinished
// returns true if it should abort and not wait for all work items to finish
typedef bool (*THREADPOOL_ABORT_CB)(void *pContext);

// callback for SearchMessageBackModify
// proc can set bDelete to delete the found object, bModify means it has been modified
// returns true if the search should end
typedef bool (*SEARCH_BACK_MODIFY_CB)(void *pMsg, bool& bDelete, bool& bModify, bool& bRewind, void *pContext);

template <class MsgT>
class CThreadPool;

const UINT MAX_QUEUE_SIZE_INFINITE = 0xffffffff;		// use in SendMessageToPool for dwMaxQueueSize if you never want it to block

// ********************************
// if you derive a class from CThreadPool<Msg>, you MUST have a destructor where the first
// statement is:
//		CThreadPool<Msg>::Terminate()
// this will stop all of the threads BEFORE running the destructor for your derived class
// otherwise, if the threads depend on anything in the derived class, it will probably crash
// ********************************

class ECSUTIL_EXT_CLASS CThreadPoolBase
{
private:
	static bool bPoolInitialized;				// no threads will process until set to true
	static CCriticalSection *pcsGlobalCThreadPool;
	static set<CThreadPoolBase *> *pGlobalCThreadPool;
	virtual void GarbageCollect(void) = 0;
	virtual CString FormatEntry(void) = 0;
	virtual void Terminate(void) = 0;

protected:
	static CSimpleRWLock rwlPerfDummy;	// dummy critical section if no CS is specified
	void RegisterCThreadPool();
	void UnregisterCThreadPool();

public:
	CThreadPoolBase() {};
	virtual ~CThreadPoolBase() {};
	static void SetPoolInitialized(void)
	{
		bPoolInitialized = true;
	}
	static bool GetPoolInitialized(void)
	{
		return bPoolInitialized;
	}
	static void GlobalGarbageCollect(void);
	static void DumpPools(CString *pDumpMsg);
	static void AllTerminate(void);
};

// CThreadWork
// each thread entry in a thread pool
template <class MsgT>
class CThreadWork : public CSimpleWorkerThread
{
	friend CThreadPool<MsgT>;
private:
	bool bInitialized;
	bool bInUse;				// set if allocated to a socket
	UINT uGrouping;				// from msg entry
	shared_ptr<MsgT> Msg;		// message currently being worked on
	FILETIME ftIdleTime;		// set with the current time when the thread is finished processing
	CSharedQueueEvent MsgEvent;	// event that a new message arrived

public:
	CThreadPool<MsgT> *pThreadPool;	// pointer to thread pool class that contains the message queue

	CThreadWork();
	~CThreadWork();
	bool InitInstance();
	void ExitInstance();
	virtual void DoWork();
	bool IsInUse(void) const;
	void SetIdle(void);
	void SetInUse(void);
	bool IfThreadIdleTooLong(void) const;
	void StartWorkerThread(DWORD dwStackSize, int iPriority);
	bool IsInitialized(void) const
	{
		return bInitialized;
	}
	CThreadWork& operator=(const CThreadWork& src)
	{
		ASSERT(false);
		return(*this);
	}
};

template <class MsgT>
class CThreadPool : public CThreadPoolBase
{
	friend CThreadWork<MsgT>;
public:
	typedef void(*UPDATE_METADATA_CB)(const MsgT& Record, UINT *puPriority, UINT *puGrouping);

	enum ENUM_THREAD_POOL_GROUPING {
		THREAD_POOL_MSG_GROUPING_DONT_CARE = 0,
		THREAD_POOL_MSG_GROUPING_CATEGORY_BASE = 1,
		THREAD_POOL_MSG_GROUPING_ALONE_BASE = 1000,
	};

	struct CThreadPoolEvents
	{
		CEvent evEmptyEvent;

		CThreadPoolEvents()
			: evEmptyEvent(FALSE, TRUE)					// make event manual reset
		{}

		// copy constructor
		CThreadPoolEvents(const CThreadPoolEvents& src)
			: evEmptyEvent(FALSE, TRUE)					// make event manual reset
		{
			(void)src;
		};

		const CThreadPoolEvents& operator =(const CThreadPoolEvents& src)
		{
			(void)src;
			return *this;
		};		//lint !e1539	// members not assigned by assignment operator
	};

protected:
	struct CMsgEntryControl
	{
		bool bProcessing;			// if set, currently processing
		UINT uPriority;				// the higher the number, the higher the priority
		UINT uLineNo;				// line number where the file was queued
		UINT uGrouping;				// if THREAD_POOL_MSG_GROUPING_DONT_CARE, no particular grouping. if != THREAD_POOL_MSG_GROUPING_DONT_CARE, only like groups will execute together. if > THREAD_POOL_MSG_GROUPING_ALONE, always execute alone
		ULONGLONG ullUniqueID;		// unique identifier for this record
		FILETIME ftDueTime;			// the soonest this should be put on the message queue, or 0 (now). Only used on FutureMsgQueue
		CMsgEntryControl()
			: bProcessing(false)
			, uPriority(0)
			, uLineNo(0)
			, uGrouping(THREAD_POOL_MSG_GROUPING_DONT_CARE)
		{
			ZeroFT(ftDueTime);
			ullUniqueID = GetUniqueInt64();
		}
	};
	struct CMsgEntry
	{
		CMsgEntryControl Control;	// control flags
		shared_ptr<MsgT> Payload;
		CMsgEntry()
		{}
	};

private:
	DWORD dwWorkerThreadStackSize;	// if zero, use default
	int iThreadPriority;			// thread priority class
	DWORD dwMaxNumThreads;			// maximum number of threads we can have
	DWORD dwMinNumThreads;			// don't go below this number
	DWORD dwMaxNumThreadsRange;		// if range defined, this is the real max, if the queue is very large
	DWORD dwQueueRangeMin;
	DWORD dwQueueRangeMax;
	DWORD dwPerfDummyNumThreads;	// if the real perfmon ptr is not provided, point it here
	DWORD dwPerfDummyQueueSize;
	DWORD dwPerfDummyQueueMax;
	DWORD dwPerfDummyQueueRateIn;
	DWORD dwPerfDummyQueueRateOut;
	DWORD dwPerfDummyWorkItems;
	DWORD dwPerfDummyOverflow;
	CSimpleRWLock *prwlPerf;		// critical section to acquire before modifying the perf counters
	DWORD *pdwPerfNumThreads;		// pointer to perfmon counter keeping track of the number of threads
	DWORD *pdwPerfQueueSize;
	DWORD *pdwPerfQueueMax;
	DWORD *pdwPerfQueueRateIn;
	DWORD *pdwPerfQueueRateOut;
	DWORD *pdwPerfWorkItems;
	DWORD *pdwPerfOverflow;
	DWORD dwMaxConcurrentThreads;
	bool bDisable;					// disable is set when Terminate is called to prevent additional work items
	CThreadPoolEvents Events;
	CSharedQueue<CMsgEntry> MsgQueue;
	list<CMsgEntry> FutureMsgQueue;				// use the lock in MsgQueue. both MsgQueue and FutureMsgQueue use the same lock
	CSharedQueue<CThreadWork<MsgT>> Pool;

	DWORD StartNewThread(void);
	bool IsMsgQueueEmpty() const
	{
		return MsgQueue.empty();
	}
	void UpdatePerf(UINT uLine, std::shared_ptr<MsgT> *pMsg, CThreadWork<MsgT> *pResetIdle, UINT uPriority, UINT uGrouping, DWORD dwDelay);
	virtual bool CompareEntry(const MsgT& Msg1, const MsgT& Msg2, UINT uSearchType) const;
	bool IfInGroup(UINT uGrouping) const;
	DWORD GetNumThreadsInternal(DWORD *pdwWorkItems = nullptr, DWORD dwIgnoreCurrentThreadID = 0, bool bExclusive = false) const;
	void AddThreads(DWORD *pdwRealMaxThreads = nullptr);
	void GarbageCollect(void);
	void DeleteOldThreadEntries(void);
	CString FormatEntry(void);
	DWORD GetMinNumThreadsInternal(void);
	void TransferFutureQueue(bool bFlush);

public:
	CThreadPool();
	virtual ~CThreadPool();
	void SetMinThreads(DWORD dwMinThreads);
	DWORD GetMinThreads(void) const;
	void SetMaxThreads(DWORD dwMaxThreads);
	void SetThreadRange(DWORD dwMaxNumThreadsRangeParam, DWORD dwQueueRangeMinParam, DWORD dwQueueRangeMaxParam);
	DWORD GetMaxThreads(void) const;
	void SetStackSize(DWORD dwStackSize);
	void SetThreadPriority(int iThreadPriorityParam);
	void SendMessageToPool(UINT uLine, std::shared_ptr<MsgT>& Msg, DWORD dwMaxQueueSize, UINT uPriority, const CSimpleWorkerThread *pThread, UINT uGrouping = THREAD_POOL_MSG_GROUPING_DONT_CARE, bool *pbNoBlock = nullptr, DWORD dwDelay = 0);
	void SendMessageToPoolDelayed(UINT uLine, DWORD dwDueTime, std::shared_ptr<MsgT>& Msg, UINT uPriority, UINT uGrouping = THREAD_POOL_MSG_GROUPING_DONT_CARE);
	virtual bool DoProcess(const CSimpleWorkerThread *pThread, const MsgT& Msg) = 0;
	void Terminate(void);
	void SetPerfCounter(CSimpleRWLock *prwlPerfParam, DWORD *pdwPerfNumThreadsParam, DWORD *pdwPerfQueueSizeParam, DWORD *pdwPerfQueueMaxParam, DWORD *pdwPerfQueueRateInParam, DWORD *pdwPerfQueueRateOutParam, DWORD *pdwPerfWorkItemsParam, DWORD *pdwPerfOverflowParam);
	DWORD SearchMessageBackModify(const MsgT& MsgFind, UINT uSearchType, SEARCH_BACK_MODIFY_CB FoundProc, void *pContext);
	bool SearchMessage(const MsgT& MsgFind, MsgT *pPayloadRet, UINT uSearchType, bool bInProcess = true) const;
	bool SearchMessageKill(const MsgT& MsgFind, UINT uSearchType);
	DWORD GetNumThreads(DWORD *pdwWorkItems = nullptr) const;
	void DumpQueue(void *pContext) const;
	virtual bool DumpEntry(bool bInProcess, const MsgT& Message, void *pContext) const;
	DWORD GetMsgQueueCount() const;
	void GetCurrentWorkItems(list<MsgT>& RetList) const;
	void WaitForWorkFinished(THREADPOOL_ABORT_CB AbortProc, void *pContext, DWORD dwWaitMillisec = 500);
	DWORD GetMaxConcurrentThreads(bool bReset = false);
	void DiscardPool(void) throw();
};

template <class MsgT>
CThreadWork<MsgT>::CThreadWork()
	: bInitialized(false)
	, bInUse(false)
	, uGrouping(CThreadPool<MsgT>::THREAD_POOL_MSG_GROUPING_DONT_CARE)
	, pThreadPool(nullptr)
{
	GetSystemTimeAsFileTime(&ftIdleTime);
};

template <class MsgT>
CThreadWork<MsgT>::~CThreadWork()
{
	KillThreadWait();
	pThreadPool = nullptr;
};

template <class MsgT>
bool CThreadWork<MsgT>::IsInUse(void) const
{
	return bInUse;
}

template <class MsgT>
void CThreadWork<MsgT>::SetIdle(void)
{
	bInUse = false;
	GetSystemTimeAsFileTime(&ftIdleTime);
}


template <class MsgT>
void CThreadWork<MsgT>::SetInUse(void)
{
	CRWLockAcquire lock(&pThreadPool->Pool.GetLock(), false);		// read lock
	{
		CRWLockAcquire lockMsg(&pThreadPool->MsgQueue.GetLock(), true);	// write lock
		bInUse = true;
		DWORD dwWorkCount = 0;
		for (typename CSharedQueue<CThreadWork<MsgT>>::iterator itPool = pThreadPool->Pool.begin(); itPool != pThreadPool->Pool.end(); ++itPool)
			if (itPool->bInUse)
				dwWorkCount++;
		if (pThreadPool->pdwPerfWorkItems != nullptr)
		{
			CSimpleRWLockAcquire lockSharedMem(pThreadPool->prwlPerf);
			*pThreadPool->pdwPerfWorkItems = dwWorkCount;
		}
	}
}

template <class MsgT>
bool CThreadWork<MsgT>::IfThreadIdleTooLong(void) const
{
	if (bInUse)
		return false;
	FILETIME ftNow;
	GetSystemTimeAsFileTime(&ftNow);
	if (IfFTZero(ftIdleTime) || ((ftIdleTime + FT_SECONDS(10)) > ftNow))
		return false;
	return true;
}

template <class MsgT>
void CThreadWork<MsgT>::StartWorkerThread(DWORD dwStackSize, int iPriority)
{
	(void)CreateThread(dwStackSize, nullptr, iPriority);
};

template <class MsgT>
bool CThreadWork<MsgT>::InitInstance()
{
	if (!CSimpleWorkerThread::InitInstance())
		return false;
	while (!GetExitFlag() && ((pThreadPool == nullptr) || !CThreadPoolBase::GetPoolInitialized()))
		Sleep(500);
	(void)CoInitialize(nullptr);
	SetCycleTime(SECONDS(5));
	StartWork();
	return true;		// return FALSE to abort the thread
}

template <class MsgT>
void CThreadWork<MsgT>::DoWork()
{
	DWORD dwWaitTime = 0;
	ASSERT(pThreadPool != nullptr);
	switch (dwEventRet)
	{
	case WAIT_OBJECT_0:
	case WAIT_OBJECT_0+1:
	case WAIT_TIMEOUT:
		while (!GetExitFlag() && !pThreadPool->bDisable)
		{
			typename CThreadPool<MsgT>::CMsgEntry *pMsg = nullptr;			// message currently being worked on
			{
				CRWLockAcquire lock(&pThreadPool->Pool.GetLock(), true);	// write lock
				{
					CRWLockAcquire lockMsg(&pThreadPool->MsgQueue.GetLock(), true);	// write lock
					// find the latest that is not being processed
					typename CSharedQueue<typename CThreadPool<MsgT>::CMsgEntry>::iterator itMsg;
					for (itMsg = pThreadPool->MsgQueue.begin(); itMsg != pThreadPool->MsgQueue.end(); ++itMsg)
						if (!itMsg->Control.bProcessing)
							break;
					if (itMsg == pThreadPool->MsgQueue.end())
						break;
					pMsg = &(*itMsg);
					Msg = pMsg->Payload;
					uGrouping = pMsg->Control.uGrouping;
					if (!pThreadPool->IfInGroup(pMsg->Control.uGrouping))
						break;
					if (!IfFTZero(pMsg->Control.ftDueTime))
					{
						ULARGE_INTEGER DueTime = FTtoULarge(pMsg->Control.ftDueTime);
						FILETIME ftNow;
						GetSystemTimeAsFileTime(&ftNow);
						// figure out how long to wait
						ULARGE_INTEGER Now = FTtoULarge(ftNow);
						if (Now.QuadPart < DueTime.QuadPart)
						{
							dwWaitTime = (DWORD)(((DueTime.QuadPart - Now.QuadPart) * SECONDS(1)) / FT_SECONDS(1));
							break;
						}
					}
					pMsg->Control.bProcessing = true;
					SetInUse();
					// no need to update perf counters since popping the message and then
					// setting the entry in use cancel each other out
				}
			}
			DWORD dwRealMaxThreads = pThreadPool->dwMaxNumThreads;
			pThreadPool->AddThreads(&dwRealMaxThreads);
			bool bProcessed = pThreadPool->DoProcess(this, *(Msg.get()));
			if (bProcessed && (pThreadPool->pdwPerfQueueRateOut != nullptr))
			{
				CSimpleRWLockAcquire lockSharedMem(pThreadPool->prwlPerf);
				InterlockedIncrement(pThreadPool->pdwPerfQueueRateOut);
			}
			{
				// message processed, remove it from the queue
				CRWLockAcquire lock(&pThreadPool->Pool.GetLock(), true);		// write lock
				{
					DWORD dwRunningThreads = pThreadPool->GetNumThreads();
					if (dwRunningThreads > pThreadPool->dwMaxConcurrentThreads)
						pThreadPool->dwMaxConcurrentThreads = dwRunningThreads;
					CRWLockAcquire lockMsg(&pThreadPool->MsgQueue.GetLock(), true);		// write lock
					if (bProcessed)
					{
						typename CSharedQueue<typename CThreadPool<MsgT>::CMsgEntry>::iterator itMsg;
						for (itMsg = pThreadPool->MsgQueue.begin(); itMsg != pThreadPool->MsgQueue.end(); ++itMsg)
							if (&(*itMsg) == pMsg)
							{
								pThreadPool->MsgQueue.erase(itMsg);
								pThreadPool->MsgQueue.TriggerEvent(pThreadPool->MsgQueue.GetCount(), TRIGGEREVENTS_DELETE);
								if (pThreadPool->MsgQueue.empty())
									VERIFY(pThreadPool->Events.evEmptyEvent.SetEvent());
								break;
							}
					}
					else
						pMsg->Control.bProcessing = false;
				}
			}
			// first set entry idle, then update counters
			pThreadPool->UpdatePerf(1, nullptr, this, 0, 0, 0);
			// now check if someone reduced the max allowed number of threads
			// if we are over the limit, terminate this thread
			if (pThreadPool->GetNumThreads() > dwRealMaxThreads)
			{
				KillThread();				// kill my thread
				break;
			}
			if (!bProcessed)
				break;
		}
		if (dwEventRet == WAIT_TIMEOUT)
		{
			// do some housekeeping - check if any threads need to be terminated
			bool bFutureEmpty;
			{
				CRWLockAcquire lock(&pThreadPool->MsgQueue.GetLock(), false);		// read lock
				bFutureEmpty = pThreadPool->FutureMsgQueue.empty();
			}
			if (bFutureEmpty)
			{
				CRWLockAcquire lock(&pThreadPool->Pool.GetLock(), true);		// write lock
				DWORD dwNumRunning = pThreadPool->GetNumThreadsInternal();
				if ((dwNumRunning > pThreadPool->GetMinNumThreadsInternal()) && IfThreadIdleTooLong())
					KillThread();				// kill my thread
			}
			// check if there are any delayed work items that have come due
			{
				CRWLockAcquire lock(&pThreadPool->MsgQueue.GetLock(), false);		// read lock
				bFutureEmpty = pThreadPool->FutureMsgQueue.empty();
			}
			if (!bFutureEmpty)
			{
				pThreadPool->TransferFutureQueue(false);
			}
		}
		if ((dwWaitTime > 0) && (dwWaitTime < SECONDS(5)))
			SetCycleTime(dwWaitTime);
		else
			SetCycleTime(SECONDS(5));
		break;
	default:
		ASSERT(false);
		break;
	}
}

template <class MsgT>
void CThreadWork<MsgT>::ExitInstance()
{
	if (pThreadPool != nullptr)
		MsgEvent.Unlink(&pThreadPool->MsgQueue);
	EventList.clear();
	bInitialized = false;
	bInUse = false;
	CoUninitialize();
	// update the count of active threads. make it ignore this thread as it is going down
	if (pThreadPool != nullptr)
		(void)pThreadPool->GetNumThreadsInternal(nullptr, GetCurrentThreadId());
}

template <class MsgT>
CThreadPool<MsgT>::CThreadPool()
	: dwWorkerThreadStackSize(0)
	, iThreadPriority(THREAD_PRIORITY_NORMAL)
	, dwMaxNumThreads(20)
	, dwMinNumThreads(4)
	, dwMaxNumThreadsRange(0)
	, dwQueueRangeMin(0)
	, dwQueueRangeMax(0)
	, dwPerfDummyNumThreads(0)
	, dwPerfDummyQueueSize(0)
	, dwPerfDummyQueueMax(0)
	, dwPerfDummyQueueRateIn(0)
	, dwPerfDummyQueueRateOut(0)
	, dwPerfDummyWorkItems(0)
	, dwPerfDummyOverflow(0)
	, prwlPerf(&rwlPerfDummy)
	, pdwPerfNumThreads(&dwPerfDummyNumThreads)
	, pdwPerfQueueSize(&dwPerfDummyQueueSize)
	, pdwPerfQueueMax(&dwPerfDummyQueueMax)
	, pdwPerfQueueRateIn(&dwPerfDummyQueueRateIn)
	, pdwPerfQueueRateOut(&dwPerfDummyQueueRateOut)
	, pdwPerfWorkItems(&dwPerfDummyWorkItems)
	, pdwPerfOverflow(&dwPerfDummyOverflow)
	, dwMaxConcurrentThreads(0)
	, bDisable(false)
{
	RegisterCThreadPool();
}

template <class MsgT>
CThreadPool<MsgT>::~CThreadPool()
{
	UnregisterCThreadPool();
	pdwPerfNumThreads = nullptr;
	pdwPerfQueueSize = nullptr;
	pdwPerfQueueMax = nullptr;
	pdwPerfWorkItems = nullptr;
	CThreadPool<MsgT>::Terminate();
}

// return true if new message can be run with the currently executing threads
// MUST HAVE POOL CS
template <class MsgT>
bool CThreadPool<MsgT>::IfInGroup(UINT uGrouping) const
{
	for (typename CSharedQueue<CThreadWork<MsgT>>::const_iterator itPool = Pool.begin(); itPool != Pool.end(); ++itPool)
	{
		if (itPool->bInUse)
		{
			if ((uGrouping != THREAD_POOL_MSG_GROUPING_DONT_CARE)
				|| (itPool->uGrouping != THREAD_POOL_MSG_GROUPING_DONT_CARE))
			{
				if ((uGrouping >= THREAD_POOL_MSG_GROUPING_ALONE_BASE)
					|| (itPool->uGrouping >= THREAD_POOL_MSG_GROUPING_ALONE_BASE))
					return false;
				if (itPool->uGrouping != uGrouping)
					return false;
			}
		}
	}
	return true;
}

template <class MsgT>
void CThreadPool<MsgT>::SetMinThreads(DWORD dwMinThreads)
{
	dwMinNumThreads = dwMinThreads;
}

template <class MsgT>
DWORD CThreadPool<MsgT>::GetMinThreads(void) const
{
	return dwMinNumThreads;
}

template <class MsgT>
void CThreadPool<MsgT>::SetMaxThreads(DWORD dwMaxThreads)
{
	dwMaxNumThreads = dwMaxThreads;
}

template <class MsgT>
void CThreadPool<MsgT>::SetThreadRange(DWORD dwMaxNumThreadsRangeParam, DWORD dwQueueRangeMinParam, DWORD dwQueueRangeMaxParam)
{
	dwMaxNumThreadsRange = dwMaxNumThreadsRangeParam;
	dwQueueRangeMin = dwQueueRangeMinParam;
	dwQueueRangeMax = dwQueueRangeMaxParam;
}

template <class MsgT>
DWORD CThreadPool<MsgT>::GetMaxThreads(void) const
{
	return dwMaxNumThreads;
}

template <class MsgT>
void CThreadPool<MsgT>::SetStackSize(DWORD dwStackSize)
{
	dwWorkerThreadStackSize = dwStackSize;
}

template <class MsgT>
void CThreadPool<MsgT>::SetThreadPriority(int iThreadPriorityParam)
{
	iThreadPriority = iThreadPriorityParam;
}

template <class MsgT>
void CThreadPool<MsgT>::SetPerfCounter(
	CSimpleRWLock *prwlPerfParam,
	DWORD *pdwPerfNumThreadsParam,
	DWORD *pdwPerfQueueSizeParam,
	DWORD *pdwPerfQueueMaxParam,
	DWORD *pdwPerfQueueRateInParam,
	DWORD *pdwPerfQueueRateOutParam,
	DWORD *pdwPerfWorkItemsParam,
	DWORD *pdwPerfOverflowParam)
{
	if (prwlPerfParam == nullptr)
		prwlPerf = &rwlPerfDummy;
	else
		prwlPerf = prwlPerfParam;
	if (pdwPerfNumThreadsParam == nullptr)
		pdwPerfNumThreads = &dwPerfDummyNumThreads;
	else
		pdwPerfNumThreads = pdwPerfNumThreadsParam;
	if (pdwPerfQueueSizeParam == nullptr)
		pdwPerfQueueSize = &dwPerfDummyQueueSize;
	else
		pdwPerfQueueSize = pdwPerfQueueSizeParam;
	if (pdwPerfQueueMaxParam == nullptr)
		pdwPerfQueueMax = &dwPerfDummyQueueMax;
	else
		pdwPerfQueueMax = pdwPerfQueueMaxParam;
	if (pdwPerfQueueRateInParam == nullptr)
		pdwPerfQueueRateIn = &dwPerfDummyQueueRateIn;
	else
		pdwPerfQueueRateIn = pdwPerfQueueRateInParam;
	if (pdwPerfQueueRateOutParam == nullptr)
		pdwPerfQueueRateOut = &dwPerfDummyQueueRateOut;
	else
		pdwPerfQueueRateOut = pdwPerfQueueRateOutParam;
	if (pdwPerfWorkItemsParam == nullptr)
		pdwPerfWorkItems = &dwPerfDummyWorkItems;
	else
		pdwPerfWorkItems = pdwPerfWorkItemsParam;
	if (pdwPerfWorkItemsParam == nullptr)
		pdwPerfOverflow = &dwPerfDummyOverflow;
	else
		pdwPerfOverflow = pdwPerfOverflowParam;
	bDisable = false;
}

// GetNumThreads
// get the number of active threads
// optionally get the number of items currently being worked on
template <class MsgT>
DWORD CThreadPool<MsgT>::GetNumThreads(DWORD *pdwWorkItems) const
{
	return GetNumThreadsInternal(pdwWorkItems, 0);
}

// GetNumThreadsInternal
// get the number of active threads, not including threads that are scheduled to be terminated
// optionally get the number of items currently being worked on
// internal version allows a thread ID to be excluded from the count
template <class MsgT>
DWORD CThreadPool<MsgT>::GetNumThreadsInternal(
	DWORD *pdwWorkItems,							// returns number of items in queue including items being processed
	DWORD dwIgnoreCurrentThreadID,					// if non-zero, exclude that thread from the count
	bool bExclusive) const							// if set, get exclusive lock to get an absolute accurate thread count
{
	CRWLockAcquire lock(&Pool.GetLock(), bExclusive);	// read lock or write lock if bExclusive is set
	DWORD dwCount = MsgQueue.GetCount();			// get current length of queue
	DWORD dwThreads = 0;
	for (typename CSharedQueue<CThreadWork<MsgT>>::const_iterator itPool = Pool.begin(); itPool != Pool.end(); ++itPool)
	{
		if ((dwIgnoreCurrentThreadID == 0) || (itPool->GetCurrentThreadID() != dwIgnoreCurrentThreadID))
		{
			if (itPool->IfActive() && !itPool->GetExitFlag())
				dwThreads++;
		}
	}
	if (pdwWorkItems != nullptr)
		*pdwWorkItems = dwCount;
	if (pdwPerfNumThreads != nullptr)
	{
		CSimpleRWLockAcquire lockSharedMem(prwlPerf);
		*pdwPerfNumThreads = dwThreads;
	}
	return dwThreads;
}

template <class MsgT>
void CThreadPool<MsgT>::GetCurrentWorkItems(list<MsgT>& RetList) const
{
	CRWLockAcquire lock(&Pool.GetLock(), false);	// read lock
	for (typename CSharedQueue<CThreadWork<MsgT>>::const_iterator itPool = Pool.begin(); itPool != Pool.end(); ++itPool)
		if (itPool->IfActive() && itPool->bInUse)
		{
			RetList.push_back(*itPool->Msg);
		}
}

template <class MsgT>
DWORD CThreadPool<MsgT>::GetMsgQueueCount() const
{
	return MsgQueue.GetCount();
}

template <class MsgT>
DWORD CThreadPool<MsgT>::StartNewThread(void)
{
	typename CSharedQueue<CThreadWork<MsgT>>::iterator itWork;
	{
		CRWLockAcquire lock(&Pool.GetLock(), true);	// write lock
		for (itWork = Pool.begin(); itWork != Pool.end(); ++itWork)
			if (!itWork->IfActive())
				break;
		if (itWork != Pool.end())
		{
			itWork->bInitialized = itWork->bInUse = false;
		}
		else
		{
			Pool.emplace_back(CThreadWork<MsgT>());
			itWork = Pool.end();
			itWork--;
			// now start thread
		}
		itWork->pThreadPool = this;
		itWork->EventList.clear();
		itWork->bInUse = false;
		itWork->MsgEvent.Link(&MsgQueue);
		itWork->MsgEvent.DisableAllTriggerEvents();
		itWork->MsgEvent.EnableTriggerEvents(TRIGGEREVENTS_PUSH | TRIGGEREVENTS_INSERTAT);
		itWork->MsgEvent.SetAllEvents();
		itWork->MsgEvent.Enable();
		itWork->EventList.push_back(&itWork->MsgEvent.Event.evQueue);
	}
	itWork->StartWorkerThread(dwWorkerThreadStackSize, iThreadPriority);
	itWork->bInitialized = true;
	return itWork->GetCurrentThreadID();
}

template <class MsgT>
void CThreadPool<MsgT>::SendMessageToPool(
	UINT uLine,								// used for debugging
	std::shared_ptr<MsgT>& Msg,				// message to queue
	DWORD dwMaxQueueSize,					// max queue size. 0 for infinite
	UINT uPriority,							// message priority
	const CSimpleWorkerThread *pThread,		// thread object, used to determine if thread is being shut down while waiting for dwMaxQueueSize
	UINT uGrouping,							// group code
	bool *pbNoBlock,						// if non-null, return without processing if it would block. return 'true' if it didn't process msg
	DWORD dwDelay)							// delay in millisec. this won't be processed until the delay is satisfied
{
	if ((pThread != nullptr) && (dwMaxQueueSize == 0))
		dwMaxQueueSize = 10000;				// don't let it go to infinity
	// check if there is a possibility of a deadlock. If the current thread is one in the thread pool
	// then don't impose a limit on the queue size
	DWORD dwThreadID = GetCurrentThreadId();
	{
		CRWLockAcquire lockPool(&Pool.GetLock(), false);		// always lock pool first, then msg if you need both locked
		for (typename CSharedQueue<CThreadWork<MsgT>>::iterator itPool = Pool.begin(); itPool != Pool.end(); ++itPool)
		{
			if (itPool->GetCurrentThreadID() == dwThreadID)
				dwMaxQueueSize = 0;
		}
	}
	if ((dwMaxQueueSize != 0) && (MsgQueue.GetCount() > dwMaxQueueSize))
	{
		if (pbNoBlock != nullptr)
		{
			*pbNoBlock = true;
			return;
		}
		CSharedQueueEvent QueueEvent;
		QueueEvent.Link(&MsgQueue);
		QueueEvent.EnableTriggerEvents(TRIGGEREVENTS_DELETE);
		QueueEvent.Enable();
		QueueEvent.SetCount(dwMaxQueueSize);
		while (MsgQueue.GetCount() > dwMaxQueueSize)
		{
			if (pThread != nullptr)					// check if we are supposed to shut down
				if (pThread->GetExitFlag())
					break;
			CSingleLock lockEmpty(&QueueEvent.Event.evQueue);
			(void)lockEmpty.Lock(SECONDS(5));
			if (lockEmpty.IsLocked())
				VERIFY(lockEmpty.Unlock());
		}
	}
	// push message and then update counts
	UpdatePerf(uLine, &Msg, nullptr, uPriority, uGrouping, dwDelay);
	AddThreads();
}

template <class MsgT>
void CThreadPool<MsgT>::SendMessageToPoolDelayed(UINT uLine, DWORD dwDueTime, std::shared_ptr<MsgT>& Msg, UINT uPriority, UINT uGrouping)
{
	FILETIME ftNow;
	GetSystemTimeAsFileTime(&ftNow);
	CMsgEntry Rec;
	Rec.Control.uPriority = uPriority;
	Rec.Control.uLineNo = uLine;
	Rec.Payload = Msg;
	Rec.Control.uGrouping = uGrouping;
	Rec.Control.ftDueTime = ftNow + dwDueTime * (FT_SECOND / SECONDS(1));
	{
		CRWLockAcquire lock(&MsgQueue.GetLock(), true);		// write lock
		FutureMsgQueue.push_back(Rec);
	}
	AddThreads();
}

template <class MsgT>
void CThreadPool<MsgT>::AddThreads(DWORD *pdwRealMaxThreads)
{
	DWORD dwRealMaxThreads = dwMaxNumThreads;
	if (dwQueueRangeMin > 0)
	{
		DWORD dwCurrentQueueSize = GetMsgQueueCount() + *pdwPerfOverflow;
		if (dwCurrentQueueSize < dwQueueRangeMin)
			dwRealMaxThreads = dwMaxNumThreads;
		else if (dwCurrentQueueSize > dwQueueRangeMax)
			dwRealMaxThreads = dwMaxNumThreadsRange;
		else
		{
			if (dwQueueRangeMax > dwQueueRangeMin)				// protect against divide by zero
			{
				// if it's inside the range, figure out a proportional number of threads
				dwRealMaxThreads = (dwCurrentQueueSize - dwQueueRangeMin) * (dwMaxNumThreadsRange - dwMaxNumThreads) / (dwQueueRangeMax - dwQueueRangeMin);	//lint !e414 possible division by zero
				dwRealMaxThreads += dwMaxNumThreads;
			}
		}
	}
	if (pdwRealMaxThreads != nullptr)
		*pdwRealMaxThreads = dwRealMaxThreads;
	{
		CRWLockAcquire lock(&Pool.GetLock(), true);
		if (GetNumThreads() < dwRealMaxThreads)				// only go through this if there is a possibility of adding a thread
		{
			// if less than min, create some threads
			while (GetNumThreads() < GetMinNumThreadsInternal())
				(void)StartNewThread();
			// see if we'd benefit with more threads
			DWORD dwActive = GetNumThreadsInternal();
			CRWLockAcquire lockMsg(&MsgQueue.GetLock(), true);
			if (((dwRealMaxThreads - dwActive) > 0) && (MsgQueue.GetCount() > dwActive))
			{
				if (IfInGroup(MsgQueue.front().Control.uGrouping))
				{
					(void)StartNewThread();
				}
			}
		}
	}
}

// SearchMessageBackModify
// search message list going backwards through the most recent entries looking for specific message
// call callback if found, keeping write lock on the message queue
template <class MsgT>
DWORD CThreadPool<MsgT>::SearchMessageBackModify(const MsgT& MsgFind, UINT uSearchType, SEARCH_BACK_MODIFY_CB FoundProc, void *pContext)
{
	bool bDone = false;
	try
	{
		CRWLockAcquire lockPool(&Pool.GetLock(), false);		// always lock pool first, then msg if you need both locked
		{
			CRWLockAcquire lockMsg(&MsgQueue.GetLock(), true);
			bool bRewind;							// a callback can request that we rewind to the start to do it all over
			do
			{
				bRewind = false;							// a callback can request that we rewind to the start to do it all over
				bDone = false;

				for (typename CSharedQueue<CMsgEntry>::reverse_iterator itMsg = MsgQueue.rbegin(); !bDone && (itMsg != MsgQueue.rend()); )
				{
					bool bDelete = false;
					if (!itMsg->Control.bProcessing)									// if it is at the head of the list and being processed, don't deal with it
					{
						if (CompareEntry(MsgFind, *itMsg->Payload, uSearchType))
						{
							bool bModify = false;
							bool bDoRewind = false;
							bDone = FoundProc(&(*itMsg->Payload), bDelete, bModify, bDoRewind, pContext);
							if (bDoRewind)
								bRewind = bDone = true;						// rewind implies done
						}
					}
					if (bDelete)
					{
						MsgQueue.erase(--itMsg.base());
						{
							CSimpleRWLockAcquire lockSharedMem(prwlPerf);
							DWORD dwCount = MsgQueue.GetCount();
							if (pdwPerfQueueSize != nullptr)
								*pdwPerfQueueSize = dwCount;
							if ((pdwPerfQueueMax != nullptr) && (dwCount > *pdwPerfQueueMax))
								*pdwPerfQueueMax = dwCount;
						}
					}
					else
						++itMsg;
				}
			} while (bRewind);
		}
	}
	catch (const CErrorInfo& E)
	{
		return E.dwError;
	}
	return ERROR_SUCCESS;
}

template <class MsgT>
bool CThreadPool<MsgT>::SearchMessage(const MsgT& MsgFind, MsgT *pPayloadRet, UINT uSearchType, bool bInProcess) const
{
	CRWLockAcquire lockPool(&Pool.GetLock(), false);		// always lock pool first, then msg if you need both locked
	{
		CRWLockAcquire lockMsg(&MsgQueue.GetLock(), false);
		for (typename CSharedQueue<CMsgEntry>::const_iterator itMsg = MsgQueue.begin(); itMsg != MsgQueue.end(); ++itMsg)
		{
			if (bInProcess || !itMsg->Control.bProcessing)
			{
				if (CompareEntry(MsgFind, *itMsg->Payload, uSearchType))
				{
					if (pPayloadRet != nullptr)
						*pPayloadRet = *itMsg->Payload;
					return true;
				}
			}
		}
	}
	return false;
}

template <class MsgT>
bool CThreadPool<MsgT>::SearchMessageKill(const MsgT& MsgFind, UINT uSearchType)
{
	bool bFound = false;
	bool bRetry;
	CRWLockAcquire lockPool(&Pool.GetLock(), false);		// always lock pool first, then msg if you need both locked
	{
		CRWLockAcquire lockMsg(&MsgQueue.GetLock(), true);	// write lock
		do
		{
			bRetry = false;
			for (typename CSharedQueue<CMsgEntry>::iterator itMsg = MsgQueue.begin(); itMsg != MsgQueue.end(); ++itMsg)
			{
				if (!itMsg->Control.bProcessing)									// if it is at the head of the list and being processed, don't deal with it
				{
					if (CompareEntry(MsgFind, *itMsg->Payload, uSearchType))
					{
						MsgQueue.erase(itMsg);
						bFound = true;
						bRetry = true;
						if (pdwPerfQueueSize != nullptr)
						{
							CSimpleRWLockAcquire lockSharedMem(prwlPerf);
							*pdwPerfQueueSize = MsgQueue.GetCount();
						}
						break;
					}
				}
			}
		} while (bRetry);
	}
	return bFound;
}

template <class MsgT>
void CThreadPool<MsgT>::Terminate()
{
	bDisable = true;
	try
	{
		KillThreadQueueSRW(Pool, Pool.GetLock());
		TransferFutureQueue(true);						// save any entries that are on the Future queue
	}
	catch (const std::bad_array_new_length& e) {
		// shouldn't happen, but can't do anything about it here.
		std::ignore = e;
	}

	{
		CSimpleRWLockAcquire lockSharedMem(prwlPerf);
		if (pdwPerfNumThreads != nullptr)
			*pdwPerfNumThreads = 0;
		if (pdwPerfQueueSize != nullptr)
			*pdwPerfQueueSize = 0;
		if (pdwPerfQueueMax != nullptr)
			*pdwPerfQueueMax = 0;
		if (pdwPerfWorkItems != nullptr)
			*pdwPerfWorkItems = 0;
	}
}

// DiscardPool
// discard all entries in the queue
template <class MsgT>
void CThreadPool<MsgT>::DiscardPool() throw()
{
	bDisable = true;
	KillThreadQueueSRW(Pool, Pool.GetLock());
	{
		CRWLockAcquire lockPool(&Pool.GetLock(), false);		// always lock pool first, then msg if you need both locked
		{
			CRWLockAcquire lockMsg(&MsgQueue.GetLock(), true);	// write lock
			MsgQueue.clear();
			FutureMsgQueue.clear();
		}
	}
	{
		CSimpleRWLockAcquire lockSharedMem(prwlPerf);
		if (pdwPerfNumThreads != nullptr)
			*pdwPerfNumThreads = 0;
		if (pdwPerfQueueSize != nullptr)
			*pdwPerfQueueSize = 0;
		if (pdwPerfQueueMax != nullptr)
			*pdwPerfQueueMax = 0;
		if (pdwPerfWorkItems != nullptr)
			*pdwPerfWorkItems = 0;
	}
}

// update perf counters
// if bAdd - adding entry to queue, so count add rate
// if pResetIdle != nullptr, then, while locked, call SetIdle for that entry
template <class MsgT>
void CThreadPool<MsgT>::UpdatePerf(UINT uLine, std::shared_ptr<MsgT> *pMsg, CThreadWork<MsgT> *pResetIdle, UINT uPriority, UINT uGrouping, DWORD dwDelay)
{
	DWORD dwCount, dwWorkCount = 0;
	// always get the pool lock first, then msg lock if you need both
	{
		CRWLockAcquire lockPool(&Pool.GetLock(), false);		// always lock pool first, then msg if you need both locked
		{
			CRWLockAcquire lockMsg(&MsgQueue.GetLock(), true);	// write lock
			if (pResetIdle != nullptr)
				pResetIdle->SetIdle();
			if (pMsg != nullptr)
			{
				if (pdwPerfQueueRateIn != nullptr)
				{
					CSimpleRWLockAcquire lockSharedMem(prwlPerf);
					InterlockedIncrement(pdwPerfQueueRateIn);
				}
				CMsgEntry Rec;
				Rec.Control.uPriority = uPriority;
				Rec.Control.uLineNo = uLine;
				Rec.Payload = *pMsg;
				Rec.Control.uGrouping = uGrouping;
				if (dwDelay != 0)
				{
					FILETIME ftNow;
					GetSystemTimeAsFileTime(&ftNow);
					Rec.Control.ftDueTime = ftNow + ((ULONGLONG)dwDelay * (FT_SECONDS(1) / SECONDS(1)));
				}
				bool bInserted = false;
				for (typename CSharedQueue<CMsgEntry>::reverse_iterator itMsg = MsgQueue.rbegin(); itMsg != MsgQueue.rend(); itMsg++)
				{
					if (itMsg->Control.uPriority >= uPriority)
					{
						// insert it AFTER element 'i'
						MsgQueue.insert(itMsg.base(), Rec);
						bInserted = true;
						break;
					}
				}
				if (!bInserted)
					MsgQueue.push_front(Rec);			// put at the head of the queue
			}
			dwCount = MsgQueue.GetCount();			// get current length of queue
		}
	}
	{
		CSimpleRWLockAcquire lockSharedMem(prwlPerf);
		if (pdwPerfQueueSize != nullptr)
			*pdwPerfQueueSize = dwCount;
		if (pdwPerfWorkItems != nullptr)
			*pdwPerfWorkItems = dwWorkCount;
		if ((pdwPerfQueueMax != nullptr) && (dwCount > *pdwPerfQueueMax))
			*pdwPerfQueueMax = dwCount;
	}
}

// DumpQueue
// run through the queue from the the back of the queue to the front, ending with the entries currently being serviced
// don't spend too much time with this since the whole thing is locked up tight for the duration
template <class MsgT>
void CThreadPool<MsgT>::DumpQueue(void *pContext) const
{
	// always get the pool lock first, then msg lock if you need both
	{
		CRWLockAcquire lockPool(&Pool.GetLock(), false);		// always lock pool first, then msg if you need both locked
		{
			CRWLockAcquire lockMsg(&MsgQueue.GetLock(), false);	// read lock

			for (typename CSharedQueue<CMsgEntry>::const_iterator itMsg = MsgQueue.begin(); itMsg != MsgQueue.end(); itMsg++)
			{
				if (!DumpEntry(false, *itMsg->Payload, pContext))
					return;
			}
		}
		// now do the entries currently in process
		for (typename CSharedQueue<CThreadWork<MsgT>>::const_iterator itPool = Pool.begin(); itPool != Pool.end(); itPool++)
		{
			if (itPool->IfActive() && itPool->bInUse)
			{
				if (!DumpEntry(true, *itPool->Msg, pContext))
					return;
			}
		}
	}
}

// dummy DumpEntry in case the user doesn't want to implement one
// return false to abort the dump
template <class MsgT>
bool CThreadPool<MsgT>::DumpEntry(bool bInProcess, const MsgT& Message, void *pContext) const
{
	(void)bInProcess;
	(void)Message;
	(void)pContext;
	return false;
}

// dummy CompareEntry in case the user doesn't want to implement one
// return false so it never matches anything
template <class MsgT>
bool CThreadPool<MsgT>::CompareEntry(const MsgT& Msg1, const MsgT& Msg2, UINT uSearchType) const
{
	(void)Msg1;
	(void)Msg2;
	(void)uSearchType;
	return false;
}

template <class MsgT>
void CThreadPool<MsgT>::DeleteOldThreadEntries(void)
{
	// check if any dead threads should be deleted
	// must not be called by any of the worker threads
	FILETIME ftNow;
	GetSystemTimeAsFileTime(&ftNow);
	CRWLockAcquire lockPool(&Pool.GetLock(), true);		// write lock
	for (typename CSharedQueue<CThreadWork<MsgT>>::iterator itPool = Pool.begin(); itPool != Pool.end(); )
	{
		if (!itPool->IfActive() && ((itPool->ftIdleTime + FT_SECONDS(30)) < ftNow))
			itPool = Pool.erase(itPool);
		else
			itPool++;
	}
}

template <class MsgT>
void CThreadPool<MsgT>::GarbageCollect(void)
{
	// check if there is anything that can be deleted
	DeleteOldThreadEntries();
}

template <class MsgT>
CString CThreadPool<MsgT>::FormatEntry(void)
{
	CString sLine;
	sLine.Format(_T("Thread Pool: %s, Queue:%u, Queue Max:%u, Threads:%u, WorkItems:%u, MinThreads:%u, MaxThreads:%u"),
		(LPCTSTR)FROM_ANSI(typeid(*this).name()), MsgQueue.GetCount(), *pdwPerfQueueMax, *pdwPerfNumThreads, *pdwPerfWorkItems, dwMinNumThreads, dwMaxNumThreads);
	return sLine;
}

template <class MsgT>
void CThreadPool<MsgT>::WaitForWorkFinished(THREADPOOL_ABORT_CB AbortProc, void *pContext, DWORD dwWaitMillisec)
{
	DWORD dwRet;
	// now wait for the threads to finish
	VERIFY(Events.evEmptyEvent.ResetEvent());
	for (;;)
	{
		if ((AbortProc != nullptr) && AbortProc(pContext))
			break;
		{
			CRWLockAcquire lock(&MsgQueue.GetLock(), false);		// read lock
			if ((GetMsgQueueCount() == 0) && FutureMsgQueue.empty())
				break;
		}
		dwRet = WaitForSingleObject(Events.evEmptyEvent, dwWaitMillisec);
		switch (dwRet)
		{
		case WAIT_FAILED:
			//dwRet = GetLastError();
			ASSERT(false);
			Sleep(dwWaitMillisec);
			break;
		case WAIT_OBJECT_0:
		case WAIT_TIMEOUT:
		default:
			break;
		}
	}
}

// GetMinNumThreadsInternal
// returns the configured min number of threads EXCEPT if there are any
// entries in the FutureMsgQueue. If there are, return at least 1 so a thread
// stays alive to service the Future entries
template <class MsgT>
DWORD CThreadPool<MsgT>::GetMinNumThreadsInternal(void)
{
	CRWLockAcquire lock(&MsgQueue.GetLock(), false);		// read lock
	if (dwMinNumThreads > 0)
		return dwMinNumThreads;
	if (FutureMsgQueue.empty() && MsgQueue.empty())			// if nothing to service, it's okay to return 0
		return dwMinNumThreads;
	return 1;
}

// TransferFutureQueue
// check if there are any entries in the Future Queue that have come due
// if so, transfer them to the main queue
template <class MsgT>
void CThreadPool<MsgT>::TransferFutureQueue(bool bFlush)		// if bFlush == true, then transfer all entries
{
	FILETIME ftNow;
	GetSystemTimeAsFileTime(&ftNow);
	typename CThreadPool<MsgT>::CMsgEntry DueMsg;
	bool bFoundDueMsg;
	for (;;)
	{
		{
			CRWLockAcquire lock(&MsgQueue.GetLock(), false);		// read lock
			bFoundDueMsg = false;
			for (typename CSharedQueue<typename CThreadPool<MsgT>::CMsgEntry>::iterator itFuture = FutureMsgQueue.begin(); itFuture != FutureMsgQueue.end(); ++itFuture)
			{
				if (bFlush || (ftNow >= itFuture->Control.ftDueTime))
				{
					DueMsg = *itFuture;
					bFoundDueMsg = true;
				}
			}
		}
		if (!bFoundDueMsg)
			break;
		SendMessageToPool(DueMsg.Control.uLineNo, DueMsg.Payload, 0, DueMsg.Control.uPriority, nullptr, DueMsg.Control.uGrouping);
		// now remove it from the FutureMsgQueue
		{
			CRWLockAcquire lock(&MsgQueue.GetLock(), true);		// write lock
			for (typename CSharedQueue<typename CThreadPool<MsgT>::CMsgEntry>::iterator itFuture = FutureMsgQueue.begin(); itFuture != FutureMsgQueue.end(); ++itFuture)
			{
				if (DueMsg.Control.ullUniqueID == itFuture->Control.ullUniqueID)
				{
					FutureMsgQueue.erase(itFuture);
					break;
				}
			}
		}
	}
}

template <class MsgT>
DWORD CThreadPool<MsgT>::GetMaxConcurrentThreads(bool bReset)
{
	DWORD dwMax = dwMaxConcurrentThreads;
	if (bReset)
		dwMaxConcurrentThreads = 0;
	return dwMax;
}

} // end namespace ecs_sdk
