//
// Copyright (C) 1994 - 2015 EMC Corporation
// All rights reserved.
//



#include "stdafx.h"

#include "CSharedQueue.h"

// Remove each event in the EventList from each queue to which it has been linked.
CSharedQueueEventBase::~CSharedQueueEventBase()
{
	try
	{
		CRWLockAcquire lockQueue(&rwlQueue, true);
		for (std::set<CSharedQueueEvent *>::iterator itEvent = EventList.begin(); itEvent != EventList.end(); ++itEvent)
		{
			if (*itEvent != NULL)
				(*itEvent)->Unlink();
		}
	}
	catch (...)
	{
	}
}

void CSharedQueueEventBase::TriggerEvent(DWORD dwCurCount, INT TriggeringEvent)
{
	CRWLockAcquire lockQueue(&rwlQueue, false);			// read lock
	for (std::set<CSharedQueueEvent *>::iterator itEvent = EventList.begin(); itEvent != EventList.end(); ++itEvent)
	{
		if (!((*itEvent)->EnableTriggerEventFlags & TriggeringEvent))
			continue;
		if ((*itEvent)->bEnable && ((*itEvent)->bAllEvents || (dwCurCount <= (*itEvent)->dwCount)))
			if (!(*itEvent)->Event.evQueue.SetEvent())
				AfxThrowUserException();
	}
}

void CSharedQueueEvent::Disconnect()
{
	bEnable = false;
}

CSharedQueueEvent::CSharedQueueEvent()
{
	dwCount = 0;
	bEnable = bAllEvents = false;
	EnableTriggerEventFlags = TRIGGEREVENTS_ALL;
}

CSharedQueueEvent::~CSharedQueueEvent()
{
	Unlink();
}

void CSharedQueueEvent::SetCount(DWORD dwCountParam)
{
	dwCount = dwCountParam;
}

void CSharedQueueEvent::SetAllEvents(bool bAllEventsParam)
{
	bAllEvents = bAllEventsParam;
}

void CSharedQueueEvent::Enable(bool bEnableParam)
{
	bEnable = bEnableParam;
}

void CSharedQueueEvent::EnableAllTriggerEvents(void)
{
	EnableTriggerEventFlags = TRIGGEREVENTS_ALL;
}

void CSharedQueueEvent::DisableAllTriggerEvents(void)
{
	EnableTriggerEventFlags = TRIGGEREVENTS_NONE;
}

void CSharedQueueEvent::EnableTriggerEvents(INT flags)
{
	EnableTriggerEventFlags |= ((DWORD)flags);
}

void CSharedQueueEvent::DisableTriggerEvents(INT flags)
{
	EnableTriggerEventFlags &= ~((DWORD)flags);
}

bool CSharedQueueEvent::IsTriggerEventEnabled(INT flag)
{
	return TST_BIT(EnableTriggerEventFlags, flag);
}

// Link
// Add this event to the EventList of the supplied queue, and add the
// queue to this event's QueueList.
void CSharedQueueEvent::Link(CSharedQueueEventBase *pQueue) throw()
{
	ASSERT(pQueue != NULL);
	if (pQueue == NULL)				//lint !e774	// (Info -- Boolean within 'if' always evaluates to False
		return;
	{
		CRWLockAcquire lockQueue(&pQueue->rwlQueue, true);
		// Add the current event to the queue's list of trigger events.
		(void)pQueue->EventList.insert(this);
	}
	{
		CSimpleRWLockAcquire lockQueueEvent(&rwlQueueEvent, true);
		// Add the queue to this event's list of queues to which it is linked.
		(void)QueueList.insert(pQueue);
	}
}

// Unlink
// If no queue is supplied, unlink from all queues: for each queue to which
// this event is linked, remove it from the queue's EventList, then remove 
// the queues from this event's QueueList.
// If a queue is supplied, unlink only from that queue.
void CSharedQueueEvent::Unlink(CSharedQueueEventBase *pQueue) throw()
{
	if (pQueue == NULL)
	{
		CSimpleRWLockAcquire lockQueueEvent(&rwlQueueEvent, true);
		for (std::set<CSharedQueueEventBase *>::iterator itQueue = QueueList.begin(); itQueue != QueueList.end(); ++itQueue)
		{
			CRWLockAcquire lockQueue(&(*itQueue)->rwlQueue, true);
			(void)(*itQueue)->EventList.erase(this);
		}
		QueueList.clear();
	}
	else
	{
		{
			CRWLockAcquire lockQueue(&pQueue->rwlQueue, true);
			(void)pQueue->EventList.erase(this);
		}
		{
			CSimpleRWLockAcquire lockQueueEvent(&rwlQueueEvent, true);
			(void)QueueList.erase(pQueue);
		}
	}
}
