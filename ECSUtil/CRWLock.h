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
