/*
 * Copyright (c) 1994 - 2017, EMC Corporation.
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

#include "stdafx.h"

#include "CSharedQueue.h"

// Remove each event in the EventList from each queue to which it has been linked.
CSharedQueueEventBase::~CSharedQueueEventBase()
{
	CRWLockAcquire lockQueue(&rwlQueue, true);
	for (std::set<CSharedQueueEvent *>::iterator itEvent = EventList.begin(); itEvent != EventList.end(); ++itEvent)
	{
		if (*itEvent != NULL)
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
