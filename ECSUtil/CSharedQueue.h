/*
 * Copyright (c) 1994-2017, EMC Corporation.
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

/****************************************************************************
*    CSharedQueue.h
*    PURPOSE: definitions for CSharedQueue template class
*
****************************************************************************/


#pragma once



#include <afxmt.h>
#include <typeinfo.h>
#include <set>
#include <memory>
#include <deque>
#include "CRWLock.h"

#define TRIGGEREVENTS_PUSH			 0x01
#define TRIGGEREVENTS_INSERTAT		 0x02
#define TRIGGEREVENTS_REPLACEAT		 0x04
#define TRIGGEREVENTS_DELETE		 0x08
#define TRIGGEREVENTS_EMPTY			 0x10
#define TRIGGEREVENTS_CHANGETYPE	 0x20
#define TRIGGEREVENTS_SERIALIZE		 0x40
#define TRIGGEREVENTS_ADD			 0x80
#define TRIGGEREVENTS_CHANGEKEY		0x100
#define TRIGGEREVENTS_INSERT		0x200
#define TRIGGEREVENTS_POP			0x400
#define TRIGGEREVENTS_NONE			0
#define TRIGGEREVENTS_ALL			~(TRIGGEREVENTS_NONE)

class ECSUTIL_EXT_CLASS CSharedQueueEvent;

class ECSUTIL_EXT_CLASS CSharedQueueEventBase
{
	friend CSharedQueueEvent;
protected:
	CRWLock rwlQueue;
	set<CSharedQueueEvent *> EventList;	// Used by a CMTQueue to list the events it triggers.

public:
	~CSharedQueueEventBase();
	void TriggerEvent(DWORD dwCurCount, INT TriggeringEvent = TRIGGEREVENTS_ALL);
};

struct CSharedQueueEventEvent
{
	CEvent evQueue;

	CSharedQueueEventEvent()
	{
	}

	// copy constructor
	CSharedQueueEventEvent(const CSharedQueueEventEvent& src)
	{
		(void)src;
	};

	const CSharedQueueEventEvent& operator =(const CSharedQueueEventEvent& src)		//lint !e1539	// members not assigned by assignment operator
	{
		if (&src == this)
			return *this;
		return *this;
	};
};

class ECSUTIL_EXT_CLASS CSharedQueueEvent
{
	friend CSharedQueueEventBase;
public:
	CSharedQueueEventEvent Event;
private:
	CSimpleRWLock rwlQueueEvent;
	DWORD dwCount;
	bool bAllEvents;
	bool bEnable;
	DWORD EnableTriggerEventFlags;	// enable/disable TriggerEvent for Push, Delete, etc.
	set<CSharedQueueEventBase *> QueueList;			// list of queues to which this event is linked for triggering.

	// Disconnect
	// called by destructor of CSharedQueueEventBase
	void Disconnect(void);

public:
	CSharedQueueEvent();
	~CSharedQueueEvent();
	void SetCount(DWORD dwCountParam);
	void SetAllEvents(bool bAllEventsParam = true);
	void Enable(bool bEnableParam = true);
	void EnableAllTriggerEvents(void);
	void DisableAllTriggerEvents(void);
	void EnableTriggerEvents(INT flags);
	void DisableTriggerEvents(INT flags);
	bool IsTriggerEventEnabled(INT flag);
	void Link(CSharedQueueEventBase *pQueue) throw();
	void Unlink(CSharedQueueEventBase *pQueue = NULL) throw();
};


/////////////////////////////////////////////////////////
// Multithread Queue

// Thread-safe queue class for type T
template<class T> class CSharedQueue : public list<T>, public CSharedQueueEventBase
{
public:
	typedef bool(*TEST_ABORT_CB)(void *pContext);
private:
	// check if queue is greater than uMaxSize
	// if so, wait for it to go below it
	void WaitForQueue(unsigned int dwMaxQueueSize, TEST_ABORT_CB pTestAbort, void *pContext)
	{
		if ((dwMaxQueueSize != 0) && (size() > dwMaxQueueSize))
		{
			CSharedQueueEvent QueueEvent;
			QueueEvent.Link(this);
			QueueEvent.EnableTriggerEvents(TRIGGEREVENTS_POP);
			QueueEvent.EnableTriggerEvents(TRIGGEREVENTS_DELETE);
			QueueEvent.EnableTriggerEvents(TRIGGEREVENTS_EMPTY);
			QueueEvent.Enable();
			QueueEvent.SetCount(dwMaxQueueSize);
			while (size() > dwMaxQueueSize)
			{
				if (pTestAbort != NULL)					// check if we are supposed to shut down
					if (pTestAbort(pContext))
						break;
				CSingleLock lockEmpty(&QueueEvent.Event.evQueue);
				(void)lockEmpty.Lock(SECONDS(5));
				if (lockEmpty.IsLocked())
					VERIFY(lockEmpty.Unlock());
			}
		}
	}

public:
	CSharedQueue()
	{
	};

	virtual ~CSharedQueue()
	{
	};

	// copy constructor
	CSharedQueue(const CSharedQueue &q)
	{
	}

	// GetLock
	// return a reference to the CRWLock object in case
	// the caller wants to enclose several operations within the same lock
	CRWLock& GetLock(void) const
	{
		return *(const_cast<CRWLock *> (&rwlQueue));
	}

	bool empty() const
	{
		CRWLockAcquire lockQueue(const_cast<CRWLock *>(&rwlQueue), false);			// read lock
		return list<T>::empty();
	};

	reference front()
	{	// return first element of mutable sequence
		CRWLockAcquire lockQueue(const_cast<CRWLock *>(&rwlQueue), true);			// write lock
		return list<T>::front();
	}

	const_reference front() const
	{	// return first element of nonmutable sequence
		CRWLockAcquire lockQueue(const_cast<CRWLock *>(&rwlQueue), false);			// read lock
		return list<T>::front();
	}

	reference back()
	{	// return last element of mutable sequence
		CRWLockAcquire lockQueue(const_cast<CRWLock *>(&rwlQueue), true);			// write lock
		return list<T>::back();
	}

	const_reference back() const
	{	// return last element of nonmutable sequence
		CRWLockAcquire lockQueue(const_cast<CRWLock *>(&rwlQueue), false);			// read lock
		return list<T>::back();
	}

	void clear()
	{
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		list<T>::clear();
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_EMPTY);
	}

	size_t size() const
	{
		CRWLockAcquire lockQueue(const_cast<CRWLock *>(&rwlQueue), false);			// read lock
		return list<T>::size();
	};

	DWORD GetCount() const
	{
		CRWLockAcquire lockQueue(const_cast<CRWLock *>(&rwlQueue), false);			// read lock
		return (DWORD)list<T>::size();
	};

	void push_back(const T& rec, unsigned int dwMaxQueueSize = 0, TEST_ABORT_CB pTestAbort = NULL, void *pContext = NULL)
	{
		if ((dwMaxQueueSize > 0) && (GetCount() > dwMaxQueueSize))
			WaitForQueue(dwMaxQueueSize, pTestAbort, pContext);
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_PUSH);
		list<T>::push_back(rec);
	}

	void push_back(T&& rec, unsigned int dwMaxQueueSize = 0, TEST_ABORT_CB pTestAbort = NULL, void *pContext = NULL)
	{
		if ((dwMaxQueueSize > 0) && (GetCount() > dwMaxQueueSize))
			WaitForQueue(dwMaxQueueSize, pTestAbort, pContext);
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_PUSH);
		list<T>::push_back(rec);
	}

	void emplace_back(T&& _Val, unsigned int dwMaxQueueSize = 0, TEST_ABORT_CB pTestAbort = NULL, void *pContext = NULL)
	{
		if ((dwMaxQueueSize > 0) && (GetCount() > dwMaxQueueSize))
			WaitForQueue(dwMaxQueueSize, pTestAbort, pContext);
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_PUSH);
		list<T>::emplace_back(_Val);
	}

	void push_front(const T& rec, unsigned int dwMaxQueueSize = 0, TEST_ABORT_CB pTestAbort = NULL, void *pContext = NULL)
	{
		if ((dwMaxQueueSize > 0) && (GetCount() > dwMaxQueueSize))
			WaitForQueue(dwMaxQueueSize, pTestAbort, pContext);
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_PUSH);
		list<T>::push_front(rec);
	}

	void push_front(T&& rec, unsigned int dwMaxQueueSize = 0, TEST_ABORT_CB pTestAbort = NULL, void *pContext = NULL)
	{
		if ((dwMaxQueueSize > 0) && (GetCount() > dwMaxQueueSize))
			WaitForQueue(dwMaxQueueSize, pTestAbort, pContext);
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_PUSH);
		list<T>::push_front(rec);
	}

	void emplace_front(T&& _Val, unsigned int dwMaxQueueSize = 0, TEST_ABORT_CB pTestAbort = NULL, void *pContext = NULL)
	{
		if ((dwMaxQueueSize > 0) && (GetCount() > dwMaxQueueSize))
			WaitForQueue(dwMaxQueueSize, pTestAbort, pContext);
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_PUSH);
		list<T>::emplace_front(_Val);
	}

	void pop_front(void)
	{
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_POP);
		list<T>::pop_front();
	}

	void pop_back(void)
	{
		CRWLockAcquire lockQueue(&rwlQueue, true);			// write lock
		TriggerEvent((DWORD)list<T>::size(), TRIGGEREVENTS_POP);
		list<T>::pop_back();
	}
};

