//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//



#include "stdafx.h"


#include "ThreadPool.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool CThreadPoolBase::bPoolInitialized = false;
CCriticalSection *CThreadPoolBase::pcsGlobalCThreadPool;
set<CThreadPoolBase *> *CThreadPoolBase::pGlobalCThreadPool;
CSimpleRWLock CThreadPoolBase::rwlPerfDummy;	// dummy critical section if no CS is specified

void CThreadPoolBase::RegisterCThreadPool()
{
	if (pcsGlobalCThreadPool == NULL)
	{
		CCriticalSection *pcsGlobalCThreadPoolTemp = new CCriticalSection;			//lint !e1732 !e1733: (Info -- new in constructor for class 'CSimpleWorkerThread' which has no assignment operator)
		if (InterlockedCompareExchangePointer((void **)&pcsGlobalCThreadPool, pcsGlobalCThreadPoolTemp, NULL) != NULL)
			delete pcsGlobalCThreadPoolTemp;
	}
	ASSERT(pcsGlobalCThreadPool != NULL);
	CSingleLock lockGlobalList(pcsGlobalCThreadPool, true);
	if (pGlobalCThreadPool == NULL)
		pGlobalCThreadPool = new set<CThreadPoolBase *>;
	(void)pGlobalCThreadPool->insert(this);
}

void CThreadPoolBase::UnregisterCThreadPool()
{
	if (pcsGlobalCThreadPool && pGlobalCThreadPool)
	{
		CSingleLock lockGlobalList(pcsGlobalCThreadPool, true);
		(void)pGlobalCThreadPool->erase(this);
	}
}

void CThreadPoolBase::GlobalGarbageCollect()
{
	if ((pcsGlobalCThreadPool != NULL) && (pGlobalCThreadPool != NULL))
	{
		CSingleLock lockGlobalList(pcsGlobalCThreadPool, true);
		for (set<CThreadPoolBase *>::iterator itSet = pGlobalCThreadPool->begin(); itSet != pGlobalCThreadPool->end(); ++itSet)
			(*itSet)->GarbageCollect();
	}
}

void CThreadPoolBase::DumpPools(CString *pDumpMsg)
{
	if ((pcsGlobalCThreadPool == NULL) || (pGlobalCThreadPool == NULL) || !bPoolInitialized)
		return;
	CSingleLock lockGlobalQueueList(pcsGlobalCThreadPool, true);

	set<CThreadPoolBase *>::iterator itSet;
	for (itSet = pGlobalCThreadPool->begin(); itSet != pGlobalCThreadPool->end(); ++itSet)
		*pDumpMsg += (*itSet)->FormatEntry() + _T("\r\n");
}
