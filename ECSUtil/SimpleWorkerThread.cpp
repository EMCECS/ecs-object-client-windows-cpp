/*
 * Copyright (c) 1994 - 2017, EMC Corporation. All Rights Reserved.
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

using namespace std;

#include "generic_defs.h"
#include "ProcessEvent.h"
#include "SimpleWorkerThread.h"
#include "Logging.h"
#include "fmtnum.h"
#include "widestring.h"
#include "NTERRTXT.H"
#include "dyndll.h"

CCriticalSection *CSimpleWorkerThread::pcsGlobalThreadSet;
set<CSimpleWorkerThread *> *CSimpleWorkerThread::pGlobalThreadSetActive;
set<CSimpleWorkerThread *> *CSimpleWorkerThread::pGlobalThreadSet;
static CThreadDescription ThreadDescription;

void CSimpleWorkerThread::SignalTermination(void)
{
	CSingleLock lock(const_cast<CCriticalSection *> (&Events.csWorkEvent), true);
	for (list<CEvent *>::const_iterator itTerm = TermEventList.begin(); itTerm != TermEventList.end(); ++itTerm)
	{
		(void)(*itTerm)->SetEvent();
	}
	// now remove the events
	TermEventList.clear();
}

CSimpleWorkerThread::CSimpleWorkerThread()
	: pThread(nullptr)
	, dwThreadID(0)
	, hThread(nullptr)
	, dwWaitTime(INFINITE)
	, bRunning(false)
	, bTerminate(false)
	, bAlertable(false)
	, dwEventRet(0)
{
	ZeroFT(ftEndThreadTime);
	// keep a list of all current threads
	{
		if (pcsGlobalThreadSet == nullptr)
		{
			CCriticalSection *pcsGlobalThreadTemp = new CCriticalSection;			//lint !e1732 !e1733 (Info -- new in constructor for class 'CSimpleWorkerThread' which has no assignment operator)
			if (InterlockedCompareExchangePointer((void **)&pcsGlobalThreadSet, pcsGlobalThreadTemp, nullptr) != nullptr)
				delete pcsGlobalThreadTemp;
		}
		ASSERT(pcsGlobalThreadSet != nullptr);
		CSingleLock lockGlobalList(pcsGlobalThreadSet, true);
		if (pGlobalThreadSet == nullptr)
			pGlobalThreadSet = new set<CSimpleWorkerThread *>;			//lint !e1732 !e1733	// (Info -- new in constructor for class 'CAtmosFSApi' which has no assignment operator) not needed since this is assigning to a static
		if (pGlobalThreadSetActive == nullptr)
			pGlobalThreadSetActive = new set<CSimpleWorkerThread *>;			//lint !e1732 !e1733	// (Info -- new in constructor for class 'CAtmosFSApi' which has no assignment operator) not needed since this is assigning to a static
		(void)pGlobalThreadSet->insert(this);
	}
}

CSimpleWorkerThread::~CSimpleWorkerThread()
{
	KillThreadWait();
	if (hThread != nullptr)
	{
		if (!CloseHandle(hThread))				// allow the thread to terminate
			LogMessage(_T(__FILE__), __LINE__, _T("CloseHandle error"), GetLastError());
	}
	if (pcsGlobalThreadSet && pGlobalThreadSet)
	{
		CSingleLock csGlobalList(pcsGlobalThreadSet, true);
		(void)pGlobalThreadSet->erase(this);
	}
	hThread = nullptr;
	pThread = nullptr;
}

bool CSimpleWorkerThread::CreateThread(
	UINT nStackSize,
	LPSECURITY_ATTRIBUTES lpSecurityAttrs,
	int nPriority)
{
	{
		CSingleLock lockWorkEvent(&Events.csWorkEvent, true);
		if (Events.m_pWorkEvent != nullptr)
			delete Events.m_pWorkEvent;
		Events.m_pWorkEvent = new CEvent();
		ASSERT(Events.m_pWorkEvent != nullptr);
		if (hThread != nullptr)
		{
			if (!CloseHandle(hThread))				// allow the thread to terminate
				LogMessage(_T(__FILE__), __LINE__, _T("CloseHandle error"), GetLastError());
			hThread = nullptr;
			pThread = nullptr;
		}
	}
	bTerminate = false;
	// create the thread suspended because there is a race condition:
	// the thread may get going before the pThread field gets initialized
	pThread = AfxBeginThread(ThreadProc, this, nPriority, nStackSize, CREATE_SUSPENDED, lpSecurityAttrs);
	if (pThread != nullptr)
	{
		CSingleLock lock(&Events.csWorkEvent, true);
		bRunning = true;
		dwThreadID = pThread->m_nThreadID;
		bool bGotThreadDescription = ThreadDescription.IsInitialized();
		DWORD dwThreadAccess = SYNCHRONIZE | THREAD_QUERY_INFORMATION;
		if (bGotThreadDescription)
			dwThreadAccess |= THREAD_SET_LIMITED_INFORMATION;
		hThread = OpenThread(dwThreadAccess, FALSE, dwThreadID);
		if (hThread == nullptr)					// typically: error 5 access denied
		{
			// the thread was created, we have to resume it so it dies a quick death (hThread == nullptr, so it will die immediately)
			(void)pThread->ResumeThread();
			// wait for it to die.
			(void)WaitForSingleObject(pThread->m_hThread, SECONDS(1));
			return nullptr;
		}
		// if win10 or later, set thread description
		if (bGotThreadDescription)
		{
			CString sThreadName(typeid(*this).name());
			(void)ThreadDescription.SetThreadDescription(hThread, sThreadName);
		}
		(void)pThread->ResumeThread();
	}
	return pThread != nullptr;
}

void CSimpleWorkerThread::StartWork()
{
	CSingleLock lock(&Events.csWorkEvent, true);

	if (Events.m_pWorkEvent != nullptr)
		(void)Events.m_pWorkEvent->SetEvent();
}

UINT CSimpleWorkerThread::ThreadProc(LPVOID pParam)
{
	CSimpleWorkerThread *pSimpleThread = (CSimpleWorkerThread *)pParam;
	if (pSimpleThread->hThread == nullptr)
		return 0;
	HANDLE EventArray[MAXIMUM_WAIT_OBJECTS];
	DWORD iEvent;

	// keep a list of all current threads
	{
		CSingleLock csGlobalList(pcsGlobalThreadSet, true);
		pair<set<CSimpleWorkerThread *>::iterator,bool> ret = pGlobalThreadSetActive->insert(pSimpleThread);
		ASSERT(ret.second);
	}
	if (!pSimpleThread->InitInstance())
		pSimpleThread->bTerminate = true;
	while (!pSimpleThread->bTerminate)
	{
		// create handle list
		iEvent = 0;
		for (UINT i=0 ; (i<pSimpleThread->EventList.size()) && (i < (MAXIMUM_WAIT_OBJECTS - 1)) ; i++)
			EventArray[iEvent++] = pSimpleThread->EventList[i]->m_hObject;
		EventArray[iEvent++] = pSimpleThread->Events.m_pWorkEvent->m_hObject;		// the first is the default event
		pSimpleThread->dwEventRet = WaitForMultipleObjectsEx(iEvent, EventArray, false, pSimpleThread->dwWaitTime, pSimpleThread->bAlertable);
		if (pSimpleThread->dwEventRet == WAIT_FAILED)
		{
			DWORD dwWaitError = GetLastError();
			if (pSimpleThread->WaitFailed(dwWaitError))
				LogMessage(_T(__FILE__), __LINE__, _T("WaitForMultipleObjectsEx error"), dwWaitError);
		}
		else
			pSimpleThread->DoWork();
	}
	pSimpleThread->ExitInstance();
	{
		CSingleLock csGlobalList(pcsGlobalThreadSet, true);
		(void)pGlobalThreadSetActive->erase(pSimpleThread);
	}
	{
		CSingleLock lock(&pSimpleThread->Events.csWorkEvent, true);
		if (pSimpleThread->Events.m_pWorkEvent != nullptr)
		{
			delete pSimpleThread->Events.m_pWorkEvent;
			pSimpleThread->Events.m_pWorkEvent = nullptr;
		}
		pSimpleThread->pThread = nullptr;
		if (pSimpleThread->hThread != nullptr)
		{
			if (!CloseHandle(pSimpleThread->hThread))				// allow the thread to terminate
				LogMessage(_T(__FILE__), __LINE__, _T("CloseHandle error"), GetLastError());
			pSimpleThread->hThread = nullptr;
			pSimpleThread->dwThreadID = 0;
		}
		pSimpleThread->bRunning = false;
		pSimpleThread->SignalTermination();
	}
	return 0;
}

void CSimpleWorkerThread::KillThread() throw()
{
	CSingleLock lock(&Events.csWorkEvent, true);

	GetSystemTimeAsFileTime(&ftEndThreadTime);
	bTerminate = true;
	if (Events.m_pWorkEvent != nullptr)
		(void)Events.m_pWorkEvent->SetEvent();
}

void CSimpleWorkerThread::KillThreadWait(bool bDontKillWaitOnly) throw()
{
	if ((pThread == nullptr) || (dwThreadID == 0))
		return;
	DWORD dwRet;
	CThreadEvent ThreadEvent;
	DWORD dwEventError = ThreadEvent.Create(dwThreadID);
	if (dwEventError != ERROR_SUCCESS)
	{
		DEBUGF(_T("CSimpleWorkerThread::KillThreadWait ThreadEvent error: cur:%d kill:%d Error:%d"), GetCurrentThreadId(), dwThreadID, dwEventError);
	}
	if (!bDontKillWaitOnly)
		KillThread();
	CSingleLock lockKillThread(&Events.csKillThread, true);
	if (Events.m_pWorkEvent == nullptr)
		return;
	while (IfActive())
	{
		// Start up the other thread so it can complete.
		// When it does, it will set the exit event and the object can be 
		// destructed.
		// wait for the thread to terminate
		if (dwEventError == ERROR_SUCCESS)
		{
			dwRet = WaitForSingleObject(ThreadEvent.m_hObject, 100);
			if (dwRet == WAIT_TIMEOUT)
				continue;
			if (dwRet == WAIT_FAILED)
				break;
		}
		else
			Sleep(100);
	}
	{
		CSingleLock lockWorkEvent(&Events.csWorkEvent, true);
		if (Events.m_pWorkEvent != nullptr)
			delete Events.m_pWorkEvent;
		Events.m_pWorkEvent = nullptr;
	}
}

bool CSimpleWorkerThread::InitInstance()
{
	return true;
}

void CSimpleWorkerThread::ExitInstance()
{
}

void CSimpleWorkerThread::SetCycleTime(UINT nMilliSecs)
{
	dwWaitTime = nMilliSecs;
}

UINT CSimpleWorkerThread::GetCycleTime() const
{
	return dwWaitTime;
}

bool CSimpleWorkerThread::GetExitFlag(void) const
{
	return bTerminate;
}

DWORD CSimpleWorkerThread::GetCurrentThreadID(void) const
{
	CSingleLock lock(const_cast<CCriticalSection *> (&Events.csWorkEvent), true);
	if (pThread != nullptr)
		return pThread->m_nThreadID;
	return 0;
}

void CSimpleWorkerThread::SetAlertable(bool bAlertableParam)
{
	bAlertable = bAlertableParam;
}

bool CSimpleWorkerThread::IfActive() const
{
	// if bRunning is false, the thread is either dead, or it is so close to dead that we don't care
	// in other words, the thread proc has returned
	// there are cases, such as in the DLL unload call, where an OS critical section is held which won't
	// allow the thread to fully terminate. While in this state a deadlock occurs if we try to wait
	// for the thread to terminate, so we'll just wait until it returns from the thread proc
	if (!bRunning)
		return false;
	// if bRunning is set, that may not mean that the thread is actually running, since it is possible
	// for the thread to be killed without returning from the thread proc, so test if the thread is running
	DWORD dwExitCode = 0;
	CSingleLock lock(const_cast<CCriticalSection *> (&Events.csWorkEvent), true);
	if (hThread == nullptr)
		return false;
	if (!GetExitCodeThread(hThread, &dwExitCode))
		return false;
	if (dwExitCode == STILL_ACTIVE)
		return true;
	if (!CloseHandle(hThread))				// allow the thread to terminate
		LogMessage(_T(__FILE__), __LINE__, _T("CloseHandle error"), GetLastError());
	*const_cast<HANDLE *>(&hThread) = nullptr;
	return false;
}

bool CSimpleWorkerThread::WaitFailed(DWORD dwError)	// over-ride to catch any error after Multi-event wait
{
	(void)dwError;
	return true;					// log error
}

CString CSimpleWorkerThread::Format() const
{
	return GetThreadType() + _T(" ThreadID=") + FmtNum(dwThreadID)
		+ _T(" handle=0x") + FmtNum(hThread, 6, true, true)
		+ _T(" WaitTime=") + FmtNum(dwWaitTime)
		+ _T(" Running=") + (bRunning ? _T("true") : _T("false"))
		+ _T(" Terminate=") + (bTerminate ? _T("true") : _T("false"))
		+ _T(" Alertable=") + (bAlertable ? _T("true") : _T("false"))
		+ _T("\r\n");
}

void CSimpleWorkerThread::DumpHandles(CString& sHandleMsg)
{
	if (!pcsGlobalThreadSet)
		return;
	CSingleLock lockGlobalList(pcsGlobalThreadSet, true);
	if (pGlobalThreadSet)
	{
		for (set<CSimpleWorkerThread *>::iterator itSet=pGlobalThreadSet->begin() ; itSet != pGlobalThreadSet->end() ; ++itSet)
		{
			sHandleMsg += (*itSet)->Format();
		}
	}
	sHandleMsg += _T("\r\nActive:\r\n");
	if (pGlobalThreadSetActive)
	{
		for (set<CSimpleWorkerThread *>::iterator itSet=pGlobalThreadSetActive->begin() ; itSet != pGlobalThreadSetActive->end() ; ++itSet)
		{
			sHandleMsg += (*itSet)->Format();
		}
	}
}

CString CSimpleWorkerThread::GetThreadType(void) const
{
	return FROM_ANSI(typeid(*this).name());
}

void CSimpleWorkerThread::GetTerminateTime(FILETIME *pftEndThreadTime) const
{
	ASSERT(pftEndThreadTime != nullptr);
	*pftEndThreadTime = ftEndThreadTime;
}

bool CSimpleWorkerThread::AddTermEvent(CEvent * pEvent)
{
	CSingleLock lock(const_cast<CCriticalSection *> (&Events.csWorkEvent), true);
	if (!IfActive())
		return false;
	TermEventList.push_back(pEvent);
	return true;
}

void CSimpleWorkerThread::RemoveTermEvent(CEvent * pEvent)
{
	CSingleLock lock(const_cast<CCriticalSection *> (&Events.csWorkEvent), true);
	for (list<CEvent *>::const_iterator itTerm = TermEventList.begin(); itTerm != TermEventList.end(); )
	{
		if (*itTerm == pEvent)
			itTerm = TermEventList.erase(itTerm);
		else
			++itTerm;
	}
}

void CSimpleWorkerThread::RemoveAllTermEvent(void)
{
	CSingleLock lock(const_cast<CCriticalSection *> (&Events.csWorkEvent), true);
	TermEventList.clear();
}

CSimpleWorkerThread *CSimpleWorkerThread::CurrentThread(void)
{
	DWORD dwCurThreadID = GetCurrentThreadId();
	CSimpleWorkerThread *pCurThread;
	ASSERT(pcsGlobalThreadSet != nullptr);
	CSingleLock lockGlobalList(pcsGlobalThreadSet, true);
	set<CSimpleWorkerThread *>::iterator itSet;
	for (itSet = pGlobalThreadSetActive->begin(); itSet != pGlobalThreadSetActive->end(); ++itSet)
	{
		pCurThread = *itSet;
		if (pCurThread->dwThreadID == dwCurThreadID)
			return pCurThread;
	}
	return nullptr;
}

CCriticalSection *CSimpleWorkerThread::GetGlobalListCriticalSection(void)
{
	return pcsGlobalThreadSet;
}

void CSimpleWorkerThread::AllTerminate()
{
	// clean up all worker threads
	if (CSimpleWorkerThread::pcsGlobalThreadSet != nullptr)
	{
		{
			CSingleLock lock(CSimpleWorkerThread::pcsGlobalThreadSet, true);
			for (set<CSimpleWorkerThread *>::const_iterator it = CSimpleWorkerThread::pGlobalThreadSet->begin();
				it != CSimpleWorkerThread::pGlobalThreadSet->end(); ++it)
			{
				if ((*it)->IfActive())
					(*it)->KillThread();
			}
		}
		for (;;)
		{
			CSingleLock lock(CSimpleWorkerThread::pcsGlobalThreadSet, true);
			bool bActive = false;
			for (set<CSimpleWorkerThread *>::const_iterator it = CSimpleWorkerThread::pGlobalThreadSet->begin();
				it != CSimpleWorkerThread::pGlobalThreadSet->end(); ++it)
			{
				if ((*it)->IfActive())
				{
					bActive = true;
					break;
				}
			}
			if (!bActive)
				break;
			lock.Unlock();
			Sleep(50);
		}
		delete CSimpleWorkerThread::pcsGlobalThreadSet;
		delete CSimpleWorkerThread::pGlobalThreadSet;
		CSimpleWorkerThread::pcsGlobalThreadSet = nullptr;
		CSimpleWorkerThread::pGlobalThreadSet = nullptr;
	}
}
