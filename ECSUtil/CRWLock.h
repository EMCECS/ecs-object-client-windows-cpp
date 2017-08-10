//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//
//

#pragma once

#include "exportdef.h"

using namespace std;

class ECSUTIL_EXT_CLASS CSimpleRWLockAcquire;

class ECSUTIL_EXT_CLASS CSimpleRWLock
{
	friend class CSimpleRWLockAcquire;
private:
	SRWLOCK RWLock;								// lock used by the caller

public:
	CSimpleRWLock& operator = (const CSimpleRWLock& Src);	// no implementation
	CSimpleRWLock(const CSimpleRWLock& Src);				// no implementation

public:
	CSimpleRWLock();
	~CSimpleRWLock();
};

class ECSUTIL_EXT_CLASS CSimpleRWLockAcquire
{
	friend class CSimpleRWLock;
private:
	CSimpleRWLock *pLock;
	DWORD dwThreadId;							// current thread ID
	bool bLocked;								// set if lock has been acquired
	bool bWrite;								// set if write lock

private:
	CSimpleRWLockAcquire& operator = (const CSimpleRWLockAcquire& ptr);		// no implementation
	CSimpleRWLockAcquire(const CSimpleRWLockAcquire& Src);					// no implementation

public:
	CSimpleRWLockAcquire(CSimpleRWLock *pLockParam, bool bWriteParam = false, bool bGetLock = true);
	~CSimpleRWLockAcquire();
	void Unlock(void) throw();
	void Lock(bool bWriteParam = false) throw();
	bool IsLocked(void) const;
	bool IsWriteLocked(void) const;
};

class CRWLockAcquire;

//lint -save -esym(1539, CRWLock::rwlListLock)
class ECSUTIL_EXT_CLASS CRWLock
{
	friend class CRWLockAcquire;
private:
	vector<CRWLockAcquire *> Instances;		// list of CRWLockAcquire instances
	mutable CSimpleRWLock rwlListLock;			// lock used for accessing the map
	SRWLOCK RWLock;								// lock used by the caller

public:
	CRWLock& operator = (const CRWLock& Src);	// no implementation
	CRWLock(const CRWLock& Src);				// no implementation
	bool IsLocked(void) const;
	bool IsWriteLocked(void) const;

public:
	CRWLock();
	~CRWLock() throw();
};
//lint -restore

class ECSUTIL_EXT_CLASS CRWLockAcquire
{
	friend class CRWLock;
private:
	CRWLock *pLock;
	DWORD dwThreadId;							// current thread ID
	bool bLocked;								// set if lock has been acquired
	bool bWrite;								// set if write lock
	bool bWaiting;								// if set, waiting for a lock (used for debugging)

private:
	CRWLockAcquire& operator = (const CRWLockAcquire& ptr);		// no implementation
	CRWLockAcquire(const CRWLockAcquire& Src);					// no implementation

public:
	CRWLockAcquire(CRWLock *pLockParam, bool bWriteParam = false, bool bGetLock = true);
	~CRWLockAcquire() throw();
	void Unlock(void) throw();
	void Lock(bool bWriteParam = false) throw();
	bool IsLocked(void) const;
	bool IsWriteLocked(void) const;
};
