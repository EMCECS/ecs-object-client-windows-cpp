//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//



#include "stdafx.h"

//lint -esym(1539,CRWLock::Instances,CRWLock::RWLock,,CSimpleRWLock::RWLock)
#include "CRWLock.h"
											// otherwise it will get created during global constructor time and it may run
											// before the critical section (in CDllFunc) is initialized
CRWLock::CRWLock()
{
	InitializeSRWLock(&RWLock);
	Instances.reserve(20);					// allocate the first batch in one shot
}

CRWLock::~CRWLock() throw()
{
	// make sure there are no outstanding locks
	// if so, assert, but then unlock the locks
	CSimpleRWLockAcquire lockList(&rwlListLock, true);
	if (!Instances.empty())
	{
		// there are outstanding locks!
		ASSERT(false);
		for (vector<CRWLockAcquire *>::const_iterator it=Instances.begin() ; it!=Instances.end() ; ++it)
		{
			(*it)->bLocked = false;
			(*it)->pLock = NULL;
		}
	}
}

CRWLock& CRWLock::operator = (const CRWLock& Src)
{
	if (&Src == this)
		return *this;
	return *this;
}

CRWLock::CRWLock(const CRWLock& Src)
{
	(void)Src;
	InitializeSRWLock(&RWLock);
}

bool CRWLock::IsLocked(void) const
{
	CSimpleRWLockAcquire lockList(&rwlListLock, false);
	return !Instances.empty();
}

bool CRWLock::IsWriteLocked(void) const
{
	bool bWriteLocked = false;
	CSimpleRWLockAcquire lockList(&rwlListLock, false);
	if (!Instances.empty())
	{
		for (vector<CRWLockAcquire *>::const_iterator it = Instances.begin(); it != Instances.end(); ++it)
		{
			if ((*it)->bWrite)
				bWriteLocked = true;
		}
	}
	return bWriteLocked;
}

CRWLockAcquire::CRWLockAcquire(CRWLock *pLockParam, bool bWriteParam, bool bGetLock)
	: pLock(pLockParam)
	, dwThreadId(GetCurrentThreadId())
	, bLocked(false)
	, bWrite(bWriteParam)
	, bWaiting(false)
{
	ASSERT(pLockParam != NULL);
	if (pLock != NULL)						//lint !e774
	{
		// now put this entry on the vector with the lock
		// hook this instance into the vector for this lock
		{
			CSimpleRWLockAcquire lockList(&pLock->rwlListLock, true);
			pLock->Instances.push_back(this);
		}
		if (bGetLock)
			Lock(bWrite);
	}
}

CRWLockAcquire::~CRWLockAcquire() throw()
{
	Unlock();
	if (pLock != NULL)
	{
		// hook this instance into the vector for this lock
		CSimpleRWLockAcquire lockList(&pLock->rwlListLock, true);
		vector<CRWLockAcquire *>::const_iterator it;
#ifdef DEBUG
		bool bErased = false;
#endif
		for (it=pLock->Instances.begin() ; it!=pLock->Instances.end() ; ++it)
			if (*it == this)
			{
				(void)pLock->Instances.erase(it);
#ifdef DEBUG
				bErased = true;
#endif
				break;
			}
#ifdef DEBUG
		ASSERT(bErased);
#endif
		pLock = NULL;
	}
}

void CRWLockAcquire::Unlock(void) throw()
{
	ASSERT(pLock != NULL);
	if (pLock == NULL)						//lint !e774
		return;
	if (bLocked)
	{
		long iNumLocks = 0;
		{
			CSimpleRWLockAcquire lockList(&pLock->rwlListLock);			// only need read lock
			for (vector<CRWLockAcquire *>::const_iterator it = pLock->Instances.begin(); it != pLock->Instances.end(); ++it)
				if ((*it != this) && ((*it)->dwThreadId == dwThreadId) && (*it)->bLocked)
					iNumLocks++;
		}
		// are there other locks on this same thread?
		if (iNumLocks > 0)
		{
			bLocked = false;
			return;
		}
		if (bWrite)
			ReleaseSRWLockExclusive(&pLock->RWLock);
		else
			ReleaseSRWLockShared(&pLock->RWLock);
		bLocked = false;
	}
}

void CRWLockAcquire::Lock(bool bWriteParam) throw()
{
	ASSERT(pLock != NULL);
	if (pLock == NULL)						//lint !e774
		return;
	Unlock();
	bWrite = bWriteParam;
	bWaiting = true;
	// check if the current thread also has the lock on some other stack frame
	bool bWriteLocked = false;
	bool bThreadLocked = false;
	{
		CSimpleRWLockAcquire lockList(&pLock->rwlListLock);			// read lock
		for (vector<CRWLockAcquire *>::const_iterator it = pLock->Instances.begin(); it != pLock->Instances.end(); ++it)
		{
			if ((*it != this) && ((*it)->dwThreadId == dwThreadId) && (*it)->bLocked)
			{
				bThreadLocked = true;			// i can change this even if I only have a read lock here
				if ((*it)->bWrite)				// because i'm only changing the entry for my thread id
					bWriteLocked = true;
			}
		}
	}
	if (bThreadLocked)
	{
		// already locked by this thread
		if (!bWriteLocked && bWrite)
		{
			ASSERT(false);					// incompatible lock - it is already read locked and we are asking for a write lock
			// let it fall through. it will deadlock
		}
		else
		{
			bLocked = true;
			bWaiting = false;
			return;
		}
	}
	if (bWrite)
		AcquireSRWLockExclusive(&pLock->RWLock);
	else
		AcquireSRWLockShared(&pLock->RWLock);
	bLocked = true;
	bWaiting = false;
}

bool CRWLockAcquire::IsLocked(void) const
{
	return bLocked;
}

bool CRWLockAcquire::IsWriteLocked(void) const
{
	return bWrite;
}

// otherwise it will get created during global constructor time and it may run
// before the critical section (in CDllFunc) is initialized
CSimpleRWLock::CSimpleRWLock()
{
	InitializeSRWLock(&RWLock);
}

CSimpleRWLock::~CSimpleRWLock()
{
}

CSimpleRWLock& CSimpleRWLock::operator = (const CSimpleRWLock& Src)
{
	if (&Src == this)
		return *this;
	return *this;
}

CSimpleRWLock::CSimpleRWLock(const CSimpleRWLock& Src)
{
	(void)Src;
	InitializeSRWLock(&RWLock);
}

CSimpleRWLockAcquire::CSimpleRWLockAcquire(CSimpleRWLock *pLockParam, bool bWriteParam, bool bGetLock)
{
	ASSERT(pLockParam != NULL);
	pLock = pLockParam;
	bLocked = false;
	bWrite = bWriteParam;
	dwThreadId = GetCurrentThreadId();
	if (bGetLock)
		Lock(bWrite);
}

CSimpleRWLockAcquire::~CSimpleRWLockAcquire()
{
	Unlock();
	if (pLock != NULL)
		pLock = NULL;
}

void CSimpleRWLockAcquire::Unlock(void) throw()
{
	ASSERT(pLock != NULL);
	if (pLock == NULL)						//lint !e774
		return;
	if (bLocked)
	{
		if (bWrite)
			ReleaseSRWLockExclusive(&pLock->RWLock);
		else
			ReleaseSRWLockShared(&pLock->RWLock);
		bLocked = false;
	}
}

void CSimpleRWLockAcquire::Lock(bool bWriteParam) throw()
{
	ASSERT(pLock != NULL);
	if (pLock == NULL)						//lint !e774
		return;
	Unlock();
	bWrite = bWriteParam;
	// check if the current thread also has the lock on some other stack frame
	if (bWrite)
		AcquireSRWLockExclusive(&pLock->RWLock);
	else
		AcquireSRWLockShared(&pLock->RWLock);
	bLocked = true;
}

bool CSimpleRWLockAcquire::IsLocked(void) const
{
	return bLocked;
}

bool CSimpleRWLockAcquire::IsWriteLocked(void) const
{
	return bWrite;
}
