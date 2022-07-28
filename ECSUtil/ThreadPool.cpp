/*
 * Copyright (c) 2017 - 2022, Dell Technologies, Inc. All Rights Reserved.
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


#include "ThreadPool.h"

namespace ecs_sdk
{

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool CThreadPoolBase::bPoolInitialized = false;
CCriticalSection *CThreadPoolBase::pcsGlobalCThreadPool;
std::set<CThreadPoolBase *> *CThreadPoolBase::pGlobalCThreadPool;
CSimpleRWLock CThreadPoolBase::rwlPerfDummy;	// dummy critical section if no CS is specified

void CThreadPoolBase::RegisterCThreadPool()
{
	if (pcsGlobalCThreadPool == nullptr)
	{
		CCriticalSection *pcsGlobalCThreadPoolTemp = new CCriticalSection;
		if (InterlockedCompareExchangePointer((void **)&pcsGlobalCThreadPool, pcsGlobalCThreadPoolTemp, nullptr) != nullptr)
			delete pcsGlobalCThreadPoolTemp;
	}
	ASSERT(pcsGlobalCThreadPool != nullptr);
	CSingleLock lockGlobalList(pcsGlobalCThreadPool, true);
	if (pGlobalCThreadPool == nullptr)
		pGlobalCThreadPool = new std::set<CThreadPoolBase *>;
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
	if ((pcsGlobalCThreadPool != nullptr) && (pGlobalCThreadPool != nullptr))
	{
		CSingleLock lockGlobalList(pcsGlobalCThreadPool, true);
		for (std::set<CThreadPoolBase *>::iterator itSet = pGlobalCThreadPool->begin(); itSet != pGlobalCThreadPool->end(); ++itSet)
			(*itSet)->GarbageCollect();
	}
}

void CThreadPoolBase::DumpPools(CString *pDumpMsg)
{
	if ((pcsGlobalCThreadPool == nullptr) || (pGlobalCThreadPool == nullptr) || !bPoolInitialized)
		return;
	CSingleLock lockGlobalQueueList(pcsGlobalCThreadPool, true);

	std::set<CThreadPoolBase *>::iterator itSet;
	for (itSet = pGlobalCThreadPool->begin(); itSet != pGlobalCThreadPool->end(); ++itSet)
		*pDumpMsg += (*itSet)->FormatEntry() + _T("\r\n");
}

void CThreadPoolBase::AllTerminate()
{
	bPoolInitialized = false;
	// clean up all thread pools
	if (CThreadPoolBase::pcsGlobalCThreadPool != nullptr)
	{
		{
			CSingleLock lock(CThreadPoolBase::pcsGlobalCThreadPool, true);
			for (std::set<CThreadPoolBase *>::const_iterator it = CThreadPoolBase::pGlobalCThreadPool->begin();
				it != CThreadPoolBase::pGlobalCThreadPool->end(); ++it)
			{
				(*it)->Terminate();
			}
		}
		delete pcsGlobalCThreadPool;
		delete pGlobalCThreadPool;
		pcsGlobalCThreadPool = nullptr;
		pGlobalCThreadPool = nullptr;
	}
}

} // end namespace ecs_sdk
