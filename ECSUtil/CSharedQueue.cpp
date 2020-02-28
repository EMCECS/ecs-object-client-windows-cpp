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

#include "stdafx.h"

#include "CSharedQueue.h"

// Remove each event in the EventList from each queue to which it has been linked.
CSharedQueueEventBase::~CSharedQueueEventBase()
{
	CRWLockAcquire lockQueue(&rwlQueue, true);
	for (std::set<CSharedQueueEvent *>::iterator itEvent = EventList.begin(); itEvent != EventList.end(); ++itEvent)
	{
		if (*itEvent != nullptr)
			(*itEvent)->Unlink();
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
	ASSERT(pQueue != nullptr);
	if (pQueue == nullptr)				//lint !e774	// (Info -- Boolean within 'if' always evaluates to False
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
	if (pQueue == nullptr)
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
