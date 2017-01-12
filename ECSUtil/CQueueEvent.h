//
// Copyright (C) 1994 - 2011 EMC Corporation
// All rights reserved.
//
//
/****************************************************************************
*    CQueueEvent.h
*    PURPOSE: definitions for CQueueEvent template class
*
****************************************************************************/

#pragma once



#include <afxmt.h>
#include "exportdef.h"

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


class ECSUTIL_EXT_CLASS CQueueEvent;

class ECSUTIL_EXT_CLASS CQueueEventBase
{
	friend CQueueEvent;
protected:
	CCriticalSection csQueue;
	CObList		EventList;			// Used by a CMTQueue to list the events it triggers.

public:
	~CQueueEventBase();

	void Unlink(CQueueEvent *);
	void TriggerEvent(DWORD dwCurCount, INT TriggeringEvent = TRIGGEREVENTS_ALL);
};

class ECSUTIL_EXT_CLASS CQueueEvent : public CEvent
{
	friend CQueueEventBase;
private:
	CCriticalSection csQueueEvent;
	CQueueEventBase *pEventBase;
	DWORD dwCount;
	bool bAllEvents;
	bool bEnable;
	DWORD EnableTriggerEventFlags;	// enable/disable TriggerEvent for Push, Delete, etc.
	CPtrList	QueueList;			// list of queues to which this event is linked for triggering.


	// Disconnect
	// called by destructor of CQueueEventBase
	void Disconnect()
	{
		pEventBase = NULL;
		bEnable = false;
	}

public:
	CQueueEvent()
	{
		pEventBase = NULL;
		dwCount = 0;
		bEnable = bAllEvents = false;
		EnableTriggerEventFlags = TRIGGEREVENTS_ALL;
	}

	~CQueueEvent()
	{
		Unlink();
	}

	void SetCount(DWORD dwCountParam)
	{
		dwCount = dwCountParam;
	}

	void SetAllEvents(bool bAllEventsParam = true)
	{
		bAllEvents = bAllEventsParam;
	}

	void Enable(bool bEnableParam = true)
	{
		bEnable = bEnableParam;
	}

	void EnableAllTriggerEvents(void)
	{
		EnableTriggerEventFlags = TRIGGEREVENTS_ALL;
	}

	void DisableAllTriggerEvents(void)
	{
		EnableTriggerEventFlags = TRIGGEREVENTS_NONE;
	}

	void EnableTriggerEvents(INT flags)
	{
		EnableTriggerEventFlags |= (flags);
	}

	void DisableTriggerEvents(INT flags)
	{
		EnableTriggerEventFlags &= ~(flags);
	}

	bool IsTriggerEventEnabled(INT flag)
	{
		if (EnableTriggerEventFlags & flag)
			return true;
		else
			return false;
	}

	// Link
	// Add this event to the EventList of the supplied queue, and add the
	// queue to this event's QueueList.
	void Link(CQueueEventBase *pQueue)
	{
		ASSERT(pQueue != NULL);
		if (pQueue == NULL)
			return;
		{
			CSingleLock lockQueue(&pQueue->csQueue, true);
			// Add the current event to the queue's list of trigger events.
			pQueue->EventList.AddHead(this);
		}
		{
			CSingleLock lockQueueEvent(&csQueueEvent, true);
			// Add the queue to this event's list of queues to which it is linked.
			QueueList.AddHead(pQueue);
		}
	}

	// Unlink
	// If no queue is supplied, unlink from all queues: for each queue to which
	// this event is linked, remove it from the queue's EventList, then remove 
	// the queues from this event's QueueList.
	// If a queue is supplied, unlink only from that queue.
	void Unlink(CQueueEventBase *pQueue = NULL)
	{
		POSITION		QueuePos, EventPos;

		if (pQueue == NULL)
		{
			CQueueEventBase		*pQ;

			{
				CSingleLock lockQueueEvent(&csQueueEvent, true);
				for( QueuePos = QueueList.GetHeadPosition(); QueuePos != NULL; )
				{
					pQ = (CQueueEventBase *)QueueList.GetNext(QueuePos);
					{
						CSingleLock lockQueue(&pQ->csQueue, true);
						EventPos = pQ->EventList.Find(this);
						if (EventPos != NULL)
							pQ->EventList.RemoveAt(EventPos);
					}
				}
				QueueList.RemoveAll();
			}
		}
		else
		{
			{
				CSingleLock lockQueue(&pQueue->csQueue, true);
				EventPos = pQueue->EventList.Find(this);
				if (EventPos != NULL)
					pQueue->EventList.RemoveAt(EventPos);
			}
			{
				CSingleLock lockQueueEvent(&csQueueEvent, true);
				QueuePos = QueueList.Find(pQueue);
				if (QueuePos != NULL)
					QueueList.RemoveAt(QueuePos);
			}
		}
	}
};

