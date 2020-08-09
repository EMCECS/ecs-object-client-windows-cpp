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

using namespace std;

#include <atlbase.h>
#include <atlstr.h>
#include <atlenc.h>
#include <Winhttp.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <VersionHelpers.h>
#include "generic_defs.H"
#include "XmlLiteUtil.h"
#include "CngAES_GCM.h"
#include "UriUtils.h"
#include "NTERRTXT.H"
#include "ECSConnection.h"
#include "GetAllThreads.h"

#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "Crypt32.lib")


// WinHttp notifications that we register for:
#define ECS_CONN_WINHTTP_CALLBACK_FLAGS (WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE | WINHTTP_CALLBACK_FLAG_HANDLES)

bool CECSConnection::bInitialized = false;						// starts out false. If false, timeouts are very short. must call SetInitialized to get regular timeouts
map<CString, CECSConnection::THROTTLE_REC> CECSConnection::ThrottleMap;	// global map used by all CECSConnection objects
CCriticalSection CECSConnection::csThrottleMap;				// critical section protecting ThrottleMap
list<CECSConnection *> CECSConnection::ECSConnectionList;			// list of all CECSConnection objects (protected by csThrottleMap)
CECSConnection::CThrottleTimerThread CECSConnection::TimerThread;	// throttle timer thread
DWORD CECSConnection::dwGlobalHttpsProtocol = 0;
DWORD CECSConnection::dwS3BucketListingMax = 1000;					// maxiumum number of items to return on a bucket listing (S3). Default = 1000 (cannot be larger than 1000)

CCriticalSection CECSConnection::csBadIPMap;
map<CECSConnection::BAD_IP_KEY,CECSConnection::BAD_IP_ENTRY> CECSConnection::BadIPMap;		// protected by csBadIPMap
map<CString,UINT> CECSConnection::LoadBalMap;										// protected by csBadIPMap
list<CECSConnection::XML_DIR_LISTING_CONTEXT *> CECSConnection::DirListList;	// listing of current dir listing operations
CCriticalSection CECSConnection::csDirListList;				// critical section protecting DirListList
CCriticalSection CECSConnection::csSessionMap;
map<CECSConnection::SESSION_MAP_KEY, CECSConnection::SESSION_MAP_VALUE> CECSConnection::SessionMap;			// protected by rwlSessionMap
long CECSConnection::lSessionKeyValue;
CString CECSConnection::sAmzMetaPrefix(TEXT("x-amz-meta-"));						// just a place to hold "x-amz-meta-"
set<CString> CECSConnection::SystemMDSet;					// set of system metadata fields that can be indexed

// global performance counters
CSimpleRWLock CECSConnection::rwlGlobalPerf;
list<CECSConnection::GLOBAL_PERF_POINTERS> CECSConnection::GlobalPerfList;

DWORD CECSConnection::dwMaxRetryCount(MaxRetryCount);				// max retries for HTTP command
DWORD CECSConnection::dwPauseBetweenRetries(500);					// pause between retries (millisec)
DWORD CECSConnection::dwPauseAfter500Error(500);					// pause between retries after HTTP 500 error (millisec)

static TCHAR *SystemMDInit[] =
{
	_T("ObjectName"),
	_T("Owner"),
	_T("Size"),
	_T("CreateTime"),
	_T("LastModified"),
	_T("ContentType"),
	_T("Expiration"),
	_T("ContentEncoding"),
	_T("Expires"),
	_T("Retention")
};

class CSignQuerySet
{
	static CSimpleRWLock lwrSignQuerySet;
	static set<CString> SignQuerySet;
	static LPCTSTR InitArray[];

public:
	CSignQuerySet()
	{}
	void Init()
	{
		CSimpleRWLockAcquire lock(&lwrSignQuerySet, true);		// write lock
		if (SignQuerySet.empty())
		{
			for (UINT i = 0; InitArray[i][0] != NUL; i++)
				SignQuerySet.emplace(InitArray[i]);
		}
	}
	bool IfQuery(const CString& sQuery)
	{
		CSimpleRWLockAcquire lock(&lwrSignQuerySet);		// read lock
		return SignQuerySet.find(sQuery) != SignQuerySet.end();
	}
};

CSimpleRWLock CSignQuerySet::lwrSignQuerySet;
set<CString> CSignQuerySet::SignQuerySet;
LPCTSTR CSignQuerySet::InitArray[] =
{
	_T("acl"),
	_T("lifecycle"),
	_T("searchmetadata"),
	_T("location"),
	_T("logging"),
	_T("notification"),
	_T("partnumber"),
	_T("policy"),
	_T("requestpayment"),
	_T("torrent"),
	_T("uploadid"),
	_T("uploads"),
	_T("versionid"),
	_T("versioning"),
	_T("versions"),
	_T("website"),
	_T("response-content-type"),
	_T("response-content-language"),
	_T("response-expires"),
	_T("response-cache-control"),
	_T("response-content-disposition"),
	_T("response-content-encoding"),
	_T("delete"),
	_T("endpoint"),
	_T("query"),
	_T("")							// sentinel
};
CSignQuerySet SignQuerySet;

struct HTTP_ERROR_ENTRY
{
	DWORD dwHTTPErrorCode;				// HTTP error
	HRESULT dwErrorCode;				// corresponding win32 error code
} HttpErrorInitArray[] =
{
	{ HTTP_STATUS_AMBIGUOUS, HTTP_E_STATUS_AMBIGUOUS},
	{ HTTP_STATUS_MOVED, HTTP_E_STATUS_MOVED},
	{ HTTP_STATUS_REDIRECT, HTTP_E_STATUS_REDIRECT},
	{ HTTP_STATUS_REDIRECT_METHOD, HTTP_E_STATUS_REDIRECT_METHOD},
	{ HTTP_STATUS_NOT_MODIFIED, HTTP_E_STATUS_NOT_MODIFIED},
	{ HTTP_STATUS_USE_PROXY, HTTP_E_STATUS_USE_PROXY},
	{ HTTP_STATUS_REDIRECT_KEEP_VERB, HTTP_E_STATUS_REDIRECT_KEEP_VERB},
	{ HTTP_STATUS_BAD_REQUEST, HTTP_E_STATUS_BAD_REQUEST},
	{ HTTP_STATUS_DENIED, HTTP_E_STATUS_DENIED},
	{ HTTP_STATUS_PAYMENT_REQ, HTTP_E_STATUS_PAYMENT_REQ},
	{ HTTP_STATUS_FORBIDDEN, HTTP_E_STATUS_FORBIDDEN},
	{ HTTP_STATUS_NOT_FOUND, HTTP_E_STATUS_NOT_FOUND},
	{ HTTP_STATUS_BAD_METHOD, HTTP_E_STATUS_BAD_METHOD},
	{ HTTP_STATUS_NONE_ACCEPTABLE, HTTP_E_STATUS_NONE_ACCEPTABLE},
	{ HTTP_STATUS_PROXY_AUTH_REQ, HTTP_E_STATUS_PROXY_AUTH_REQ},
	{ HTTP_STATUS_REQUEST_TIMEOUT, HTTP_E_STATUS_REQUEST_TIMEOUT},
	{ HTTP_STATUS_CONFLICT, HTTP_E_STATUS_CONFLICT},
	{ HTTP_STATUS_GONE, HTTP_E_STATUS_GONE},
	{ HTTP_STATUS_LENGTH_REQUIRED, HTTP_E_STATUS_LENGTH_REQUIRED},
	{ HTTP_STATUS_PRECOND_FAILED, HTTP_E_STATUS_PRECOND_FAILED},
	{ HTTP_STATUS_REQUEST_TOO_LARGE, HTTP_E_STATUS_REQUEST_TOO_LARGE},
	{ HTTP_STATUS_URI_TOO_LONG, HTTP_E_STATUS_URI_TOO_LONG},
	{ HTTP_STATUS_UNSUPPORTED_MEDIA, HTTP_E_STATUS_UNSUPPORTED_MEDIA},
	{ HTTP_STATUS_SERVER_ERROR, HTTP_E_STATUS_SERVER_ERROR},
	{ HTTP_STATUS_NOT_SUPPORTED, HTTP_E_STATUS_NOT_SUPPORTED},
	{ HTTP_STATUS_BAD_GATEWAY, HTTP_E_STATUS_BAD_GATEWAY},
	{ HTTP_STATUS_SERVICE_UNAVAIL, HTTP_E_STATUS_SERVICE_UNAVAIL},
	{ HTTP_STATUS_GATEWAY_TIMEOUT, HTTP_E_STATUS_GATEWAY_TIMEOUT},
	{ HTTP_STATUS_VERSION_NOT_SUP, HTTP_E_STATUS_VERSION_NOT_SUP},
};

map<DWORD, HRESULT> HttpErrorMap;

static bool IfServerReached(DWORD dwError)
{
	switch (dwError)
	{
	case ERROR_WINHTTP_HEADER_NOT_FOUND:
	case ERROR_WINHTTP_INVALID_HEADER:
	case ERROR_WINHTTP_INVALID_QUERY_REQUEST:
	case ERROR_WINHTTP_HEADER_ALREADY_EXISTS:
	case ERROR_WINHTTP_REDIRECT_FAILED:
	case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR:
	case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT:
	case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
	case ERROR_WINHTTP_UNHANDLED_SCRIPT_TYPE:
	case ERROR_WINHTTP_SCRIPT_EXECUTION_ERROR:
		break;
	default:
		return false;
	}
	return true;
}

void CECSConnection::SetPerfStateSize(long lDiff)
{
	if (pulStateMapSize != nullptr)
	{
		long lCurMapSize = InterlockedAdd((long *)pulStateMapSize, lDiff);
		if (pulMaxStateMapSize != nullptr)
		{
			while (lCurMapSize > InterlockedAdd((long *)pulMaxStateMapSize, 0))
			{
				long lOldMax = InterlockedExchange(pulMaxStateMapSize, lCurMapSize);
				if (lOldMax <= lCurMapSize)
					break;
				lCurMapSize = lOldMax;
			}
		}
	}
}

shared_ptr<CECSConnection::CECSConnectionState> CECSConnection::GetStateBuf()
{
	DWORD dwThreadID = GetCurrentThreadId();

	CSimpleRWLockAcquire lock(&StateList.rwlStateMap, false);			// read lock
																		// this will either insert it or if it already exists, return the existing entry for this thread
	map<DWORD, shared_ptr<CECSConnectionState>>::iterator itState = StateList.StateMap.find(dwThreadID);
	if (itState == StateList.StateMap.end())
	{
		lock.Unlock();
		lock.Lock(true);									// get write lock
		size_t MapSize = StateList.StateMap.size();
		pair<map<DWORD, shared_ptr<CECSConnectionState>>::iterator, bool> ret = StateList.StateMap.emplace(make_pair(dwThreadID, make_shared<CECSConnectionState>()));
		if (ret.second)
		{
			ret.first->second->pECSConnection = this;
			SetPerfStateSize(1);
		}
		InterlockedIncrement(&ret.first->second->ulReferenceCount);
		ASSERT(ret.first->second->pECSConnection == this);
		shared_ptr<CECSConnection::CECSConnectionState> StateRet = ret.first->second;
		// if the map is getting large, don't wait for the periodic garbage collection. do it now
		if (MapSize > 500)
		{
			lock.Unlock();
			GarbageCollect();
		}
		return StateRet;
	}
	ASSERT(itState->second->pECSConnection == this);
	InterlockedIncrement(&itState->second->ulReferenceCount);
	return itState->second;
}

CECSConnection::CStateRef::CStateRef(CECSConnection *pConnParam)
	: pConn(pConnParam)
{
	if (pConn != nullptr)
	{
		Ref = pConn->GetStateBuf();
	}
}

CECSConnection::CStateRef::~CStateRef()
{
	if (Ref)
	{
		// ftLastUsed is read only if ulReferenceCount is 0
		GetSystemTimeAsFileTime(&Ref->ftLastUsed);		// no lock needed since only this thread will update these fields
		InterlockedDecrement(&Ref->ulReferenceCount);
	}
}

void CECSConnection::PrepareCmd()
{
	CStateRef State(this);
	ASSERT(State.Ref->dwCurrentThread == 0);
	State.Ref->dwCurrentThread = GetCurrentThreadId();
	{
		CSingleLock lock(&State.Ref->CallbackContext.csContext, true);

		VERIFY(State.Ref->CallbackContext.Event.evCmd.ResetEvent());
		State.Ref->CallbackContext.Reset();
		State.Ref->CallbackContext.bDisableSecureLog = State.Ref->bDisableSecureLog;
		State.Ref->CallbackContext.dwSecureError = State.Ref->dwSecureError;
		State.Ref->CallbackContext.pHost = this;
	}
}

void CECSConnection::CleanupCmd()
{
	CStateRef State(this);
	ASSERT(State.Ref->dwCurrentThread != 0);
	State.Ref->dwCurrentThread = 0;
	CSingleLock lock(&State.Ref->CallbackContext.csContext, true);

	State.Ref->dwSecureError = State.Ref->CallbackContext.dwSecureError;		// any security error is passed back to the main class
}

bool CECSConnection::WaitComplete(DWORD dwCallbackExpected)
{
	CStateRef State(this);
	const UINT MaxWaitInterval = SECONDS(5);			// max time to wait before checking for abort
	DWORD dwError;
	UINT iTimeout = 0;
	UINT iTimeoutMax = max(dwLongestTimeout, SECONDS(90))/MaxWaitInterval + 2;
	for (;;)
	{
		// wait at most 500 ms. then check if we are terminating
		dwError = WaitForSingleObject(State.Ref->CallbackContext.Event.evCmd.m_hObject, MaxWaitInterval);
		// check for thread exit (but not if we are waiting for the handle to close)
		if ((dwCallbackExpected != WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) && TestAbort())
		{
			CSingleLock lock(&State.Ref->CallbackContext.csContext, true);
			State.Ref->CallbackContext.Result.dwError = ERROR_OPERATION_ABORTED;
			State.Ref->CallbackContext.Result.dwResult = 0;
			CleanupCmd();
			return false;
		}
		if (dwError == WAIT_OBJECT_0)
		{
			CSingleLock lock(&State.Ref->CallbackContext.csContext, true);
			bool bGotIt = false;
			// command finished!
			for (list<CMD_RECEIVED>::const_iterator it = State.Ref->CallbackContext.CallbacksReceived.begin()
				; it != State.Ref->CallbackContext.CallbacksReceived.end(); ++it)
			{
				if (it->dwInternetStatus == dwCallbackExpected)
					bGotIt = true;
			}
			State.Ref->CallbackContext.CallbacksReceived.clear();
			// if it is waiting for WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING, don't exit until we get it
			// if it is waiting for anything else, return on failure, or the receipt of the correct callback
			if ((dwCallbackExpected != WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) || bGotIt)
			{
				if (bGotIt || State.Ref->CallbackContext.bFailed)
				{
					CleanupCmd();
					if (State.Ref->CallbackContext.bFailed)
						return false;
					return true;
				}
			}
		}
		else if (dwError == WAIT_TIMEOUT)
		{
			CSingleLock lock(&State.Ref->CallbackContext.csContext, true);
			iTimeout++;
			if (iTimeout > iTimeoutMax)
			{
				State.Ref->CallbackContext.Result.dwError = WAIT_TIMEOUT;
				State.Ref->CallbackContext.Result.dwResult = 0;
				CleanupCmd();
				return false;
			}
		}
		else if (dwError == WAIT_FAILED)
		{
			CSingleLock lock(&State.Ref->CallbackContext.csContext, true);
			State.Ref->CallbackContext.Result.dwError = GetLastError();
			State.Ref->CallbackContext.Result.dwResult = 0;
			CleanupCmd();
			return false;
		}
		else
		{
			ASSERT(false);
			break;
		}
	}
	CleanupCmd();
	return false;
}

bool CECSConnection::WaitStreamSend(STREAM_CONTEXT *pStreamSend, const CSharedQueueEvent& MsgEvent)
{
	const UINT MaxWaitInterval = SECONDS(1);			// max time to wait before checking for abort
	DWORD dwError;
	UINT iTimeout = 0;
	UINT iTimeoutMax = max(dwLongestTimeout, SECONDS(90)) / MaxWaitInterval + 2;
	for (;;)
	{
		// wait then check if we are terminating
		dwError = WaitForSingleObject(MsgEvent.Event.evQueue.m_hObject, MaxWaitInterval);
		if (!pStreamSend->StreamData.empty())
		{
//			DumpDebugFileFmt(_T(__FILE__), __LINE__, _T("WaitStreamSend: SUCCESS %d"), pStreamSend->StreamData.GetCount());
			return true;
		}
		// check for thread exit (but not if we are waiting for the handle to close)
		if (TestAbort())
		{
//			DumpDebugFileFmt(_T(__FILE__), __LINE__, _T("WaitStreamSend: FAIL ABORT %d"), pStreamSend->StreamData.GetCount());
			return false;
		}
		if (dwError == WAIT_OBJECT_0)
			continue;							// wait some more
		if (dwError == WAIT_TIMEOUT)
		{
			iTimeout++;
			if (iTimeout > iTimeoutMax)
			{
//				DumpDebugFileFmt(_T(__FILE__), __LINE__, _T("WaitStreamSend: FAIL TIMEOUT %d"), pStreamSend->StreamData.GetCount());
				return false;
			}
		}
		else if (dwError == WAIT_FAILED)
		{
//			DumpDebugFileFmt(_T(__FILE__), __LINE__, _T("WaitStreamSend: FAIL FAILED %d"), pStreamSend->StreamData.GetCount());
			return false;
		}
		else
		{
			ASSERT(false);
			break;
		}
	}
//	DumpDebugFileFmt(_T(__FILE__), __LINE__, _T("WaitStreamSend: FAIL FAILED DEFAULT %d"), pStreamSend->StreamData.GetCount());
	return false;
}

CECSConnection::CThrottleTimerThread::CThrottleTimerThread()
{
	ZeroFT(ftLast);
}

CECSConnection::CThrottleTimerThread::~CThrottleTimerThread()
{
	KillThreadWait();
};

void CECSConnection::CThrottleTimerThread::DoWork()
{
	if (dwEventRet != WAIT_TIMEOUT)
		return;
	FILETIME ftNow;

	GetSystemTimeAsFileTime(&ftNow);
	ULARGE_INTEGER LastTime;
	if (IfFTZero(ftLast))
		LastTime.QuadPart = FT_SECONDS(1);
	else
		LastTime.QuadPart = FTtoULarge(ftNow).QuadPart - FTtoULarge(ftLast).QuadPart;
	ftLast = ftNow;
	map<CString,CECSConnection::THROTTLE_REC>::iterator itMap;
	CSingleLock lock(&csThrottleMap, true);
	for (itMap=CECSConnection::ThrottleMap.begin() ; itMap!=CECSConnection::ThrottleMap.end() ; ++itMap)
	{
		if (itMap->second.Upload.iBytesSec != 0)
		{
			int iBytesSec = (int)((itMap->second.Upload.iBytesSec * LastTime.QuadPart)/FT_SECONDS(1));
			itMap->second.Upload.iBytesCurInterval += iBytesSec;		// add it in case the current count was negative, which indicates that in the previous second someone sent too many bytes, we'll make up for it now
			if (itMap->second.Upload.iBytesCurInterval > iBytesSec)
				itMap->second.Upload.iBytesCurInterval = iBytesSec;
		}
		else
			itMap->second.Upload.iBytesCurInterval = 0;
		if (itMap->second.Download.iBytesSec != 0)
		{
			int iBytesSec = (int)((itMap->second.Download.iBytesSec * LastTime.QuadPart)/FT_SECONDS(1));
			itMap->second.Download.iBytesCurInterval += iBytesSec;		// add it in case the current count was negative, which indicates that in the previous second someone sent too many bytes, we'll make up for it now
			if (itMap->second.Download.iBytesCurInterval > iBytesSec)
				itMap->second.Download.iBytesCurInterval = iBytesSec;
		}
		else
			itMap->second.Download.iBytesCurInterval = 0;
		// now notify any current objects waiting for this moment
		list<CECSConnection *>::iterator itList;
		for (itList = CECSConnection::ECSConnectionList.begin() ; itList != CECSConnection::ECSConnectionList.end() ; ++itList)
		{
			if ((*itList)->sHost == itMap->first)
			{
				CSimpleRWLockAcquire lockStateMap(&(*itList)->StateList.rwlStateMap, false);
				map<DWORD,shared_ptr<CECSConnectionState>>::iterator itStateMap;
				for (itStateMap = (*itList)->StateList.StateMap.begin(); itStateMap != (*itList)->StateList.StateMap.end(); ++itStateMap)
					VERIFY(itStateMap->second->evThrottle.SetEvent());
			}
		}
	}
}

bool CECSConnection::CThrottleTimerThread::InitInstance()
{
	SetCycleTime(SECONDS(1));
	ZeroFT(ftLast);
	return true;		// return FALSE to abort the thread
}

CECSConnection::CECSConnection()
{
	ZeroFT(ftS3V4SigningKey);
	CSingleLock lock(&csThrottleMap, true);
	ECSConnectionList.push_back(this);
}

void CECSConnection::Init()
{
	SignQuerySet.Init();
	for (UINT i = 0; i < _countof(HttpErrorInitArray); i++)
		(void)HttpErrorMap.insert(make_pair(HttpErrorInitArray[i].dwHTTPErrorCode, HttpErrorInitArray[i].dwErrorCode));
	for (UINT i = 0; i < _countof(SystemMDInit); ++i)
		(void)SystemMDSet.insert(SystemMDInit[i]);

	bInitialized = true;
};

void CECSConnection::SetS3BucketListingMax(DWORD dwS3BucketListingMaxParam)
{
	dwS3BucketListingMax = dwS3BucketListingMaxParam;
}

// sets protocol flags for all connections
// protocol parameter combination (OR) of the following defines:
//	WINHTTP_FLAG_SECURE_PROTOCOL_SSL2
//	WINHTTP_FLAG_SECURE_PROTOCOL_SSL3
//	WINHTTP_FLAG_SECURE_PROTOCOL_TLS1
//	WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
//	WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
// for example:
//	WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
void CECSConnection::SetGlobalHttpsProtocol(DWORD dwGlobalHttpsProtocolParam)
{
	dwGlobalHttpsProtocol = dwGlobalHttpsProtocolParam;
}

// sets protocol flags for specific connection
// protocol parameter combination (OR) of the following defines:
//	WINHTTP_FLAG_SECURE_PROTOCOL_SSL2
//	WINHTTP_FLAG_SECURE_PROTOCOL_SSL3
//	WINHTTP_FLAG_SECURE_PROTOCOL_TLS1
//	WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
//	WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
// for example:
//	WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
void CECSConnection::SetHttpsProtocol(DWORD dwHttpsProtocolParam)
{
	dwHttpsProtocol = dwHttpsProtocolParam;
}

CECSConnection::~CECSConnection()
{
	CloseAll();
	CSingleLock lock(&csThrottleMap, true);
	ECSConnectionList.remove(this);
}

CECSConnection::CECSConnection(const CECSConnection& Rec)
{
	*this = Rec;
	CSingleLock lock(&csThrottleMap, true);
	ECSConnectionList.push_back(this);
}

BOOL CECSConnection::WinHttpQueryHeadersBuffer(
	__in      HINTERNET hHandle,
	__in      DWORD dwInfoLevel,
	__in_opt  LPCTSTR pwszName,
	__inout   CBuffer& RetBuf,
	__inout   LPDWORD lpdwIndex
	)
{
	DWORD dwLen = 0;
	if (WinHttpQueryHeaders(hHandle, dwInfoLevel, (pwszName == WINHTTP_HEADER_NAME_BY_INDEX) ? WINHTTP_HEADER_NAME_BY_INDEX : (LPCWSTR)TO_UNICODE(pwszName), WINHTTP_NO_OUTPUT_BUFFER, &dwLen, lpdwIndex))
		return TRUE;
	DWORD dwError = GetLastError();
	if (dwError != ERROR_INSUFFICIENT_BUFFER)
		return FALSE;
	RetBuf.SetBufSize(dwLen);
	return WinHttpQueryHeaders(hHandle, dwInfoLevel, (pwszName == WINHTTP_HEADER_NAME_BY_INDEX) ? WINHTTP_HEADER_NAME_BY_INDEX : (LPCWSTR)TO_UNICODE(pwszName), RetBuf.GetData(), &dwLen, lpdwIndex);
}

// GetCanonicalTime
// output current time in the format:
//   Thu, 05 Jun 2008 16:38:19 GMT
CString CECSConnection::GetCanonicalTime(const SYSTEMTIME *pstTime)
{
	SYSTEMTIME stNow;

	if (pstTime == nullptr)
	{
		GetSystemTime(&stNow);
		pstTime = &stNow;
	}
	CStringW sTimeStr;
	LPWSTR pszTimeStr = sTimeStr.GetBuffer(WINHTTP_TIME_FORMAT_BUFSIZE / sizeof(WCHAR));

	if (!WinHttpTimeFromSystemTime(pstTime, pszTimeStr))
	{
		sTimeStr.ReleaseBuffer();
		return _T("");
	}
	sTimeStr.ReleaseBuffer();
#ifndef _UNICODE
	CString sTimeStrA(sTimeStr);
	return sTimeStrA;
#else
	return sTimeStr;
#endif
}

// ParseCanonicalTime
// output current time in the format:
//   Thu, 05 Jun 2008 16:38:19 GMT
FILETIME CECSConnection::ParseCanonicalTime(LPCTSTR pszCanonTime)
{
	CStringW sCanonTime(TO_UNICODE(pszCanonTime));
	SYSTEMTIME stTime;
	FILETIME ftTime;

	ZeroFT(ftTime);
	if (!WinHttpTimeToSystemTime(sCanonTime, &stTime))
		return ftTime;
	if (!SystemTimeToFileTime(&stTime, &ftTime))
		return ftTime;
	return ftTime;
}

// ParseISO8601Date
// convert ISO 8601 date to FILETIME
//   yyyy-mm-ddThh:mm:ss.mmmZ
DWORD CECSConnection::ParseISO8601Date(LPCTSTR pszDate, FILETIME& ftDate, bool bLocal)
{
	SYSTEMTIME stDate, stDateUTC;

	ZeroMemory(&stDate, sizeof(stDate));
	ZeroFT(ftDate);
	if (_stscanf_s(pszDate, _T("%hd-%hd-%hdT%hd:%hd:%hd.%hd"),
		&stDate.wYear, &stDate.wMonth, &stDate.wDay, &stDate.wHour, &stDate.wMinute, &stDate.wSecond, &stDate.wMilliseconds) < 3)
	{
		if (_stscanf_s(pszDate, _T("%4hd%2hd%2hdT%2hd%2hd%2hd%3hd"),
			&stDate.wYear, &stDate.wMonth, &stDate.wDay, &stDate.wHour, &stDate.wMinute, &stDate.wSecond, &stDate.wMilliseconds) < 3)
		{
			return ERROR_INVALID_DATA;
		}
	}
	if (bLocal)
	{
		if (!TzSpecificLocalTimeToSystemTime(nullptr, &stDate, &stDateUTC))
			return GetLastError();
		stDate = stDateUTC;
	}
	if (!SystemTimeToFileTime(&stDate, &ftDate))
		return GetLastError();
	return ERROR_SUCCESS;
}

CString CECSConnection::FormatISO8601Date(const FILETIME& ftDate, bool bLocal, bool bMilliSec, bool bBasicFormat)
{
	SYSTEMTIME stDateUTC;
	if (!FileTimeToSystemTime(&ftDate, &stDateUTC))
		return _T("");
	return FormatISO8601Date(stDateUTC, bLocal, bMilliSec, bBasicFormat);
}

CString CECSConnection::FormatISO8601Date(const SYSTEMTIME& stDateUTC, bool bLocal, bool bMilliSec, bool bBasicFormat)
{
	SYSTEMTIME stDate;
	if (bLocal)
	{
		if (!SystemTimeToTzSpecificLocalTime(nullptr, &stDateUTC, &stDate))
			return _T("");
	}
	else
		stDate = stDateUTC;
	CString sDateSep, sTimeSep;
	if (!bBasicFormat)
	{
		sDateSep = _T('-');
		sTimeSep = _T(':');
	}
	return FmtNum(stDate.wYear, 4, true) + sDateSep + FmtNum(stDate.wMonth, 2, true) + sDateSep + FmtNum(stDate.wDay, 2, true)
		+ _T("T") + FmtNum(stDate.wHour, 2, true) + sTimeSep + FmtNum(stDate.wMinute, 2, true) + sTimeSep + FmtNum(stDate.wSecond, 2, true)
		+ (bMilliSec ? (LPCTSTR)(_T(".") + FmtNum(stDate.wMilliseconds, 3, true)) : _T(""))
		+ _T("Z");
}

void CECSConnection::SetGlobalPerformanceCounters(const list<GLOBAL_PERF_POINTERS>& PerfListParam)
{
	CSimpleRWLockAcquire lock(&rwlGlobalPerf, true);		// write lock
	GlobalPerfList = PerfListParam;
}

void CECSConnection::SetPerformanceCounters(ULONGLONG *pullPerfBytesSentParam, ULONGLONG *pullPerfBytesRcvParam, ULONG *pulStateMapSizeParam, ULONG *pulMaxStateMapSizeParam)
{
	pullPerfBytesSent = pullPerfBytesSentParam;
	pullPerfBytesRcv = pullPerfBytesRcvParam;
	pulStateMapSize = pulStateMapSizeParam;
	pulMaxStateMapSize = pulMaxStateMapSizeParam;
	// get the current state info
	if (pulStateMapSize != nullptr)
	{
		CSimpleRWLockAcquire lock(&StateList.rwlStateMap, true);			// write lock
		SetPerfStateSize((LONG)StateList.StateMap.size());
	}
}

static void CheckQuery(const CString& sQuery, map<CString, CString>& QueryMap)
{
	int iEqual = sQuery.Find(_T('='));
	CString sToken;
	if (iEqual < 0)
		sToken = sQuery;
	else
		sToken = sQuery.Left(iEqual);
	sToken.MakeLower();
	sToken.TrimLeft();
	sToken.TrimRight();
	if (SignQuerySet.IfQuery(sToken))
	{
		(void)QueryMap.insert(make_pair(sToken, UriDecode(sQuery)));
	}
}

// CECSConnection::signRequestS3v2
//
//Authorization = "AWS" + " " + AWSAccessKeyId + ":" + Signature;
//
//Signature = Base64( HMAC-SHA1( YourSecretAccessKeyID, UTF-8-Encoding-Of( StringToSign ) ) );
//
//StringToSign = HTTP-Verb + "\n" +
//	Content-MD5 + "\n" +
//	Content-Type + "\n" +
//	Date + "\n" +
//	CanonicalizedAmzHeaders +
//	CanonicalizedResource;
//
//CanonicalizedResource = [ "/" + Bucket ] +
//	<HTTP-Request-URI, from the protocol name up to the query string> +
//	[ subresource, if present. For example "?acl", "?location", "?logging", or "?torrent"];
//
CString CECSConnection::signRequestS3v2(const CString& secretStr, const CString& method, const CString& resource, const map<CString, HEADER_STRUCT>& headers, LPCTSTR pszExpires)
{
	CString sAuthorization;
	try
	{
		map<CString, HEADER_STRUCT>::const_iterator iter;
		//		<HTTPMethod>\n
		CString sCanonical(method);
		sCanonical.MakeUpper();
		sCanonical += _T("\n");
		//		Content-MD5
		if ((iter = headers.find(_T("content-md5"))) != headers.end())
			sCanonical += iter->second.sContents + _T("\n");
		else
			sCanonical += _T("\n");

		//		Content-Type
		if ((pszExpires == nullptr) && ((iter = headers.find(_T("content-type"))) != headers.end()))
			sCanonical += iter->second.sContents + _T("\n");
		else
			sCanonical += _T("\n");

		//		Date
		if (pszExpires != nullptr)
			sCanonical += CString(pszExpires) + _T("\n");
		else if ((iter = headers.find(_T("x-amz-date"))) != headers.end())
			sCanonical += iter->second.sContents + _T("\n");
		else if ((iter = headers.find(_T("date"))) != headers.end())
			sCanonical += iter->second.sContents + _T("\n");
		else
			sCanonical += _T("\n");

		//	CanonicalizedAmzHeaders +
		CString sToken;
		for (iter = headers.lower_bound(_T("x-")); iter != headers.end(); ++iter) {
			if ((iter->first.Find(_T("x-amz")) == 0) || (iter->first.Find(_T("x-emc")) == 0))
			{
				sToken = iter->second.sContents;
				// now get rid of multiple spaces
				int iBlank;
				while ((iBlank = sToken.Find(_T("  "))) >= 0)
					(void)sToken.Delete(iBlank);
				(void)sToken.TrimRight();
				(void)sToken.TrimLeft();
				sCanonical += iter->first + _T(':') + sToken + _T("\n");
			}
		}
		//	CanonicalizedResource;
		CString sResource(resource);
		int iQuestion = sResource.Find(_T('?'));
		if (iQuestion >= 0)
		{
			CString sQueryString = resource.Mid(iQuestion);
			(void)sResource.Delete(iQuestion, resource.GetLength() - iQuestion);
			int pos = 0;
			CString sQuery;
			map<CString, CString> QueryMap;
			for (;;)
			{
				sQuery = sQueryString.Tokenize(_T("?&"), pos);
				if (sQuery.IsEmpty())
					break;
				CheckQuery(sQuery, QueryMap);
			}
			if (!QueryMap.empty())
			{
				for (map<CString, CString>::const_iterator itMap = QueryMap.begin(); itMap != QueryMap.end(); ++itMap)
				{
					if (itMap == QueryMap.begin())
						sResource += _T("?");
					else
						sResource += _T("&");
					sResource += itMap->second;
				}
			}
		}
		//sCanonical += UriEncode(sResource);
		sCanonical += sResource;
		// Signature = Base64( HMAC-SHA1( YourSecretAccessKeyID, UTF-8-Encoding-Of( StringToSign ) ) );
		CAnsiString AnsiPassword;
		CBuffer Outbuf;

	#ifdef _UNICODE
		// convert input string to UTF-8
		AnsiPassword.Set(secretStr, -1, CP_UTF8);
		// convert canonical string to UTF-8
		CAnsiString UTF8Buf;
		UTF8Buf.Set(sCanonical, -1, CP_UTF8);
	#else
		AnsiPassword.Load(secretStr, (DWORD)strlen(secretStr) + 1);
		CAnsiString UTF8Buf(sCanonical);
	#endif
		// HMAC-SHA1
		CCngAES_GCM Hash;
		Hash.CreateHash(BCRYPT_SHA1_ALGORITHM, AnsiPassword.GetData(), AnsiPassword.GetBufSize());
		Hash.AddHashData(UTF8Buf.GetData(), (UINT)strlen(UTF8Buf));
		Hash.GetHashData(Outbuf);

		// now convert to base64
		// Authorization = "AWS" + " " + AWSAccessKeyId + ":" + Signature;
		if (pszExpires == nullptr)
			sAuthorization = _T("AWS ") + sS3KeyID + _T(":") + Outbuf.EncodeBase64();
		else
			sAuthorization = Outbuf.EncodeBase64();
	}
	catch (const CErrorInfo& E)
	{
		(void)E;
		sAuthorization.Empty();
	}
	return sAuthorization;
}

// CreateS3SigningKey
//	DateKey              = HMAC-SHA256("AWS4"+"<SecretAccessKey>", "<yyyymmdd>")
//	DateRegionKey        = HMAC-SHA256(<DateKey>, "<aws-region>")
//	DateRegionServiceKey = HMAC-SHA256(<DateRegionKey>, "<aws-service>")
//	SigningKey           = HMAC-SHA256(<DateRegionServiceKey>, "aws4_request")
void CECSConnection::CreateS3SigningKey(
	const CString& secretStr,						// (i) secret access key
	const SYSTEMTIME& stRequestTime,				// (i) request timestamp
	CBuffer& SigningKey)							// (o) signing key
{
	CString sStr;
	CAnsiString AnsiKey, AnsiContent;

	//	DateKey              = HMAC-SHA256("AWS4"+"<SecretAccessKey>", "<yyyymmdd>")
	AnsiKey.Set(_T("AWS4") + secretStr, -1, CP_UTF8);
	AnsiKey.SetBufSize((DWORD)strlen(AnsiKey));
	sStr.Format(_T("%04d%02d%02d"), stRequestTime.wYear, stRequestTime.wMonth, stRequestTime.wDay);
	AnsiContent.Set(sStr, -1, CP_UTF8);
	AnsiContent.SetBufSize((DWORD)strlen(AnsiContent));
	CCngAES_GCM DataHash;
	DataHash.CreateHash(BCRYPT_SHA256_ALGORITHM, AnsiKey);
	DataHash.AddHashData(AnsiContent);
	CBuffer DateKey;
	DataHash.GetHashData(DateKey);

	//	DateRegionKey        = HMAC-SHA256(<DateKey>, "<aws-region>")
	DataHash.CreateHash(BCRYPT_SHA256_ALGORITHM, DateKey);
	CAnsiString AWSRegion;
	AWSRegion.Set(sS3Region, -1, CP_UTF8);
	AWSRegion.SetBufSize((DWORD)strlen(AWSRegion));
	DataHash.AddHashData(AWSRegion);
	CBuffer DateRegionKey;
	DataHash.GetHashData(DateRegionKey);

	//	DateRegionServiceKey = HMAC-SHA256(<DateRegionKey>, "<aws-service>")
	DataHash.CreateHash(BCRYPT_SHA256_ALGORITHM, DateRegionKey);
	DataHash.AddHashData((const BYTE *)"s3", 2);
	CBuffer DateRegionServiceKey;
	DataHash.GetHashData(DateRegionServiceKey);

	//	SigningKey           = HMAC-SHA256(<DateRegionServiceKey>, "aws4_request")
	DataHash.CreateHash(BCRYPT_SHA256_ALGORITHM, DateRegionServiceKey);
	DataHash.AddHashData((const BYTE *)"aws4_request", 12);
	DataHash.GetHashData(SigningKey);

	// now save this signing key for future use
	{
		FILETIME ftRequestTime;
		(void)SystemTimeToFileTime(&stRequestTime, &ftRequestTime);
		CSimpleRWLockAcquire lockWrite(&lwrS3V4SigningKey, true);

		ftS3V4SigningKey = ftRequestTime;
		GlobalS3V4SigningKey = SigningKey;
	}
}

// CECSConnection::signRequestS3v4
//
//		<HTTPMethod>\n
//		<CanonicalURI>\n
//		<CanonicalQueryString>\n
//		<CanonicalHeaders>\n
//		<SignedHeaders>\n
//		<HashedPayload>
CString CECSConnection::signRequestS3v4(
	const CString& secretStr,
	const CString& method,
	const CString& resource,
	const map<CString, HEADER_STRUCT>& headers,
	const void *pData,
	DWORD dwDataLen,
	E_S3_V4_PAYLOAD PayloadType,
	ULONGLONG ullTotalLen,
	ULONGLONG ullTotalPayloadLen,
	CBuffer& S3SigningKey,
	CString& sPreviousSignature,
	const SYSTEMTIME& stRequestTime)
{
	CString sAuthorization;
	try
	{
		// first get a hash of the payload
		CCngAES_GCM DataHash;
		CBuffer Hash;
		CString sPayloadHash;
		switch (PayloadType)
		{
		case E_S3_V4_PAYLOAD::Signed:
		{
			CBuffer PayloadHash;
			DataHash.CreateHash(BCRYPT_SHA256_ALGORITHM);
			if ((pData != nullptr) && (dwDataLen != 0))
				DataHash.AddHashData((const BYTE *)pData, dwDataLen);
			else
				DataHash.AddHashData((const BYTE *)"", 0);
			DataHash.GetHashData(PayloadHash);
			sPayloadHash = BinaryToHexString(PayloadHash);
			AddHeader(_T("x-amz-content-sha256"), sPayloadHash);
		}
		break;
		case E_S3_V4_PAYLOAD::Chunked:
		{
			AddHeader(_T("x-amz-decoded-content-length"), FmtNum(ullTotalPayloadLen));
			AddHeader(_T("content-encoding"), _T("aws-chunked"));
			sPayloadHash = _T("STREAMING-AWS4-HMAC-SHA256-PAYLOAD");
			AddHeader(_T("x-amz-content-sha256"), sPayloadHash);
		}
		break;
		case E_S3_V4_PAYLOAD::Unsigned:
		{
			sPayloadHash = _T("UNSIGNED-PAYLOAD");
		}
		break;
		default:
			ASSERT(false);
			break;
		}
		// split out the query string before the UriDecode because the resource may have a '?'
		CString sTempResource, sFullQuery;
		int iQuery = resource.Find(_T("?"));
		if (iQuery >= 0)
		{
			sFullQuery = resource.Mid(iQuery + 1);
			sTempResource = resource.Left(iQuery);
		}
		else
			sTempResource = resource;
		// make sure the resource is encoded for v4
		sTempResource = UriEncode(UriDecode(sTempResource), E_URI_ENCODE::V4Auth);

		//		<HTTPMethod>\n
		CString sCanonical(method);
		sCanonical.MakeUpper();
		sCanonical += _T("\n");
		//		<CanonicalURI>\n
		if (sTempResource.IsEmpty())
			sTempResource = _T("/");
		sCanonical += sTempResource + _T("\n");
		//		<CanonicalQueryString>
		if (!sFullQuery.IsEmpty())
		{
			CString sQuery;
			// each query separated by '&'
			// each query of form: var=value
			int iPosQuery = 0;
			map<CString, CString> QueryMap;
			for (;;)
			{
				sQuery = sFullQuery.Tokenize(_T("&"), iPosQuery);
				if (sQuery.IsEmpty())
					break;
				int iEquals = sQuery.Find(_T('='));
				pair<map<CString, CString>::iterator, bool> ret;
				if (iEquals >= 0)
					ret = QueryMap.insert(make_pair(UriEncode(UriDecode(sQuery.Left(iEquals)), E_URI_ENCODE::V4AuthSlash), UriEncode(UriDecode(sQuery.Mid(iEquals + 1)), E_URI_ENCODE::V4AuthSlash)));
				else
					ret = QueryMap.insert(make_pair(UriEncode(UriDecode(sQuery), E_URI_ENCODE::V4AuthSlash), _T("")));
				ASSERT(ret.second);
			}
			for (map<CString, CString>::const_iterator itMap = QueryMap.begin(); itMap != QueryMap.end(); ++itMap)
			{
				if (itMap != QueryMap.begin())
					sCanonical += _T("&");
				sCanonical += itMap->first + _T("=") + itMap->second;
			}
		}
		sCanonical += _T("\n");
		//		<CanonicalHeaders>\n
		//		<SignedHeaders>\n
		CString sSignedHeaders;
		for (map<CString, HEADER_STRUCT>::const_iterator itMap = headers.begin(); itMap != headers.end(); ++itMap)
		{
			if (!sSignedHeaders.IsEmpty())
				sSignedHeaders += _T(";");
			sSignedHeaders += itMap->first;
			CString sLabel(itMap->first), sValue(itMap->second.sContents);
			sCanonical += sLabel.Trim() + _T(":") + sValue.Trim() + _T("\n");
		}
		sCanonical += _T("\n") + sSignedHeaders + _T("\n");
		//		<HashedPayload>
		sCanonical += sPayloadHash;
		// create StringToSign
		//	"AWS4-HMAC-SHA256" + \n" +
		//	timeStampISO8601Format + "\n" +
		//	<Scope> +"\n" +
		//	Hex(SHA256Hash(<CanonicalRequest>))
		CString sStringToSign(_T("AWS4-HMAC-SHA256\n")), sStr;
//		sStringToSign += GetCanonicalTime(&stRequestTime) + _T("\n");
		sStringToSign += FormatISO8601Date(stRequestTime, false, false, true) + _T("\n");
		sStr.Format(_T("%04d%02d%02d/%s/s3/aws4_request\n"), stRequestTime.wYear, stRequestTime.wMonth, stRequestTime.wDay, (LPCTSTR)sS3Region);
		sStringToSign += sStr;
		DataHash.CreateHash(BCRYPT_SHA256_ALGORITHM);
		CAnsiString AnsiCanonical;
		AnsiCanonical.Set(sCanonical, -1, CP_UTF8);
		AnsiCanonical.SetBufSize((DWORD)strlen(AnsiCanonical));
		DataHash.AddHashData(AnsiCanonical);
		DataHash.GetHashData(Hash);
		sStringToSign += BinaryToHexString(Hash);
		// check if we have a signing key already created for this host
		{
			CSimpleRWLockAcquire lockRead(&lwrS3V4SigningKey, false);				// get read lock

			// if the date is the same, we can use the signing key
			SYSTEMTIME stS3SigningKey;
			FileTimeToSystemTime(&ftS3V4SigningKey, &stS3SigningKey);
			if ((stRequestTime.wYear == stS3SigningKey.wYear)
				&& (stRequestTime.wMonth == stS3SigningKey.wMonth)
				&& (stRequestTime.wDay == stS3SigningKey.wDay))
			{
				// got it! We can use the stored signing key
				S3SigningKey = GlobalS3V4SigningKey;
			}
		}
		if (S3SigningKey.IsEmpty())
			CreateS3SigningKey(secretStr, stRequestTime, S3SigningKey);

		// sign the sStringToSign
		CAnsiString StringToSign(sStringToSign);
		StringToSign.SetBufSize((DWORD)strlen(StringToSign));
		CBuffer Signature;
		DataHash.CreateHash(BCRYPT_SHA256_ALGORITHM, S3SigningKey);
		DataHash.AddHashData(StringToSign);
		DataHash.GetHashData(Signature);

		CString sSignature(BinaryToHexString(Signature));
		sPreviousSignature = sSignature;
		// format date into yyyyMMdd for the Authorization header
		sAuthorization.Format(_T("AWS4-HMAC-SHA256 Credential=%s/%04u%02u%02u/%s/s3/aws4_request,SignedHeaders=%s,Signature=%s"),
			(LPCTSTR)sS3KeyID, stRequestTime.wYear, stRequestTime.wMonth, stRequestTime.wDay, (LPCTSTR)sS3Region, (LPCTSTR)sSignedHeaders, (LPCTSTR)sSignature);
	}
	catch (const CErrorInfo& E)
	{
		(void)E;
		sAuthorization.Empty();
	}
	return sAuthorization;
}

void CALLBACK CECSConnection::HttpStatusCallback(
	__in  HINTERNET hInternet,
	__in  DWORD_PTR dwContext,
	__in  DWORD dwInternetStatus,
	__in  LPVOID lpvStatusInformation,
	__in  DWORD dwStatusInformationLength)
{
	(void)hInternet;
	HTTP_CALLBACK_CONTEXT* pContext = (HTTP_CALLBACK_CONTEXT*)dwContext;
	ASSERT(pContext != nullptr);
	// this callback can run on any thread, so this needs to be synchronized with the initiating thread (SendRequestInternal)
	// it has been possible that the lock (pContext->csContext) is not fully destroyed before the destructor is called on the HTTP_CALLBACK_CONTEXT structure
	// the lock is put in its own block and a flag and event are used to make sure the caller doesn't exit until the lock below is fully destroyed
	{
		CSingleLock lock(&pContext->csContext, true);
		pContext->Event.evExit.ResetEvent();						// reset exit event
		InterlockedExchange(&pContext->lExitFlag, 0);				// reset exit flag
#ifdef DEBUG
		if (dwInternetStatus != WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
		{
			ASSERT(!pContext->bComplete);
			ASSERT((dwInternetStatus == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE)
				|| (dwInternetStatus == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE)
				|| (dwInternetStatus == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
				|| (dwInternetStatus == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
				|| (dwInternetStatus == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)
				|| (dwInternetStatus == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE)
				|| (dwInternetStatus == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
				|| (dwInternetStatus == WINHTTP_CALLBACK_STATUS_HANDLE_CREATED)
				|| (dwInternetStatus == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING));
		}
#endif
		CMD_RECEIVED RcvdRec;
		RcvdRec.dwInternetStatus = dwInternetStatus;
		GetSystemTimeAsFileTime(&RcvdRec.ftCmdTime);
		pContext->CallbacksReceived.push_back(RcvdRec);
		switch (dwInternetStatus)
		{
		case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
			{
				ASSERT((dwStatusInformationLength == sizeof(DWORD)) && (lpvStatusInformation != nullptr));
				if ((dwStatusInformationLength == sizeof(DWORD)) && (lpvStatusInformation != nullptr))
				{
					DWORD dwStatus = *((DWORD*)lpvStatusInformation);
					pContext->dwSecureError |= dwStatus;
				}
			}
			break;
		case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:				// WinHttpQueryDataAvailable finished
			{
				ASSERT(dwStatusInformationLength == 4);
				pContext->dwReadLength = *((DWORD*)lpvStatusInformation);
				pContext->bComplete = true;
				VERIFY(pContext->Event.evCmd.SetEvent());
			}
			break;
		case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:				// WinHttpReceiveResponse finished
			{
				pContext->bHeadersAvail = true;
				pContext->bComplete = true;
				VERIFY(pContext->Event.evCmd.SetEvent());
			}
			break;
		case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:					// WinHttpReadData finished
			{
				pContext->pReadData = (BYTE*)lpvStatusInformation;
				pContext->dwReadLength = dwStatusInformationLength;
				pContext->bComplete = true;
				VERIFY(pContext->Event.evCmd.SetEvent());
			}
			break;
		case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:			// WinHttpSendRequest finished
			{
				pContext->bSendRequestComplete = true;
				pContext->bComplete = true;
				VERIFY(pContext->Event.evCmd.SetEvent());
			}
			break;
		case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:				// WinHttpWriteData finished
			{
				ASSERT(dwStatusInformationLength == 4);
				pContext->dwBytesWritten = *((DWORD*)lpvStatusInformation);
				pContext->bComplete = true;
				VERIFY(pContext->Event.evCmd.SetEvent());
			}
			break;
		case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:					// Any of the above functions when an error occurs.
			{
				pContext->bFailed = true;
				pContext->bComplete = true;
				pContext->Result = *((WINHTTP_ASYNC_RESULT*)lpvStatusInformation);
				VERIFY(pContext->Event.evCmd.SetEvent());
			}
			break;
		case WINHTTP_CALLBACK_STATUS_HANDLE_CREATED:
			{
				ASSERT(dwStatusInformationLength == sizeof(HINTERNET));
				HINTERNET hIntHandle = *((HINTERNET*)lpvStatusInformation);
				(void)hIntHandle;
				pContext->bHandleCreated = true;
			}
			break;
		case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
			{
				ASSERT(dwStatusInformationLength == sizeof(HINTERNET));
				HINTERNET hIntHandle = *((HINTERNET*)lpvStatusInformation);
				pContext->bHandleClosing = true;
				pContext->bComplete = true;
				VERIFY(pContext->Event.evCmd.SetEvent());
			}
			break;
		default:
			break;
		}
	}
	// now signal the caller that the callback is done
	InterlockedExchange(&pContext->lExitFlag, 1);			// set exit flag
	pContext->Event.evExit.SetEvent();						// set exit event
}

DWORD CECSConnection::InitSession()
{
	CStateRef State(this);
	State.Ref->Session.AllocSession(sHost, GetCurrentServerIP());
	DWORD dwError = ERROR_SUCCESS;
	// Use WinHttpOpen to obtain a session handle.
	CString sVer;
	if (sUserAgent.IsEmpty())
		sVer = _T("Test/1.0");
	else
		sVer = sUserAgent;
	if (!State.Ref->Session.pValue->hSession.IfOpen() || !State.Ref->Session.pValue->hConnect.IfOpen())
	{
		State.Ref->Session.pValue->CloseAll();
		CString sProxyString(sProxy);
		if (!sProxyString.IsEmpty())
		{
			if (dwProxyPort != 0)
				sProxyString += _T(":") + FmtNum(dwProxyPort);
			State.Ref->Session.pValue->hSession = WinHttpOpen(TO_UNICODE(sVer),
				WINHTTP_ACCESS_TYPE_NAMED_PROXY,
				TO_UNICODE(sProxyString),
				L"<local>",
				WINHTTP_FLAG_ASYNC);
		}
		else
		{
			State.Ref->Session.pValue->hSession = WinHttpOpen(TO_UNICODE(sVer),
				bUseDefaultProxy ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				WINHTTP_FLAG_ASYNC);
		}
		if (!State.Ref->Session.pValue->hSession.IfOpen())
			return GetLastError();
		// the protocol specific for this connection takes precedence
		DWORD dwTempProtocol = dwHttpsProtocol;
		if (dwTempProtocol == 0)
			dwTempProtocol = dwGlobalHttpsProtocol;
		if (dwTempProtocol != 0)
		{
			if (!WinHttpSetOption(State.Ref->Session.pValue->hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwTempProtocol, sizeof(dwTempProtocol)))
			{
				dwError = GetLastError();
				State.Ref->Session.pValue->CloseAll();
				return dwError;
			}
		}
		ASSERT(!State.Ref->Session.pValue->hConnect.IfOpen());
		// Specify an HTTP server.
		State.Ref->Session.pValue->hConnect = WinHttpConnect(State.Ref->Session.pValue->hSession, TO_UNICODE(GetCurrentServerIP()), Port, 0);
		if (!State.Ref->Session.pValue->hConnect.IfOpen())
		{
			dwError = GetLastError();
			State.Ref->Session.pValue->CloseAll();
			return dwError;
		}
	}
	return ERROR_SUCCESS;
}

void CECSConnection::CECSConnectionState::CloseRequest(bool bSaveCert) throw()
{
	// Close any request handle.
	if (hRequest.IfOpen())
	{
		if (bSaveCert || bSaveCertInfo)
		{
			// if unknown CA, get details about the certificate and get the certificate itself
			// we can present the details to the user for validation, and install the certificate
			// in the root store so this certificate will be accepted in the future
			if (pECSConnection != nullptr)
				(void)pECSConnection->CECSConnection::RetrieveServerCertificate(CertInfo);
		}
		if (bCallbackRegistered)
			if (pECSConnection != nullptr)
				pECSConnection->PrepareCmd();
		(void)hRequest.CloseHandle();
		if (bCallbackRegistered)
			if (pECSConnection != nullptr)
				(void)pECSConnection->WaitComplete(WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING);
		bCallbackRegistered = false;
	}
}

void CECSConnection::CloseAll() throw()
{
	CStateRef State(this);
	// Close any open handles.
	State.Ref->CloseRequest();
}

// InitHeader
// set headers common to all requests
void CECSConnection::InitHeader(void)
{
	CStateRef State(this);
	State.Ref->Headers.clear();
	// add the list of headers common to all requests
	SYSTEMTIME stNow;
	GetSystemTime(&stNow);
	AddHeader(_T("host"), GetCurrentServerIP());
	if (!IfS3v4())
		AddHeader(_T("date"), GetCanonicalTime(&stNow));
	else
		AddHeader(_T("x-amz-date"), FormatISO8601Date(stNow, false, false, true));
}

void CECSConnection::AddHeader(LPCTSTR pszHeaderLabel, LPCTSTR pszHeaderText, bool bOverride)
{
	CStateRef State(this);
	CString sHeaderLabel(pszHeaderLabel);
	sHeaderLabel.TrimLeft();
	sHeaderLabel.TrimRight();
	CString sLabelLower(sHeaderLabel);
	sLabelLower.MakeLower();
	HEADER_STRUCT Hdr;
	Hdr.sHeader = sHeaderLabel;
	Hdr.sContents = pszHeaderText;
	pair<map<CString, HEADER_STRUCT>::iterator, bool> ret = State.Ref->Headers.insert(make_pair(sLabelLower, Hdr));
	if (!ret.second && bOverride)
	{
		ret.first->second.sHeader = sHeaderLabel;
		ret.first->second.sContents = pszHeaderText;
	}
}

// GetCurrentServerIP
// pick from one of the ip addrs going to this host
LPCTSTR CECSConnection::GetCurrentServerIP(void)
{
	CStateRef State(this);
	if (State.Ref->IPListLocal.empty())
		return _T("?");
	if (State.Ref->iIPList >= State.Ref->IPListLocal.size())
		State.Ref->iIPList = 0;
	return State.Ref->IPListLocal[State.Ref->iIPList];
}

// GetNextECSIP
// pick from one of the ip addrs going to this host
// returns false if there are no available IPs for this host
bool CECSConnection::GetNextECSIP(map<CString,BAD_IP_ENTRY>& IPUsed)
{
	CStateRef State(this);
	if (dwBadIPAddrAge == 0)
	{
		dwBadIPAddrAge = MINUTES(12);
	}
	__int64 liBadIPAge = (__int64)dwBadIPAddrAge * 10000;			// convert to FILETIME units
	if (State.Ref->IPListLocal.empty())
		return false;
	// first see if any of the entries are too old to be in there
	CSingleLock csBad(&csBadIPMap, true);

	FILETIME ftNow;
	GetSystemTimeAsFileTime(&ftNow);
	for (map<BAD_IP_KEY, BAD_IP_ENTRY>::iterator itMap = BadIPMap.begin(); itMap != BadIPMap.end(); )
	{
		if (ftNow > (itMap->second.ftError + liBadIPAge))
			itMap = BadIPMap.erase(itMap);
		else
			++itMap;
	}

	// insert new LoadBalMap record, or retrieve existing record
	pair<map<CString, UINT>::iterator, bool> Ret = LoadBalMap.insert(make_pair(sHost, 0));
	if (++Ret.first->second >= State.Ref->IPListLocal.size())
		Ret.first->second = 0;
	State.Ref->iIPList = Ret.first->second;

	bool bBad;
	UINT i;
	for (i = 0; i<State.Ref->IPListLocal.size(); i++)
	{
		bBad = false;
		if (State.Ref->iIPList >= State.Ref->IPListLocal.size())
			State.Ref->iIPList = 0;
		// check if this entry is BAD
		if (BadIPMap.find(BAD_IP_KEY(sHost, State.Ref->IPListLocal[State.Ref->iIPList])) != BadIPMap.end())
			bBad = true;					// bad - move to the next
		// check if we've already tried this one
	//	if (!bBad && (IPUsed.find(State.Ref->IPListLocal[State.Ref->iIPList]) != IPUsed.end()))
	//		bBad = true;
		if (!bBad)
			break;
		++State.Ref->iIPList;					// try the next
	}
	// update the current IP pointer as it might have changed
	Ret.first->second = State.Ref->iIPList;
	return i < State.Ref->IPListLocal.size();
}

// TestAllIPBad
// check if all IPs are marked bad for this host
// if so, remove them all from the bad map so it can be retried
void CECSConnection::TestAllIPBad()
{
	CStateRef State(this);
	deque<CString> IPList = State.Ref->IPListLocal;
	{
		// we need to test and reset a rare possibilty that all of the IPs for this host have been marked bad
		// normally that shouldn't happen because an IP is only marked bad if another IP was successful
		// but due to multiple overlapped requests, it is a possibility
		// in this case, the failure that caused the bad IPs was likely intermittent, so reset them and try again
		{
			CSingleLock csBad(&csBadIPMap, true);

			// run through the map, for each entry for this host, delete any corresponding entry in IPList
			// if, after going through the whole map, if IPList is empty that means that all of the IPs were marked bad
			for (map<BAD_IP_KEY, BAD_IP_ENTRY>::iterator itMap = BadIPMap.begin(); itMap != BadIPMap.end(); ++itMap)
			{
				if (sHost == itMap->first.sHostName)
				{
					for (deque<CString>::iterator it = IPList.begin(); it != IPList.end(); )
					{
						if (*it == itMap->first.sIP)
							it = IPList.erase(it);
						else
							++it;
					}
				}
			}
			// if IPList is now empty, all entries were marked bad. unmark them
			if (IPList.empty())
			{
				for (map<BAD_IP_KEY, BAD_IP_ENTRY>::iterator itMap = BadIPMap.begin(); itMap != BadIPMap.end();)
				{
					if (sHost == itMap->first.sHostName)
						itMap = BadIPMap.erase(itMap);
					else
						++itMap;
				}
			}
		}
	}
}

// LogBadIPAddr
// log the list of addresses that were unsuccessful
// this is only called if it eventually did connect via one of the addresses
// if all of the addresses failed, it calls the 'disconnect' callback and will try again later
// there is no point in blaming the problem on each individual address
void CECSConnection::LogBadIPAddr(const map<CString,BAD_IP_ENTRY>& IPUsed)
{
	CSingleLock csBad(&csBadIPMap, true);
	FILETIME ftNow;

	if (bTestConnection)				// only mark an IP bad if there is only 1 IP in the list
		return;
	BAD_IP_ENTRY Entry;
	GetSystemTimeAsFileTime(&ftNow);
	for (map<CString,BAD_IP_ENTRY>::const_iterator itUsed=IPUsed.begin() ; itUsed != IPUsed.end() ; ++itUsed)
	{
		Entry.ftError = ftNow;
		Entry.ErrorInfo = itUsed->second.ErrorInfo;
		pair<map<BAD_IP_KEY,BAD_IP_ENTRY>::iterator,bool> Ret = BadIPMap.insert(make_pair(BAD_IP_KEY(sHost, itUsed->first), Entry));
		if (!Ret.second)				// already in the map
			Ret.first->second = Entry;
		else
		{
			LogMessage(itUsed->second.ErrorInfo.sFile, itUsed->second.ErrorInfo.dwLine, _T("Server (%1) error caused a failover to a different connection: %2\r\nConnection that failed: %3\r\nConnection that is now in use: %4"),
					itUsed->second.ErrorInfo.Error.dwError, (LPCTSTR)GetHost(), (LPCTSTR)itUsed->second.ErrorInfo.Format(), (LPCTSTR)itUsed->first, GetCurrentServerIP());
		}
	}
	TestAllIPBad();
}

// IfMarkIPBad
// test if this error should mark the IP address bad
bool CECSConnection::IfMarkIPBad(DWORD dwError)
{
	if ((dwError >= WINHTTP_ERROR_BASE) && (dwError <= WINHTTP_ERROR_LAST))
	{
		if ((dwError != ERROR_WINHTTP_OPERATION_CANCELLED)
			&& (dwError != ERROR_WINHTTP_INVALID_SERVER_RESPONSE))
			return true;
	}
	return false;
}

// SendRequest
// complete the request and send it, and get the response
// adds the following headers:
//	if dwDatalen != 0: content-length: <dwDataLen>
CECSConnection::S3_ERROR CECSConnection::SendRequest(
	LPCTSTR pszMethod,
	LPCTSTR pszResource,
	const void *pData,
	DWORD dwDataLen,
	CBuffer& RetData,
	list<HEADER_REQ> *pHeaderReq,
	DWORD dwReceivedDataHint,				// an approximate size of the received data to aid in allocation
	DWORD dwBufOffset,						// reserve dwBufOffset bytes at the start of the buffer
	STREAM_CONTEXT *pStreamSend,			// if supplied, don't use pData/dwDataLen. wait on this queue for data to send
	STREAM_CONTEXT *pStreamReceive,			// is supplied, don't return data through RetData, push it on this queue
	ULONGLONG ullTotalLen)					// for StreamSend the total length of the transfer, for StreamReceive, the starting offset into the object
{
	CStateRef State(this);
	bool bGotServerResponse = false;
	S3_ERROR Error;
	map<CString,BAD_IP_ENTRY> IPUsed;
	CString sResource(pszResource);
	list<HEADER_REQ> SaveHeaderReq;
	try
	{
		if (pHeaderReq != nullptr)
			SaveHeaderReq = *pHeaderReq;					// save in case of retries
		// first get a local copy of the IPList for use by this request
		{
			CSimpleRWLockAcquire lock(&rwlIPListHost);			// read lock
			State.Ref->IPListLocal = IPListHost;
		}
		if (!GetNextECSIP(IPUsed))
		{
			TestAllIPBad();					// if first time through and there are no IPs it could be that they are all marked bad
			if (!GetNextECSIP(IPUsed))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, (DWORD)SEC_E_NO_IP_ADDRESSES);		// no IP addresses!
		}
		if (bTestConnection)
			dwMaxRetryCount = __min(2UL, dwMaxRetryCount);
		for (UINT i = 0; i < dwMaxRetryCount; i++)
		{
			bGotServerResponse = false;
			// update the date header in case retries have made this time too far in the past
			SYSTEMTIME stNow;
			GetSystemTime(&stNow);
			if (State.Ref->Headers.find(_T("date")) != State.Ref->Headers.end())
				AddHeader(_T("date"), GetCanonicalTime(&stNow));
			if (State.Ref->Headers.find(_T("x-amz-date")) != State.Ref->Headers.end())
				AddHeader(_T("x-amz-date"), FormatISO8601Date(stNow, false, false, true));
			Error = SendRequestInternal(pszMethod, sResource, pData, dwDataLen, RetData, pHeaderReq, dwReceivedDataHint, dwBufOffset, &bGotServerResponse, pStreamSend, pStreamReceive, ullTotalLen);
			if (!Error.IfError())
			{
				// no error - but let's look a little closer
				// we MUST have gotten to the server, AND there should be an HTTP error code returned which should be 20x
				if (bGotServerResponse
					&& ((Error.dwHttpError == 0)
						|| ((Error.dwHttpError >= HTTP_STATUS_OK) && (Error.dwHttpError < HTTP_STATUS_AMBIGUOUS))))
				{
					if (!IPUsed.empty())
						LogBadIPAddr(IPUsed);
					return Error;							// success!
				}
				if (!bGotServerResponse)
					Error.dwError = ERROR_HOST_UNREACHABLE;			// fake an error code if it was successful, but didn't reach Atmos
				else
					Error.dwError = (DWORD)HTTP_E_STATUS_UNEXPECTED;
			}
			// don't retry if this is a stream operation
			// we can't retry because the data stream would need to be reset
			if ((pStreamSend != nullptr) || (pStreamReceive != nullptr))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			if ((Error.dwHttpError < HTTP_STATUS_SERVER_ERROR)
				&& (bGotServerResponse || (Error.dwError == ERROR_WINHTTP_SECURE_FAILURE)))
			{
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);					// if we got through to the server, no need to retry
			}
			// if timeout, don't retry more than twice. this takes forever
			if ((Error.dwError == ERROR_WINHTTP_TIMEOUT) && (i > 1))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			if ((!bInitialized || bTestConnection) && (Error.dwError == ERROR_WINHTTP_TIMEOUT))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);					// during initialization - don't retry. if it fails right away too bad
			if (Error.dwError == ERROR_OPERATION_ABORTED)		// if aborted, don't retry.
				return Error;
			// if this is some TCP/IP type error, try marking the IP bad
			if (IfMarkIPBad(Error.dwError))
			{
				// save the error for the log message
				pair<map<CString, BAD_IP_ENTRY>::iterator, bool> Ret = IPUsed.insert(make_pair(GetCurrentServerIP(), BAD_IP_ENTRY(Error)));
			}
			// try another IP address in the list
			if (!GetNextECSIP(IPUsed))
				break;
			// restore pHeaderReq in case it had changed with the failed request
			if (pHeaderReq != nullptr)
				*pHeaderReq = SaveHeaderReq;
			if (Error.dwHttpError == HTTP_STATUS_SERVER_ERROR)
			{
				// if ECS is busy, wait a bit and try again. hopefully on a different node
				Sleep(dwPauseAfter500Error);
			}
			else
				Sleep(dwPauseBetweenRetries);
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		Error = E.Error;
		// must be some kind of comm error, call the "disconnect" callback
		if (Error.IfError() && (DisconnectCB != nullptr) && !bTestConnection)
		{
			if (!bGotServerResponse
				|| (Error.S3Error == S3_ERROR_InvalidAccessKeyId)
				|| (Error.S3Error == S3_ERROR_RequestTimeTooSkewed))
			{
				if ((Error.dwError != ERROR_OPERATION_ABORTED)
					&& (Error.dwError != ERROR_WINHTTP_INVALID_SERVER_RESPONSE)
					&& (Error.dwError != ERROR_WINHTTP_CONNECTION_ERROR))
				{
					CS3ErrorInfo ErrorInfo(E);
					ErrorInfo.sAdditionalInfo = CString(pszMethod) + _T("|") + sResource;
					DisconnectCB(this, &ErrorInfo, nullptr);
				}
			}
		}
	}
	if (!Error.IfError() && !IPUsed.empty())
	{
		// got through but had problems with at least one IP address
		LogBadIPAddr(IPUsed);
	}
	return Error;
}

struct XML_ECS_ERROR_CONTEXT
{
	CECSConnection::S3_ERROR *pError;
	XML_ECS_ERROR_CONTEXT()
		: pError(nullptr)
	{}
};

const WCHAR * const XML_ECS_ADMIN_ERROR_CODE = L"//error/code";
const WCHAR * const XML_ECS_ADMIN_ERROR_DESC = L"//error/description";
const WCHAR * const XML_ECS_ADMIN_ERROR_DETAILS = L"//error/details";
const WCHAR * const XML_ECS_ADMIN_ERROR_RETRYABLE = L"//error/retryable";

HRESULT XmlECSAdminErrorCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_ECS_ERROR_CONTEXT *pInfo = (XML_ECS_ERROR_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((psValue != nullptr) && !psValue->IsEmpty())
		{
			if (sXmlPath.CompareNoCase(XML_ECS_ADMIN_ERROR_CODE) == 0)
			{
				pInfo->pError->S3Error = S3TranslateError(FROM_UNICODE(*psValue));
				pInfo->pError->sS3Code = FROM_UNICODE(*psValue);
			}
			else if ((sXmlPath.CompareNoCase(XML_ECS_ADMIN_ERROR_DESC) == 0)
				|| (sXmlPath.CompareNoCase(XML_ECS_ADMIN_ERROR_DETAILS) == 0))
			{
				if (!pInfo->pError->sDetails.IsEmpty())
					pInfo->pError->sDetails += _T(": ");
				pInfo->pError->sDetails += FROM_UNICODE(*psValue);
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

const WCHAR * const XML_S3_ERROR_CODE = L"//Error/Code";
const WCHAR * const XML_S3_ERROR_MESSAGE = L"//Error/Message";
const WCHAR * const XML_S3_ERROR_RESOURCE = L"//Error/Resource";
const WCHAR * const XML_S3_ERROR_REQUEST_ID = L"//Error/RequestId";

HRESULT XmlS3ErrorCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_ECS_ERROR_CONTEXT *pInfo = (XML_ECS_ERROR_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((psValue != nullptr) && !psValue->IsEmpty())
		{
			if (sXmlPath.CompareNoCase(XML_S3_ERROR_CODE) == 0)
			{
				pInfo->pError->S3Error = S3TranslateError(FROM_UNICODE(*psValue));
				pInfo->pError->sS3Code = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_ERROR_MESSAGE) == 0)
				pInfo->pError->sDetails = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_ERROR_RESOURCE) == 0)
				pInfo->pError->sS3Resource = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_ERROR_REQUEST_ID) == 0)
				pInfo->pError->sS3RequestID = FROM_UNICODE(*psValue);
		}
		break;

	default:
		break;
	}
	return 0;
}

// SetTimeouts
// set timeouts if there are any that are not default
void CECSConnection::SetTimeouts(const CInternetHandle& hRequest)
{
	// if any of the timeout values are zero, replace them with the winhttp default
	if (dwWinHttpOptionConnectRetries == 0)
		dwWinHttpOptionConnectRetries = 5;
	if (dwWinHttpOptionConnectTimeout == 0)
		dwWinHttpOptionConnectTimeout = SECONDS(60);
	if (dwWinHttpOptionReceiveResponseTimeout == 0)
		dwWinHttpOptionReceiveResponseTimeout = SECONDS(90);
	if (dwWinHttpOptionReceiveTimeout == 0)
		dwWinHttpOptionReceiveTimeout = SECONDS(30);
	if (dwWinHttpOptionSendTimeout == 0)
		dwWinHttpOptionSendTimeout = SECONDS(30);

	// figure out the longest timeout value for all of them
	dwLongestTimeout = __max(dwWinHttpOptionConnectTimeout, __max(dwWinHttpOptionReceiveResponseTimeout, __max(dwWinHttpOptionReceiveTimeout, dwWinHttpOptionSendTimeout)));

	if (bInitialized && !bTestConnection)
	{
		if (!WinHttpSetTimeouts(hRequest, 0, dwWinHttpOptionConnectTimeout, dwWinHttpOptionSendTimeout, dwWinHttpOptionReceiveTimeout))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());

		if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_RETRIES, &dwWinHttpOptionConnectRetries, sizeof(dwWinHttpOptionConnectRetries))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwWinHttpOptionConnectTimeout, sizeof(dwWinHttpOptionConnectTimeout))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &dwWinHttpOptionReceiveResponseTimeout, sizeof(dwWinHttpOptionReceiveResponseTimeout))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwWinHttpOptionReceiveTimeout, sizeof(dwWinHttpOptionReceiveTimeout))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &dwWinHttpOptionSendTimeout, sizeof(dwWinHttpOptionSendTimeout)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
	}
	else
	{
		DWORD dwTimeout = SECONDS(2);
		if (bTestConnection)
			dwTimeout = SECONDS(15);
		// short timeouts used only during initialization
		if (!WinHttpSetTimeouts(hRequest, 0, dwTimeout, dwTimeout, dwTimeout))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
		DWORD dwRetries = 1;
		if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_RETRIES, &dwRetries, sizeof(dwRetries))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &dwTimeout, sizeof(dwTimeout))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout))
			|| !WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
	}
}

// used where a class method can't be used
static bool TestAbortStatic(void *pContext)
{
	CECSConnection *pHost = (CECSConnection *)pContext;
	return pHost->TestAbort();
}

CStringA CECSConnection::BuildV4ChunkMetadata(UINT uChunkLength, const CString& sSignature)
{
	CStateRef State(this);
	CStringA sChunkPrefix;
	sChunkPrefix.Format(State.Ref->sChunkMetadataFormatA, uChunkLength, (LPCTSTR)sSignature);
	ASSERT((State.Ref->uS3AuthV4ChunkMetadataOffset == 0) || (sChunkPrefix.GetLength() == State.Ref->uS3AuthV4ChunkMetadataOffset));
	return sChunkPrefix;
}

void CECSConnection::CreateS3V4ChunkMetadata(
	CString& sPreviousSignature,
	CBuffer& S3AuthV4SendBuf,
	UINT& uS3AuthV4SendBufIndex,
	const CString& sEmptySignature,
	const CBuffer& S3SigningKey,
	const SYSTEMTIME& stRequestTime)
{
	CStateRef State(this);
	CCngAES_GCM Hash;
	CString sSignature;

	CBuffer CurrentHash;
	CString sStringToSign(_T("AWS4-HMAC-SHA256-PAYLOAD\n")), sStr;
	//		sStringToSign += GetCanonicalTime(&stRequestTime) + _T("\n");
	sStringToSign += FormatISO8601Date(stRequestTime, false, false, true) + _T("\n");
	sStr.Format(_T("%04d%02d%02d/%s/s3/aws4_request\n"), stRequestTime.wYear, stRequestTime.wMonth, stRequestTime.wDay, (LPCTSTR)sS3Region);
	sStringToSign += sStr;
	sStringToSign += sPreviousSignature + _T("\n");
	sStringToSign += sEmptySignature + _T("\n");
	if (uS3AuthV4SendBufIndex > 0)
	{
		Hash.CreateHash(BCRYPT_SHA256_ALGORITHM);
		Hash.AddHashData(S3AuthV4SendBuf.GetData() + State.Ref->uS3AuthV4ChunkMetadataOffset, uS3AuthV4SendBufIndex);
		Hash.GetHashData(CurrentHash);
		sStringToSign += BinaryToHexString(CurrentHash);
	}
	else
		sStringToSign += sEmptySignature;

	// sign the sStringToSign
	CAnsiString StringToSign(sStringToSign);
	StringToSign.SetBufSize((DWORD)strlen(StringToSign));
	CBuffer Signature;
	Hash.CreateHash(BCRYPT_SHA256_ALGORITHM, S3SigningKey);
	Hash.AddHashData(StringToSign);
	Hash.GetHashData(Signature);
	sSignature = BinaryToHexString(Signature);
	sPreviousSignature = sSignature;

	CStringA sChunkPrefix(BuildV4ChunkMetadata(uS3AuthV4SendBufIndex, sSignature));
	CopyMemory((void *)S3AuthV4SendBuf.GetData(), (LPCSTR)sChunkPrefix, sChunkPrefix.GetLength());
	S3AuthV4SendBuf.SetAt(State.Ref->uS3AuthV4ChunkMetadataOffset + uS3AuthV4SendBufIndex, '\r');
	S3AuthV4SendBuf.SetAt(State.Ref->uS3AuthV4ChunkMetadataOffset + uS3AuthV4SendBufIndex + 1, '\n');
}

// SendRequestInternal
// complete the request and send it, and get the response
// adds the following headers:
//	if dwDatalen != 0: content-length: <dwDataLen>
CECSConnection::S3_ERROR CECSConnection::SendRequestInternal(
	LPCTSTR pszMethod,
	LPCTSTR pszResource,
	const void *pData,
	DWORD dwDataLen,
	CBuffer& RetData,
	list<HEADER_REQ> *pHeaderReq,
	DWORD dwReceivedDataHint,				// an approximate size of the received data to aid in allocation
	DWORD dwBufOffset,						// reserve dwBufOffset bytes at the start of the buffer
	bool *pbGotServerResponse,				// if this is set, that means it got to the server. don't retry it. Also, don't call the disconnected callback
	STREAM_CONTEXT *pStreamSend,			// if supplied, don't use pData/dwDataLen. wait on this queue for data to send
	STREAM_CONTEXT *pStreamReceive,			// is supplied, don't return data through RetData, push it on this queue
	ULONGLONG ullTotalLen)					// for StreamSend the total length of the transfer, for StreamReceive, the starting offset into the object
{
	CStateRef State(this);
	S3_ERROR Error;
	// use const version of pStreamSend to use "read" locks instead of "write" locks when only reading is being done
	const STREAM_CONTEXT *pConstStreamSend = (const STREAM_CONTEXT *)pStreamSend;
	CString sHostHeader(CString(GetCurrentServerIP()) + _T(":") + FmtNum(Port));
	bool bS3AuthV4 = false;
	UINT uS3AuthV4ChunkSize(DefaultS3AuthV4ChunkSize);
	ULONGLONG ullTotalPayloadLen(ullTotalLen);
	CBuffer S3AuthV4SendBuf;
	UINT uS3AuthV4SendBufIndex = 0;
	CString sPreviousSignature, sEmptySignature;
	CBuffer S3SigningKey;
	CCngAES_GCM Hash;
	SYSTEMTIME stRequestTime;
	FILETIME ftRequestTime;
	bool bGotRequestTime = false;
	DWORD dwMaxStreamQueueSizeRecv = 1000;	// give it something. get a better number if pStreamReceive used

	try
	{
		// determine the max size of the receive queue
		if (pStreamReceive != nullptr)
		{
			// assume that each entry in the stream receive queue, coming from WinHttp, is 8k
			dwMaxStreamQueueSizeRecv = DefaultMaxStreamQueueSizeRecv;
			if (dwMaxStreamQueueSizeRecv == 0)
				dwMaxStreamQueueSizeRecv = 1000;
		}
		bS3AuthV4 = IfS3v4(&uS3AuthV4ChunkSize);

		// if V4, initialize the send buffer
		if (bS3AuthV4)
		{
			CBuffer EmptySignature;
			Hash.CreateHash(BCRYPT_SHA256_ALGORITHM);
			Hash.AddHashData((const BYTE *)"", 0);
			Hash.GetHashData(EmptySignature);
			sEmptySignature = BinaryToHexString(EmptySignature);
			if (sS3Region.IsEmpty())
				sS3Region = _T("ECS");
			// init the chunk variables
			if (uS3AuthV4ChunkSize < 0x2000)
				uS3AuthV4ChunkSize = 0x2000;			// don't let it go less than 8k
			UINT uDigits = 0;
			for (UINT uChunkSize = uS3AuthV4ChunkSize; uChunkSize != 0; uChunkSize >>= 4)
				++uDigits;
			State.Ref->sChunkMetadataFormatA.Format("%%0%dx;chunk-signature=%%S\r\n", uDigits);
			CStringA sTestSig = BuildV4ChunkMetadata(uS3AuthV4ChunkSize, sEmptySignature);
			State.Ref->uS3AuthV4ChunkMetadataOffset = sTestSig.GetLength();
			State.Ref->uS3AuthV4ChunkMetadataSize = State.Ref->uS3AuthV4ChunkMetadataOffset + 2;
			S3AuthV4SendBuf.SetBufSize(uS3AuthV4ChunkSize + State.Ref->uS3AuthV4ChunkMetadataSize);
		}

		// fixup the host header line (if it exists)
		{
			map<CString, HEADER_STRUCT>::iterator itHeader = State.Ref->Headers.find(_T("host"));
			if (itHeader != State.Ref->Headers.end())
				itHeader->second.sContents = sHostHeader;
			// get date header if it exists
			itHeader = State.Ref->Headers.find(_T("date"));
			if (itHeader != State.Ref->Headers.end())
			{
				ftRequestTime = ParseCanonicalTime(itHeader->second.sContents);
				FileTimeToSystemTime(&ftRequestTime, &stRequestTime);
				bGotRequestTime = true;
			}
			itHeader = State.Ref->Headers.find(_T("x-amz-date"));
			if (itHeader != State.Ref->Headers.end())
			{
				if (ParseISO8601Date(itHeader->second.sContents, ftRequestTime) == ERROR_SUCCESS)
				{
					FileTimeToSystemTime(&ftRequestTime, &stRequestTime);
					bGotRequestTime = true;
				}
			}
		}
		if (!bGotRequestTime)
		{
			GetSystemTimeAsFileTime(&ftRequestTime);
			FileTimeToSystemTime(&ftRequestTime, &stRequestTime);
		}
		State.Ref->dwSecureError = 0;
		if (pbGotServerResponse != nullptr)
			*pbGotServerResponse = false;
		if (pConstStreamSend != nullptr)
		{
			if (bS3AuthV4)
			{
				// if v4auth, gotta take the metadata into account
				ULONGLONG ullChunks = ullTotalLen / uS3AuthV4ChunkSize;
				if (ullTotalLen % uS3AuthV4ChunkSize != 0)
					++ullChunks;
				++ullChunks;				// add in one more because of the final chunk of 0 data that must be sent
				ullTotalLen += ullChunks * State.Ref->uS3AuthV4ChunkMetadataSize;
			}
			AddHeader(_T("content-length"), FmtNum(ullTotalLen));
			if (!pConstStreamSend->bMultiPart)
				AddHeader(_T("content-type"), _T("application/octet-stream"), false);
		}
		else if (dwDataLen != 0)
		{
			AddHeader(_T("content-length"), FmtNum(dwDataLen));
			AddHeader(_T("content-type"), _T("application/octet-stream"), false);			// set to a generic value, but if there is already a header there, leave it
		}
		// now construct the signature for the header
		CString sMethod(pszMethod), sResource(pszResource);
		CString sSignature;
		if (!bS3AuthV4)
			sSignature = signRequestS3v2(sSecret, sMethod, sResource, State.Ref->Headers);
		else
			sSignature = signRequestS3v4(sSecret, sMethod, sResource,
				State.Ref->Headers, pData, dwDataLen, pConstStreamSend != nullptr ? E_S3_V4_PAYLOAD::Chunked : E_S3_V4_PAYLOAD::Signed, ullTotalLen, ullTotalPayloadLen, S3SigningKey,
				sPreviousSignature, stRequestTime);

		DWORD dwError = InitSession();
		if (dwError != ERROR_SUCCESS)
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, dwError);
		// close any current request
		State.Ref->CloseRequest();
		// Create an HTTP request handle.
		State.Ref->hRequest = WinHttpOpenRequest(State.Ref->Session.pValue->hConnect, TO_UNICODE(pszMethod), TO_UNICODE(pszResource),
			nullptr, WINHTTP_NO_REFERER, 
			WINHTTP_DEFAULT_ACCEPT_TYPES, 
			(bSSL ? WINHTTP_FLAG_SECURE : 0));
		State.Ref->bCallbackRegistered = false;
		State.Ref->CallbackContext.pbCallbackRegistered = &State.Ref->bCallbackRegistered;
		if (!State.Ref->hRequest.IfOpen())
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
		if (WinHttpSetStatusCallback(State.Ref->hRequest, CECSConnection::HttpStatusCallback, ECS_CONN_WINHTTP_CALLBACK_FLAGS, NULL) == WINHTTP_INVALID_STATUS_CALLBACK)
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
		DWORD_PTR dwpContext = (DWORD_PTR)&State.Ref->CallbackContext;
		if (!WinHttpSetOption(State.Ref->hRequest, WINHTTP_OPTION_CONTEXT_VALUE, &dwpContext, sizeof(dwpContext)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
		State.Ref->bCallbackRegistered = true;
		DWORD dwAutoLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
		if (!WinHttpSetOption(State.Ref->hRequest, WINHTTP_OPTION_AUTOLOGON_POLICY, &dwAutoLogon, sizeof(dwAutoLogon)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
		DWORD dwSecurityFlags = dwHttpSecurityFlags | State.Ref->dwSecurityFlagsAdd & ~State.Ref->dwSecurityFlagsSub;
		if (!WinHttpSetOption(State.Ref->hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSecurityFlags, sizeof(dwSecurityFlags)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
		SetTimeouts(State.Ref->hRequest);
		CString sHeaders;
		map<CString,HEADER_STRUCT>::const_iterator iter;
		for (iter=State.Ref->Headers.begin() ; iter != State.Ref->Headers.end() ; ++iter)
		{
			sHeaders += iter->second.sHeader + _T(":") + iter->second.sContents + _T("\r\n");
		}
		if (!State.Ref->bS3Admin)
			sHeaders += _T("Authorization: ") + sSignature + _T("\r\n");
		DWORD dwIndex;
		CBuffer RetBuf;
		bool bUploadThrottle;
		bool bDownloadThrottle;
		bool bAuthFailure = false;
		IfThrottle(&bDownloadThrottle, &bUploadThrottle);
		// loop here in case the request needs to be resent because the proxy server needs authorization
		for (UINT iRetryAuth=0 ; iRetryAuth<3 ; iRetryAuth++)
		{
			// send authorization, if we've already determined it was necessary
			if ((State.Ref->dwProxyAuthScheme != 0) && !sProxyUser.IsEmpty())
			{
				// if passport, we must use WinHttpSetOption instead of WinHttpSetCredentials
				if (State.Ref->dwProxyAuthScheme == WINHTTP_AUTH_SCHEME_PASSPORT)
				{
					if (!WinHttpSetOption(State.Ref->hRequest, WINHTTP_OPTION_PROXY_USERNAME, (void *)(LPCTSTR)sProxyUser, sProxyUser.GetLength()))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
					if (!WinHttpSetOption(State.Ref->hRequest, WINHTTP_OPTION_PROXY_PASSWORD, (void *)(LPCTSTR)sProxyPassword, sProxyPassword.GetLength()))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
				}
				else
				{
					if (!WinHttpSetCredentials(State.Ref->hRequest, WINHTTP_AUTH_TARGET_PROXY, State.Ref->dwProxyAuthScheme, TO_UNICODE(sProxyUser), TO_UNICODE(sProxyPassword), nullptr))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
				}
			}
			if ((State.Ref->dwAuthScheme != 0) && !State.Ref->sHTTPUser.IsEmpty())
			{
				if (!WinHttpSetCredentials(State.Ref->hRequest, WINHTTP_AUTH_TARGET_SERVER, State.Ref->dwAuthScheme, TO_UNICODE(State.Ref->sHTTPUser), TO_UNICODE(State.Ref->sHTTPPassword), nullptr))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
			}
			{
				DWORD dwDataPartLen = dwDataLen;
				ULONGLONG ullCurDataSent = 0ULL;
				if ((dwMaxWriteRequest > 0) && (dwDataPartLen > dwMaxWriteRequest))
					dwDataPartLen = dwMaxWriteRequest;
				if (bUploadThrottle && (pConstStreamSend == nullptr))
					dwDataPartLen = 0;							// just start the request but send the data below where it is controlled by the throttle
				for (;;)
				{
					PrepareCmd();
					bool bSendReturn;
					if (pConstStreamSend == nullptr)
						bSendReturn = WinHttpSendRequest(State.Ref->hRequest, TO_UNICODE((LPCTSTR)sHeaders), (DWORD)sHeaders.GetLength(), const_cast<void *>(pData), dwDataPartLen, dwDataLen, (DWORD_PTR)&State.Ref->CallbackContext) != FALSE;
					else
						bSendReturn = WinHttpSendRequest(State.Ref->hRequest, TO_UNICODE((LPCTSTR)sHeaders), (DWORD)sHeaders.GetLength(), WINHTTP_NO_REQUEST_DATA, 0, (ullTotalLen > ULONG_MAX) ? WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH : (DWORD)ullTotalLen, (DWORD_PTR)&State.Ref->CallbackContext) != FALSE;
					if (!bSendReturn)
					{
						dwError = GetLastError();
						CleanupCmd();
						if (dwError == ERROR_WINHTTP_RESEND_REQUEST)
							continue;
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, dwError);
					}
					if (!WaitComplete(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE))
					{
						if (State.Ref->CallbackContext.Result.dwError == ERROR_WINHTTP_RESEND_REQUEST)
							continue;
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, State.Ref->CallbackContext.Result.dwError);
					}
					break;
				}
				if (pConstStreamSend != nullptr)
				{
					dwDataPartLen = 0;							// didn't send any data yet
					ullCurDataSent = 0ULL;
				}
				// if performance monitor is attached, count the bytes sent
				{
					{
						CSimpleRWLockAcquire lock(&rwlGlobalPerf);				// read lock
						for (list<GLOBAL_PERF_POINTERS>::const_iterator it = GlobalPerfList.begin(); it != GlobalPerfList.end(); ++it)
						{
							if (it->pullBytesSent != nullptr)
								(void)InterlockedExchangeAdd64((LONG64 *)it->pullBytesSent, (LONG64)sHeaders.GetLength() + dwDataPartLen);
						}
					}
					if (pullPerfBytesSent != nullptr)
						(void)InterlockedExchangeAdd64((LONG64 *)pullPerfBytesSent, (LONG64)sHeaders.GetLength() + dwDataPartLen);
				}
				ullCurDataSent += (ULONGLONG)dwDataPartLen;
				CSharedQueueEvent MsgEvent;		// event that a new message arrived on pStreamSend
				if (pConstStreamSend != nullptr)
				{
					MsgEvent.Link(&pStreamSend->StreamData);					// link the queue to the event
					MsgEvent.DisableAllTriggerEvents();
					MsgEvent.EnableTriggerEvents(TRIGGEREVENTS_PUSH | TRIGGEREVENTS_INSERTAT);
					MsgEvent.SetAllEvents();
					MsgEvent.Enable();
				}
				// stream data available
				bool bStreamBufAvailable = false;
				ULONGLONG ullTotalBytesSent = 0;
				for (;;)
				{
					bool bWriteDataSuccess;
					bool bDoWriteData = true;
					State.Ref->CallbackContext.dwBytesWritten = 0;
					if (pConstStreamSend == nullptr)
					{
						if (ullCurDataSent >= (ULONGLONG)dwDataLen)
							break;
						dwDataPartLen = dwDataLen - (DWORD)ullCurDataSent;
						if ((dwMaxWriteRequest > 0) && (dwDataPartLen > dwMaxWriteRequest))
							dwDataPartLen = dwMaxWriteRequest;
						PrepareCmd();
						bWriteDataSuccess = WinHttpWriteData(State.Ref->hRequest, (BYTE *)pData + (DWORD)ullCurDataSent, dwDataPartLen, nullptr) != FALSE;
					}
					else
					{
						if (!bStreamBufAvailable)
						{
							// need more data
//							DumpDebugFileFmt(_T(__FILE__), __LINE__, _T("StreamSend: %s, %d"), pszResource, pConstStreamSend->StreamData.GetCount());
							// if queue is empty, wait for something to happen
							if (pConstStreamSend->StreamData.empty())
								if (!WaitStreamSend(pStreamSend, MsgEvent))
									throw CS3ErrorInfo(_T(__FILE__), __LINE__, WAIT_TIMEOUT);
							if (pConstStreamSend->StreamData.empty())
								throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_NO_DATA_DETECTED);
							dwDataPartLen = 0;
							ullCurDataSent = 0ULL;
							bStreamBufAvailable = true;
						}
						if (!pConstStreamSend->StreamData.front().Data.IsEmpty())
						{
							if (ullCurDataSent >= (ULONGLONG)pConstStreamSend->StreamData.front().Data.GetBufSize())
							{
								bWriteDataSuccess = true;				// empty data, don't write it but say it was successful
								bDoWriteData = false;
							}
							else
							{
								dwDataPartLen = pConstStreamSend->StreamData.front().Data.GetBufSize() - (DWORD)ullCurDataSent;
								if (bS3AuthV4)
								{
									UINT uFreeSpace = uS3AuthV4ChunkSize - uS3AuthV4SendBufIndex;
									if (uFreeSpace < dwDataPartLen)
										dwDataPartLen = uFreeSpace;
									if (dwDataPartLen > 0)
									{
										memcpy_s(S3AuthV4SendBuf.GetData() + State.Ref->uS3AuthV4ChunkMetadataOffset + uS3AuthV4SendBufIndex,
											S3AuthV4SendBuf.GetBufSize() - State.Ref->uS3AuthV4ChunkMetadataOffset - uS3AuthV4SendBufIndex,
											pConstStreamSend->StreamData.front().Data.GetData() + ullCurDataSent,
											dwDataPartLen);
										uS3AuthV4SendBufIndex += dwDataPartLen;
										ullCurDataSent += dwDataPartLen;
									}
									if (uS3AuthV4ChunkSize == uS3AuthV4SendBufIndex)
									{
										// got a full buffer. time to send it
										// prepare string to sign
										CreateS3V4ChunkMetadata(sPreviousSignature, S3AuthV4SendBuf, uS3AuthV4SendBufIndex, sEmptySignature, S3SigningKey, stRequestTime);

										PrepareCmd();
										bWriteDataSuccess = WinHttpWriteData(State.Ref->hRequest, S3AuthV4SendBuf.GetData(), uS3AuthV4SendBufIndex + State.Ref->uS3AuthV4ChunkMetadataSize, nullptr) != FALSE;
										bDoWriteData = true;
									}
									else
									{
										bWriteDataSuccess = true;				// got to get more data, don't write it but say it was successful
										bDoWriteData = false;
									}
								}
								else
								{
									if ((dwMaxWriteRequest > 0) && (dwDataPartLen > dwMaxWriteRequest))
										dwDataPartLen = dwMaxWriteRequest;
									PrepareCmd();
									bWriteDataSuccess = WinHttpWriteData(State.Ref->hRequest, pConstStreamSend->StreamData.front().Data.GetData() + ullCurDataSent, dwDataPartLen, nullptr) != FALSE;
								}
							}
						}
						else
						{
							bWriteDataSuccess = true;				// empty data, don't write it but say it was successful
							bDoWriteData = false;
						}
					}
					if (!bWriteDataSuccess)
					{
						dwError = GetLastError();
//						DumpDebugFileFmt(_T(__FILE__), __LINE__, _T("StreamSend: %s, ERROR %s"), pszResource, NTLT(dwError));
						CleanupCmd();
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, dwError);
					}
					int iDataSent = 0;
					if (bDoWriteData)
					{
						if (!WaitComplete(WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE))
							throw CS3ErrorInfo(_T(__FILE__), __LINE__, State.Ref->CallbackContext.Result.dwError);
						{
							{
								CSimpleRWLockAcquire lock(&rwlGlobalPerf);				// read lock
								for (list<GLOBAL_PERF_POINTERS>::const_iterator it = GlobalPerfList.begin(); it != GlobalPerfList.end(); ++it)
								{
									if (it->pullBytesSent != nullptr)
										(void)InterlockedExchangeAdd64((LONG64 *)it->pullBytesSent, State.Ref->CallbackContext.dwBytesWritten);
								}
							}
							if (pullPerfBytesSent != nullptr)
								(void)InterlockedExchangeAdd64((LONG64 *)pullPerfBytesSent, State.Ref->CallbackContext.dwBytesWritten);
						}

						if (bS3AuthV4)
							uS3AuthV4SendBufIndex = 0;
						else
							ullCurDataSent += (ULONGLONG)State.Ref->CallbackContext.dwBytesWritten;
						iDataSent = (int)State.Ref->CallbackContext.dwBytesWritten;
						ullTotalBytesSent += iDataSent;
					}
					bool bLast = false;
					if (pConstStreamSend == nullptr)
						ASSERT(State.Ref->CallbackContext.dwBytesWritten == dwDataPartLen);
					else
					{
						if (pConstStreamSend->UpdateProgressCB != nullptr)
						{
							pStreamSend->iAccProgress += iDataSent;
							pConstStreamSend->UpdateProgressCB(iDataSent, pConstStreamSend->pContext);
						}
						if (ullCurDataSent >= (ULONGLONG)pConstStreamSend->StreamData.front().Data.GetBufSize())
						{
							CRWLockAcquire lockQueue(&pConstStreamSend->StreamData.GetLock(), true);			// write lock
							bLast = pConstStreamSend->StreamData.front().bLast;
							pStreamSend->StreamData.pop_front();
							bStreamBufAvailable = false;
						}
					}
					if (bLast)
					{
						if (bS3AuthV4)
						{
							// send any remaining data
							if (uS3AuthV4SendBufIndex > 0)
							{
								// prepare string to sign
								CreateS3V4ChunkMetadata(sPreviousSignature, S3AuthV4SendBuf, uS3AuthV4SendBufIndex, sEmptySignature, S3SigningKey, stRequestTime);
								PrepareCmd();
								bWriteDataSuccess = WinHttpWriteData(State.Ref->hRequest, S3AuthV4SendBuf.GetData(), uS3AuthV4SendBufIndex + State.Ref->uS3AuthV4ChunkMetadataSize, nullptr) != FALSE;
								if (!bWriteDataSuccess)
								{
									dwError = GetLastError();
									CleanupCmd();
									throw CS3ErrorInfo(_T(__FILE__), __LINE__, dwError);
								}
								if (!WaitComplete(WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE))
									throw CS3ErrorInfo(_T(__FILE__), __LINE__, State.Ref->CallbackContext.Result.dwError);
								if (pConstStreamSend->UpdateProgressCB != nullptr)
								{
									pStreamSend->iAccProgress += uS3AuthV4SendBufIndex + State.Ref->uS3AuthV4ChunkMetadataSize;
									pConstStreamSend->UpdateProgressCB(uS3AuthV4SendBufIndex + State.Ref->uS3AuthV4ChunkMetadataSize, pConstStreamSend->pContext);
								}
								ullTotalBytesSent += State.Ref->CallbackContext.dwBytesWritten;
							}
							// now send the last packet
							uS3AuthV4SendBufIndex = 0;
							CreateS3V4ChunkMetadata(sPreviousSignature, S3AuthV4SendBuf, uS3AuthV4SendBufIndex, sEmptySignature, S3SigningKey, stRequestTime);
							PrepareCmd();
							bWriteDataSuccess = WinHttpWriteData(State.Ref->hRequest, S3AuthV4SendBuf.GetData(), uS3AuthV4SendBufIndex + State.Ref->uS3AuthV4ChunkMetadataSize, nullptr) != FALSE;
							if (!bWriteDataSuccess)
							{
								dwError = GetLastError();
								CleanupCmd();
								throw CS3ErrorInfo(_T(__FILE__), __LINE__, dwError);
							}
							if (!WaitComplete(WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE))
								throw CS3ErrorInfo(_T(__FILE__), __LINE__, State.Ref->CallbackContext.Result.dwError);
							ullTotalBytesSent += State.Ref->CallbackContext.dwBytesWritten;
						}
						break;
					}
					// process download throttle
					// see if we've used up all of our quota for this interval
					if (bUploadThrottle)
					{
						map<CString, THROTTLE_REC>::iterator itThrottle;
						{
							CSingleLock lock(&csThrottleMap, true);
							itThrottle = ThrottleMap.find(sHost);
							if ((itThrottle != ThrottleMap.end()) && (itThrottle->second.Upload.iBytesSec != 0))
							{
								// okay, we need to throttle
								itThrottle->second.Upload.iBytesCurInterval -= (int)State.Ref->CallbackContext.dwBytesWritten;
							}
						}
						for (;;)
						{
							if (TestAbort())
								throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_OPERATION_ABORTED);
							// now check if the bytes for this interval is positive
							{
								CSingleLock lock(&csThrottleMap, true);
								itThrottle = ThrottleMap.find(sHost);
								if ((itThrottle == ThrottleMap.end()) || (itThrottle->second.Upload.iBytesSec == 0))
								{
									// the throttle disappeared! The user must have turned throttle off
									break;					// just go full speed
								}
								// okay, we need to throttle
								if (itThrottle->second.Upload.iBytesCurInterval >= 0)
									break;
							}
							// wait for the next time slot where hopefully we can receive more bytes
							dwError = WaitForSingleObject(State.Ref->evThrottle.m_hObject, SECONDS(2));
							switch (dwError)
							{
							case WAIT_FAILED:
								throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
							case WAIT_ABANDONED:
								throw CS3ErrorInfo(_T(__FILE__), __LINE__, dwError);
							default:
								ASSERT(false);
								break;
							case WAIT_TIMEOUT:
							case WAIT_OBJECT_0:
								break;
							}
						}
					}
				}
			}
			// wait for response
			PrepareCmd();
			if (!WinHttpReceiveResponse(State.Ref->hRequest, nullptr))
			{
				dwError = GetLastError();
				if (IfServerReached(dwError) && (pbGotServerResponse != nullptr))
					*pbGotServerResponse = true;
				CleanupCmd();
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, dwError);
			}
			if (!WaitComplete(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE))
			{
				if (IfServerReached(State.Ref->CallbackContext.Result.dwError) && (pbGotServerResponse != nullptr))
					*pbGotServerResponse = true;
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, State.Ref->CallbackContext.Result.dwError);
			}
			if (pbGotServerResponse != nullptr)
				*pbGotServerResponse = true;
			dwIndex = 0;
			if (!WinHttpQueryHeadersBuffer(State.Ref->hRequest, WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX, RetBuf, &dwIndex))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
			if (RetBuf.IsEmpty())
				Error.dwHttpError = 0;
			else
				Error.dwHttpError = _wtoi((LPCWSTR)RetBuf.GetData());
			// if proxy authentication error, figure out what to do here
			if ((Error.dwHttpError != HTTP_STATUS_DENIED) && (Error.dwHttpError != HTTP_STATUS_PROXY_AUTH_REQ))
				break;
			{
				bAuthFailure = true;
				DWORD dwSupportedSchemes;
				DWORD dwFirstScheme;
				DWORD dwTarget;
				// Obtain the supported and preferred schemes.
				if (!WinHttpQueryAuthSchemes(State.Ref->hRequest, &dwSupportedSchemes, &dwFirstScheme, &dwTarget))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
				// Set the credentials before re-sending the request.
				if (Error.dwHttpError == HTTP_STATUS_PROXY_AUTH_REQ)
					State.Ref->dwProxyAuthScheme = ChooseAuthScheme(dwSupportedSchemes);
				else
					State.Ref->dwAuthScheme = ChooseAuthScheme(dwSupportedSchemes);
			}
		}
		// if it required authorization and was successful
		// and we have secure logs disabled meaning this is a test connection
		// log an INFO message detailing the proxy connection
		if (bAuthFailure
			&& !Error.IfError()
			&& State.Ref->bDisableSecureLog
			&& (State.Ref->dwProxyAuthScheme != 0))
		{
			CString sProxyString(sProxy);
			if (dwProxyPort != 0)
				sProxyString += _T(":") + FmtNum(dwProxyPort);
			if (bSSL || (State.Ref->dwProxyAuthScheme != WINHTTP_AUTH_SCHEME_BASIC))
				LogMessage(_T(__FILE__), __LINE__, _T("Connection to %1 successfully authenticated through proxy server.\r\nProxy: %2\r\nAuthentication: %3\r\n"), ERROR_SUCCESS, (LPCTSTR)sHost, (LPCTSTR)sProxyString, (LPCTSTR)FormatAuthScheme());
			else
				LogMessage(_T(__FILE__), __LINE__, _T("Connection to %1 successfully authenticated through proxy server - NOT SECURE.\r\nWarning: This server is requesting your username and password to be sent in an insecure manner (basic authentication without a secure connection).\r\nProxy: %2\r\nAuthentication: %3\r\n"), ERROR_SUCCESS, (LPCTSTR)sHost, (LPCTSTR)sProxyString, (LPCTSTR)FormatAuthScheme());
		}
		if (pHeaderReq != nullptr)
		{
			if (pHeaderReq->empty())
			{
				dwIndex = 0;
				if (!WinHttpQueryHeadersBuffer(State.Ref->hRequest, WINHTTP_QUERY_RAW_HEADERS, WINHTTP_HEADER_NAME_BY_INDEX, RetBuf, &dwIndex))
				{
					dwError = GetLastError();
					LogMessage(_T(__FILE__), __LINE__, _T("WinHttpQueryHeadersBuffer Error"), dwError);
				}
				else
				{
					list<CString> AllHeaders;
#ifdef _UNICODE
					LoadNullTermStringArray((LPCWSTR)RetBuf.GetData(), AllHeaders);
#else
					CAnsiString HeadersStr;
					HeadersStr.Set((LPCWSTR)RetBuf.GetData(), RetBuf.GetBufSize()/sizeof(WCHAR));
					LoadNullTermStringArray((LPCSTR)HeadersStr.GetData(), AllHeaders);
#endif
					CString sHeader, sContent;
					for (list<CString>::const_iterator it = AllHeaders.begin(); it != AllHeaders.end(); ++it)
					{
						int iSep = it->Find(_T(':'));
						if (iSep >= 0)
						{
							sHeader = it->Left(iSep);
							sHeader.TrimRight();
							sHeader.TrimLeft();
							sContent = it->Mid(iSep + 1);
							sContent.TrimRight();
							sContent.TrimLeft();
							// see if we've already seen this label
							list<HEADER_REQ>::iterator itReq;
							for (itReq = pHeaderReq->begin(); itReq != pHeaderReq->end(); ++itReq)
								if (itReq->sHeader.CompareNoCase(sHeader) == 0)
									break;
							if (itReq == pHeaderReq->end())
							{
								HEADER_REQ Rec;
								Rec.sHeader = sHeader;
								Rec.ContentList.push_back(sContent);
								pHeaderReq->push_back(Rec);
							}
							else
								itReq->ContentList.push_back(sContent);
						}
					}
				}
			}
			else
			{
				for (list<HEADER_REQ>::iterator itReq = pHeaderReq->begin(); itReq != pHeaderReq->end(); ++itReq)
				{
					dwIndex = 0;
					for (;;)
					{
						if (!WinHttpQueryHeadersBuffer(State.Ref->hRequest, WINHTTP_QUERY_CUSTOM, itReq->sHeader, RetBuf, &dwIndex))
						{
							dwError = GetLastError();
							if (dwError == ERROR_WINHTTP_HEADER_NOT_FOUND)
								break;
							LogMessage(_T(__FILE__), __LINE__, _T("WinHttpQueryHeadersBuffer error: %1"), dwError, (LPCTSTR)(itReq->sHeader + _T(" #") + FmtNum(dwIndex)));
							break;
						}
						itReq->ContentList.emplace_back(FROM_UNICODE((LPCWSTR)RetBuf.GetData()));
					}
				}
			}
		}
		if (dwReceivedDataHint == 0)
			dwReceivedDataHint = GDReadWriteChunkMax;
		RetData.SetBufSize(dwReceivedDataHint + dwBufOffset + 1024);			// pre-allocate this buffer
		RetData.SetBufSize(0);
		DWORD dwLen = dwBufOffset;
		DWORD dwDownloaded = 0;
		DWORD dwSize = 0;
		STREAM_DATA_ENTRY RcvBuf;
		for (;;)
		{
			// Check for available data.
			PrepareCmd();
			if (!WinHttpQueryDataAvailable(State.Ref->hRequest, nullptr))
			{
				Error.dwError = GetLastError();
				CleanupCmd();
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			}
			if (!WaitComplete(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, State.Ref->CallbackContext.Result.dwError);
			dwSize = State.Ref->CallbackContext.dwReadLength;
			if (dwSize == 0)
			{
				if (pStreamReceive != nullptr)
				{
					if (Error.dwHttpError < 400)
					{
						RcvBuf.Data.Empty();
						RcvBuf.ullOffset = dwLen;
						RcvBuf.bLast = true;
						pStreamReceive->StreamData.push_back(RcvBuf,
							dwMaxStreamQueueSizeRecv,
							TestAbortStatic,
							this);
					}
				}
				break;
			}
			bool bReadReturn;
			PrepareCmd();
			if (pStreamReceive == nullptr)
			{
				// Allocate space for the buffer.
				// if the buffer must be enlarged (hopefully should never occur if Hint is accurate)
				// enlarge it by GDReadWriteChunkMax (a big number)
				while (RetData.GetAllocSize() < (dwLen + dwSize))
					RetData.SetBufSize(RetData.GetBufSize() + GDReadWriteChunkMax);
				RetData.SetBufSize(dwLen + dwSize);
				bReadReturn = WinHttpReadData(State.Ref->hRequest, RetData.GetData() + dwLen, dwSize, nullptr) != FALSE;
			}
			else
			{
				RcvBuf.Data.SetBufSize(dwSize);
				RcvBuf.bLast = false;
				bReadReturn = WinHttpReadData(State.Ref->hRequest, RcvBuf.Data.GetData(), dwSize, nullptr) != FALSE;
			}
			if (!bReadReturn)
			{
				Error.dwError = GetLastError();
				CleanupCmd();
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			}
			if (!WaitComplete(WINHTTP_CALLBACK_STATUS_READ_COMPLETE))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, State.Ref->CallbackContext.Result.dwError);
			dwDownloaded = State.Ref->CallbackContext.dwReadLength;
			{
				{
					CSimpleRWLockAcquire lock(&rwlGlobalPerf);				// read lock
					for (list<GLOBAL_PERF_POINTERS>::const_iterator it = GlobalPerfList.begin(); it != GlobalPerfList.end(); ++it)
					{
						if (it->pullBytesRcv != nullptr)
							(void)InterlockedExchangeAdd64((LONG64 *)it->pullBytesRcv, dwDownloaded);
					}
				}
				if (pullPerfBytesRcv != nullptr)
					(void)InterlockedExchangeAdd64((LONG64 *)pullPerfBytesRcv, dwDownloaded);
			}
			if (pStreamReceive != nullptr)
			{
				if (Error.dwHttpError < 400)
				{
					RcvBuf.Data.SetBufSize(dwDownloaded);
					RcvBuf.ullOffset = dwLen;
					pStreamReceive->ullTotalSize += (ULONGLONG)dwDownloaded;
					State.Ref->ullReadBytes += (ULONGLONG)dwDownloaded;
					pStreamReceive->StreamData.push_back(RcvBuf,
						dwMaxStreamQueueSizeRecv,
						TestAbortStatic,
						this);
				}
				else
					RetData = RcvBuf.Data;
			}
			else
			{
				RetData.SetBufSize(dwLen + dwDownloaded);
			}
			dwLen += dwDownloaded;
			// process download throttle
			// see if we've used up all of our quota for this interval
			if (bDownloadThrottle)
			{
				map<CString,THROTTLE_REC>::iterator itThrottle;
				{
					CSingleLock lock(&csThrottleMap, true);
					itThrottle = ThrottleMap.find(sHost);
					if ((itThrottle != ThrottleMap.end()) && (itThrottle->second.Download.iBytesSec != 0))
					{
						itThrottle->second.Download.iBytesCurInterval -= (int)dwDownloaded;
					}
				}
				for (;;)
				{
					if (TestAbort())
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_OPERATION_ABORTED);
					// now check if the bytes for this interval has become positive
					{
						CSingleLock lock(&csThrottleMap, true);
						itThrottle = ThrottleMap.find(sHost);
						if ((itThrottle == ThrottleMap.end()) || (itThrottle->second.Download.iBytesSec == 0))
						{
							// throttle has been canceled during this download
							// go full speed
							break;
						}
						if (itThrottle->second.Download.iBytesCurInterval >= 0)
							break;
					}
					// wait for the next time slot where hopefully we can receive more bytes
					dwError = WaitForSingleObject(State.Ref->evThrottle.m_hObject, SECONDS(2));
					switch (dwError)
					{
					case WAIT_FAILED:
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, GetLastError());
					case WAIT_ABANDONED:
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, dwError);
					default:
						ASSERT(false);
						break;
					case WAIT_TIMEOUT:
					case WAIT_OBJECT_0:
						break;
					}
				}
			}
		}
		// verify that RetData is NUL terminated
		// it will always be 8 bit characters
		DWORD dwRetDataLen = RetData.GetBufSize();
		RetData.SetBufSize(dwRetDataLen + 1);
		RetData.SetAt(dwRetDataLen, 0);
		RetData.SetBufSize(dwRetDataLen);			// this won't reallocate the buffer, so it will now always be NUL terminated
		// if error, get error detail from XML response
		if ((Error.dwHttpError >= 400) && !RetData.IsEmpty())
		{
			XML_ECS_ERROR_CONTEXT Context;
			Context.pError = &Error;
			if (!State.Ref->bS3Admin)
			{
				(void)ScanXml(&RetData, &Context, XmlS3ErrorCB);
			}
			else if (State.Ref->bS3Admin)
			{
				(void)ScanXml(&RetData, &Context, XmlECSAdminErrorCB);
			}
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		// if the progress has been incremented, cancel it out
		if ((pConstStreamSend != nullptr) && (pConstStreamSend->UpdateProgressCB != nullptr) && (pConstStreamSend->iAccProgress != 0))
		{
			pConstStreamSend->UpdateProgressCB(-pConstStreamSend->iAccProgress, pConstStreamSend->pContext);
			pStreamSend->iAccProgress = 0;
		}
		Error = E.Error;
		Error.sHostAddr = sHostHeader;
		State.Ref->CloseRequest(Error.dwError == ERROR_WINHTTP_SECURE_FAILURE);
		State.Ref->Session.pValue->bKillWhenDone = true;
		State.Ref->Session.ReleaseSession();
		if (Error.dwError == ERROR_WINHTTP_SECURE_FAILURE)
		{
			Error.dwSecureError = State.Ref->dwSecureError;
			GetCertInfo(Error.CertInfo);
		}
		WaitForCallbackDone(*State.Ref);
		return Error;
	}
	State.Ref->CloseRequest();
	State.Ref->Session.ReleaseSession();
	Error.sHostAddr = sHostHeader;
	// wait for the callback to be completely finished
	WaitForCallbackDone(*State.Ref);
	return Error;
}

void CECSConnection::WaitForCallbackDone(CECSConnectionState& State)
{
	// wait for lExitFlag to be 1
	while (InterlockedAdd(&State.CallbackContext.lExitFlag, 0) == 0)
	{
		WaitForSingleObject(State.CallbackContext.Event.evExit.m_hObject, 10);
	}
}

void CECSConnection::SetSecret(LPCTSTR pszSecret)		// set shared secret string in base64
{
	if (sSecret != pszSecret)
	{
		{
			CSimpleRWLockAcquire lockWrite(&lwrS3V4SigningKey, true);		// write lock

			// clear out the signing key so it will get recreated with the new secret
			GlobalS3V4SigningKey.Empty();
			ZeroFT(ftS3V4SigningKey);
		}
		sSecret = pszSecret;
		KillHostSessions();
	}
}

void CECSConnection::SetUser(LPCTSTR pszUser)			// set user ID
{
	if (sUser != pszUser)
	{
		sUser = pszUser;
		KillHostSessions();
	}
}

void CECSConnection::SetHost(LPCTSTR pszHost)				// set Host
{
	if (sHost != pszHost)
	{
		KillHostSessions();
		sHost = pszHost;
		KillHostSessions();
	}
}

void CECSConnection::SetIPList(const deque<CString>& IPListParam)
{
	CStateRef State(this);
	CSimpleRWLockAcquire lock(&rwlIPListHost, true);			// write lock because we might change it
	if (IPListHost != IPListParam)
	{
		IPListHost = IPListParam;
		State.Ref->iIPList = 0;
		lock.Unlock();
		KillHostSessions();
	}
}

void CECSConnection::GetIPList(deque<CString>& IPListParam)
{
	CSimpleRWLockAcquire lock(&rwlIPListHost, false);			// read lock
	IPListParam = IPListHost;
}

void CECSConnection::SetSSL(bool bSSLParam)
{
	INTERNET_PORT PortParam;
	if (bSSLParam)
		PortParam = INTERNET_DEFAULT_HTTPS_PORT;
	else
		PortParam = INTERNET_DEFAULT_HTTP_PORT;
	if ((bSSLParam != bSSL) || (Port != PortParam))
	{
		bSSL = bSSLParam;
		Port = PortParam;
		KillHostSessions();
	}
}

void CECSConnection::SetHostAuth(bool bAuthV4, UINT uS3AuthV4ChunkSize)
{
	CSimpleRWLockAcquire lockWrite(&lwrS3V4SigningKey, true);		// write lock

	bS3AuthV4 = bAuthV4;
	uS3AuthV4ChunkSize = uS3AuthV4ChunkSize;
}

bool CECSConnection::IfS3v4(UINT *puChunkSize) const
{
	CSimpleRWLockAcquire lockRead(&lwrS3V4SigningKey, false);				// get read lock
	if (puChunkSize != nullptr)
		*puChunkSize = uS3AuthV4ChunkSize;
	return bS3AuthV4;
}

void CECSConnection::SetRegion(LPCTSTR pszS3Region)
{
	sS3Region = pszS3Region;
}

void CECSConnection::SetS3KeyID(LPCTSTR pszS3KeyID)
{
	sS3KeyID = pszS3KeyID;
}

CString CECSConnection::GetS3KeyID()
{
	return sS3KeyID;
}

void CECSConnection::SetUserAgent(LPCTSTR pszUserAgent)
{
	sUserAgent = pszUserAgent;
}

void CECSConnection::SetPort(INTERNET_PORT PortParam)
{
	if (Port != PortParam)
	{
		Port = PortParam;
		KillHostSessions();
	}
}

void CECSConnection::SetProxy(bool bUseDefaultProxyParam, LPCTSTR pszProxy, DWORD dwPort, LPCTSTR pszProxyUser, LPCTSTR pszProxyPassword)
{
	CString sProxyParam(pszProxy);
	CString sProxyUserParam(pszProxyUser);
	CString sProxyPasswordParam(pszProxyPassword);
	if ((sProxy != sProxyParam)
		|| (dwProxyPort != dwPort)
		|| (sProxyUser != sProxyUserParam)
		|| (sProxyPassword != sProxyPasswordParam)
		|| (bUseDefaultProxyParam != bUseDefaultProxy))
	{
		bUseDefaultProxy = bUseDefaultProxyParam;
		sProxy = sProxyParam;
		dwProxyPort = dwPort;
		sProxyUser = sProxyUserParam;
		sProxyPassword = sProxyPasswordParam;
		KillHostSessions();
	}
}

void CECSConnection::SetTest(bool bTestParam)
{
	bTestConnection = bTestParam;
}

CECSConnection::S3_ERROR CECSConnection::Create(
	LPCTSTR pszPath,
	const void *pData,
	DWORD dwLen,
	const list<HEADER_STRUCT> *pMDList,
	const CBuffer *pChecksum,
	STREAM_CONTEXT *pStreamSend,
	ULONGLONG ullTotalLen,								// only used if stream send
	LPCTSTR pIfNoneMatch,								// creates if-none-match header. use "*" to prevent an object from overwriting an existing object
	list <HEADER_REQ> *pReq)							// if non-nullptr, return headers from result of PUT call
{
	S3_ERROR Error;
	CStateRef State(this);
	try
	{
		CBuffer RetData;

		InitHeader();
		if (pChecksum != nullptr && !pChecksum->IsEmpty())
			AddHeader(_T("Content-MD5"), pChecksum->EncodeBase64());
		if (pMDList != nullptr)
		{
			for (list<HEADER_STRUCT>::const_iterator itList = pMDList->begin(); itList != pMDList->end(); ++itList)
			{
				AddHeader(itList->sHeader, itList->sContents);
			}
		}
		if (pIfNoneMatch != nullptr)
			AddHeader(_T("if-none-match"), pIfNoneMatch);
		Error = SendRequest(_T("PUT"), (LPCTSTR)UriEncode(pszPath), pData, dwLen, RetData, pReq, 0, 0, pStreamSend, nullptr, ullTotalLen);
		if (Error.IfError())
			return Error;
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

CECSConnection::S3_ERROR CECSConnection::RenameS3(
	LPCTSTR pszOldPath,
	LPCTSTR pszNewPath,
	LPCTSTR pszVersionId,		// nonNULL: version ID to copy
	bool bCopy,
	const list<CECSConnection::HEADER_STRUCT> *pMDList,
	const list<CString> *pDeleteTagParam)
{
	CStateRef State(this);
	CECSConnection::S3_ERROR Error;
	CBuffer RetData;
	CString sOldPathS3(pszOldPath), sNewPathS3(pszNewPath);
	// first figure out if this an object or a folder
	// if it ends in '\' it's a folder
	if (sOldPathS3.IsEmpty() || sNewPathS3.IsEmpty())
		return ERROR_INVALID_NAME;

	if (sOldPathS3[sOldPathS3.GetLength() - 1] != _T('/'))
	{
		deque<ACL_ENTRY> Acls;
		// first get the ACL of the "before" object
		S3_ERROR AclAtError = ReadACL(sOldPathS3, Acls, pszVersionId);

		// object - copy to new path and then delete the old object
		InitHeader();
		list<CECSConnection::HEADER_STRUCT> MDList;
		if (pMDList != nullptr)
			MDList = *pMDList;
		// we are ALWAYS going to read the current headers and never do a CopyMD
		// this is because we don't know at this point if we need to do a multipart copy
		// and multipart copy does not seem to support copying of the MD
		// it is really complicated to do this again later on, so we'll just always do it here
		ULONGLONG ullObjectSize = 0ULL;
		{
			InitHeader();
			// read all the headers of the source file
			list<HEADER_REQ> Req;
			CString sHeadPath(UriEncode(sOldPathS3));
			if (pszVersionId != nullptr)
				sHeadPath += CString(_T("?versionId=")) + pszVersionId;
			Error = SendRequest(_T("HEAD"), UriEncode(sHeadPath), nullptr, 0, RetData, &Req);
			if (Error.IfError())
				return Error;
			for (list<HEADER_REQ>::const_iterator it = Req.begin(); it != Req.end(); ++it)
			{
				if (!it->ContentList.empty())
				{
					if (it->sHeader.CompareNoCase(_T("Content-Length")) == 0)
					{
						ullObjectSize = _tcstoui64(it->ContentList.front(), nullptr, 10);
					}
					bool bFound = false;
					for (list<HEADER_STRUCT>::const_iterator itMD = MDList.begin(); itMD != MDList.end(); ++itMD)
					{
						if (it->sHeader.CompareNoCase(itMD->sHeader) == 0)
						{
							bFound = true;
							break;
						}
					}
					if (!bFound)
					{
						WriteMetadataEntry(MDList, it->sHeader, it->ContentList.front());
					}
				}
			}
			if (!MDList.empty() && pDeleteTagParam != nullptr && !pDeleteTagParam->empty())
			{
				for (list<HEADER_STRUCT>::const_iterator itList = MDList.begin(); itList != MDList.end(); )
				{
					bool bDeleted = false;
					for (list<CString>::const_iterator itDel = pDeleteTagParam->begin(); itDel != pDeleteTagParam->end(); ++itDel)
					{
						if (*itDel == itList->sHeader)
						{
							bDeleted = true;
							break;
						}
					}
					if (bDeleted)
						itList = MDList.erase(itList);
					else
						++itList;
				}
			}
		}
		Error = CopyS3(sOldPathS3, sNewPathS3, pszVersionId, false, ullObjectSize, &MDList);
		if (Error.IfError())
			return Error;
		if (!AclAtError.IfError())
			(void)WriteACL(sNewPathS3, Acls);		// if this fails, don't fail the rename since it is mostly done

		if (!bCopy)
		{
			InitHeader();
			Error = SendRequest(_T("DELETE"), UriEncode(sOldPathS3), nullptr, 0, RetData);
		}
	}
	else
	{
		// rename folder
		return ERROR_INVALID_FUNCTION;
	}
	return Error;
}

CECSConnection::S3_ERROR CECSConnection::DeleteS3(LPCTSTR pszPath, LPCTSTR pszVersionId)
{
	CStateRef State(this);
	S3_ERROR Error;
	try
	{
		CString sPath(pszPath);
		if (sPath.IsEmpty())
			return S3_ERROR(ERROR_INVALID_NAME);
		CBuffer RetData;
		// if folder, do bulk delete
		if (sPath[sPath.GetLength() - 1] == _T('/'))
		{
			list<CECSConnection::S3_DELETE_ENTRY> PathList;
			PathList.push_back(CECSConnection::S3_DELETE_ENTRY(pszPath, pszVersionId));
			return DeleteS3(PathList);
		}
		InitHeader();
		CString sResource(UriEncode(sPath));
		if (pszVersionId != nullptr)
			sResource += CString(_T("?versionId=")) + pszVersionId;
		Error = SendRequest(_T("DELETE"), sResource, nullptr, 0, RetData);
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

CECSConnection::S3_ERROR CECSConnection::DeleteS3(const list<CECSConnection::S3_DELETE_ENTRY>& PathList)
{
	CStateRef State(this);
	S3_ERROR Error;
	try
	{
		State.Ref->S3DeletePathList.clear();
		DeleteS3Internal(PathList);
		DeleteS3Send();								// delete any straggelers
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

void CECSConnection::DeleteS3Internal(const list<CECSConnection::S3_DELETE_ENTRY>& PathList)
{
	CStateRef State(this);
	S3_ERROR Error;

	list<CECSConnection::S3_DELETE_ENTRY>::const_iterator itPath;
	for (itPath = PathList.begin(); itPath != PathList.end(); ++itPath)
	{
		if (itPath->sKey[itPath->sKey.GetLength() - 1] == _T('/'))
		{
			list<CECSConnection::S3_DELETE_ENTRY> PathListAdd;
			DirEntryList_t DirList;
			Error = DirListing(itPath->sKey, DirList, false);
			if (Error.IfError() && !Error.IfNotFound())
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			if (!Error.IfError())
			{
				for (DirEntryList_t::const_iterator itDir = DirList.begin(); itDir != DirList.end(); ++itDir)
				{
					PathListAdd.push_back(S3_DELETE_ENTRY(itPath->sKey + itDir->sName + (itDir->bDir ? _T("/") : _T(""))));
				}
				DeleteS3Internal(PathListAdd);
			}
		}
		State.Ref->S3DeletePathList.emplace_back(S3_DELETE_ENTRY(itPath->sKey, itPath->sVersionId));
		if (State.Ref->S3DeletePathList.size() > MaxS3DeleteObjects)
			DeleteS3Send();
	}
}

const WCHAR * const XML_DELETES3_ERROR = L"//DeleteResult/Error";
const WCHAR * const XML_DELETES3_ERROR_CODE = L"//DeleteResult/Error/Code";
const WCHAR * const XML_DELETES3_ERROR_MESSAGE = L"//DeleteResult/Error/Message";
const WCHAR * const XML_DELETES3_ERROR_REQUESTID = L"//DeleteResult/Error/RequestId";
const WCHAR * const XML_DELETES3_ERROR_HOSTID = L"//DeleteResult/Error/HostId";
const WCHAR * const XML_DELETES3_ERROR_KEY = L"//DeleteResult/Error/Key";

struct XML_DELETES3_ENTRY
{
	CString sKey;
	E_S3_ERROR_TYPE Error;
	CString sCode;
	CString sMessage;
	CString sRequestId;
	CString sHostId;

	XML_DELETES3_ENTRY()
		: Error(S3_ERROR_UNKNOWN)
	{}

	void Clear(void)
	{
		sKey.Empty();
		Error = S3_ERROR_UNKNOWN;
		sCode.Empty();
		sMessage.Empty();
		sRequestId.Empty();
		sHostId.Empty();
	}
};

struct XML_DELETES3_CONTEXT
{
	list<XML_DELETES3_ENTRY> ErrorList;
	XML_DELETES3_ENTRY Rec;
};

HRESULT XmlDeleteS3CB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_DELETES3_CONTEXT *pInfo = (XML_DELETES3_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (psValue != nullptr)
		{
			if (sXmlPath.CompareNoCase(XML_DELETES3_ERROR_CODE) == 0)
			{
				pInfo->Rec.sCode = FROM_UNICODE(*psValue);
				pInfo->Rec.Error = S3TranslateError(FROM_UNICODE(*psValue));
			}
			else if (sXmlPath.CompareNoCase(XML_DELETES3_ERROR_MESSAGE) == 0)
			{
				pInfo->Rec.sMessage = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_DELETES3_ERROR_REQUESTID) == 0)
			{
				pInfo->Rec.sRequestId = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_DELETES3_ERROR_HOSTID) == 0)
			{
				pInfo->Rec.sHostId = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_DELETES3_ERROR_KEY) == 0)
			{
				pInfo->Rec.sKey = FROM_UNICODE(*psValue);
			}
		}
		break;
	case XmlNodeType_Element:
		if (sXmlPath.CompareNoCase(XML_DELETES3_ERROR) == 0)
		{
			pInfo->Rec.Clear();
		}
		break;
	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_DELETES3_ERROR) == 0)
		{
			pInfo->ErrorList.push_back(pInfo->Rec);
			pInfo->Rec.Clear();
		}
		break;

	default:
		break;
	}
	return 0;
}

void CECSConnection::DeleteS3Send()
{
	CStateRef State(this);
	if (State.Ref->S3DeletePathList.empty())
		return;
	CBufferStream *pBufStream = new CBufferStream;
	CComPtr<IStream> pOutFileStream = pBufStream;
	CComPtr<IXmlWriter> pWriter;
	S3_ERROR Error;
	CString sBucket, sKey;

	if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), (void**)&pWriter, nullptr)))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
	if (FAILED(pWriter->SetOutput(pOutFileStream)))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
	if (FAILED(pWriter->WriteStartDocument(XmlStandalone_Omit)))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
	if (FAILED(pWriter->WriteStartElement(nullptr, L"Delete", nullptr)))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

	if (FAILED(pWriter->WriteStartElement(nullptr, L"Quiet", nullptr)))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
	if (FAILED(pWriter->WriteString(L"true")))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
	if (FAILED(pWriter->WriteFullEndElement()))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

	// now put out the list of objects to delete
	State.Ref->S3DeletePathList.sort();
	State.Ref->S3DeletePathList.unique();
	UINT iCount = 0;								// keep a count. don't let it go over MaxS3DeleteObjects
	list<S3_DELETE_ENTRY>::iterator itPath;
	for (itPath = State.Ref->S3DeletePathList.end(); itPath != State.Ref->S3DeletePathList.begin();)
	{
		--itPath;															// run through the list from the end to the beginning
		// extract the bucket
		if (sBucket.IsEmpty())
		{
			int iSlash = itPath->sKey.Find(_T('/'), 1);
			if (iSlash < 0)
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_DATA);
			sBucket = itPath->sKey.Mid(1, iSlash - 1);
		}
		// verify we are always in the same bucket
		if (itPath->sKey.Find(_T('/') + sBucket + _T('/')) != 0)
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_DATA);
		sKey = itPath->sKey.Mid(sBucket.GetLength() + 2);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"Object", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"Key", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteString(TO_UNICODE(sKey))))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (!itPath->sVersionId.IsEmpty())
		{
			if (FAILED(pWriter->WriteStartElement(nullptr, L"VersionId", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(TO_UNICODE(itPath->sVersionId))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteFullEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		}
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		itPath = State.Ref->S3DeletePathList.erase(itPath);
		iCount++;
		if (iCount >= MaxS3DeleteObjects)
			break;
	}
	if (FAILED(pWriter->WriteFullEndElement()))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
	if (FAILED(pWriter->WriteEndDocument()))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
	if (FAILED(pWriter->Flush()))
		throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
	CString sXmlOut(pBufStream->GetXml());
#ifdef _UNICODE
	CAnsiString XmlUTF8(sXmlOut, CP_UTF8);
#else
	CAnsiString XmlUTF8(sXmlOut);
#endif
	XmlUTF8.SetBufSize((DWORD)strlen(XmlUTF8));
	{
		CBuffer RetData;
		InitHeader();
		CCngAES_GCM HashObj;
		HashObj.CreateHash(BCRYPT_MD5_ALGORITHM);
		HashObj.AddHashData(XmlUTF8);
		CBuffer MD5Hash;
		HashObj.GetHashData(MD5Hash);
		AddHeader(_T("Content-MD5"), MD5Hash.EncodeBase64());
		AddHeader(_T("Content-Type"), _T("application/xml"));
		Error = SendRequest(_T("POST"), _T("/") + sBucket + _T("/?delete"), XmlUTF8.GetData(), XmlUTF8.GetBufSize(), RetData);
		if (Error.IfError())
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);

		// now interpret the returned XML
		XML_DELETES3_CONTEXT Context;
		HRESULT hr = ScanXml(&RetData, &Context, XmlDeleteS3CB);
		if (FAILED(hr))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, hr);
		if (!Context.ErrorList.empty())
		{
			Error.sDetails.Empty();
			for (list<XML_DELETES3_ENTRY>::const_iterator itList = Context.ErrorList.begin(); itList != Context.ErrorList.end(); ++itList)
			{
				Error.sDetails += itList->sCode + _T(":") + itList->sMessage + _T(": ") + itList->sKey + _T("\n");
			}
			Error.dwHttpError = 500;
			Error.S3Error = Context.ErrorList.front().Error;
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
		}
	}
}

// CECSConnection::Read
// if lwOffset == 0 and dwLen == 0, read entire file
// if dwLen != 0, read 'dwLen' bytes starting from lwOffset
// if lwOffset != 0 and dwLen == 0, read from lwOffset to the end of the file
// dwBufOffset reserves that many bytes at the start of the buffer. The data is written starting at dwBufOffset
CECSConnection::S3_ERROR CECSConnection::Read(
	LPCTSTR pszPath,
	ULONGLONG lwLen,
	ULONGLONG lwOffset,
	CBuffer& RetData,
	DWORD dwBufOffset,
	STREAM_CONTEXT *pStreamReceive,
	list<HEADER_REQ> *pRcvHeaders,
	ULONGLONG *pullReturnedLength)
{
	const UINT READ_RETRY_MAX_TRIES = 5;
	CStateRef State(this);
	S3_ERROR Error;
	try
	{
		list<HEADER_REQ> HeaderReq;
		InitHeader();
		CString sRange;
		for (UINT iRetry = 0; iRetry < READ_RETRY_MAX_TRIES; iRetry++)
		{
			(void)State.Ref->Headers.erase(_T("range"));			// erase it in case of a retry
			HeaderReq.clear();
			if ((lwOffset != 0) || (lwLen != 0))
			{
				sRange = _T("bytes=") + FmtNum(lwOffset) + _T("-");
				if (lwLen != 0)
					sRange += FmtNum(lwOffset + lwLen - 1);
				AddHeader(_T("range"), sRange);
			}
			State.Ref->ullReadBytes = 0ULL;
			Error = SendRequest(_T("GET"), (LPCTSTR)UriEncode(pszPath), nullptr, 0, RetData, &HeaderReq,
				((pStreamReceive == nullptr) && (lwLen != 0)) ? ((DWORD)lwLen + 1024) : 0, dwBufOffset, nullptr, pStreamReceive, lwOffset);
			if (pRcvHeaders != nullptr)
				*pRcvHeaders = HeaderReq;
			if (!Error.IfError())
			{
				LONGLONG llStartRange, llExpectedLength, llTotalSize;
				ULONGLONG ullTotalLength;
				bool bWrongSize = false;

				if (pStreamReceive == nullptr)
					ullTotalLength = (ULONGLONG)RetData.GetBufSize();
				else
					ullTotalLength = State.Ref->ullReadBytes;
				if (pullReturnedLength != nullptr)
					*pullReturnedLength = ullTotalLength;
				// make sure we have all the bytes we asked for
				for (list<HEADER_REQ>::const_iterator it = HeaderReq.begin(); it != HeaderReq.end(); ++it)
				{
					if (it->sHeader == _T("Content-Length"))
					{
						if (it->ContentList.size() == 1)				// there should only be 1
						{
							llExpectedLength = _ttoi64(it->ContentList.front());
							bWrongSize = ullTotalLength != (ULONGLONG)llExpectedLength;
							if (bWrongSize)
								break;
						}
					}
					else if (it->sHeader == _T("Content-Range"))
					{
						if (it->ContentList.size() == 1)				// there should only be 1
						{
							if (_stscanf_s(it->ContentList.front(), _T("bytes %I64d-%I64d/%I64d"), &llStartRange, &llExpectedLength, &llTotalSize) == 3)
							{
								bWrongSize = ullTotalLength != (ULONGLONG)(llExpectedLength + 1 - llStartRange);
								if (bWrongSize)
									break;
							}
						}
					}
				}
				if (bWrongSize)
				{
					if ((pStreamReceive == nullptr) && (iRetry < (READ_RETRY_MAX_TRIES - 1)))
					{
//						DumpDebugFileFmt(_T(__FILE__), __LINE__, _T("CECSConnection::S3_ERROR CECSConnection::Read: %s - RETRY:%d"),
//							(LPCTSTR)sPath, iRetry);
						continue;
					}
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_BAD_LENGTH);
				}
			}
			break;
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

// XML for Versions response
/*
<ListVersionsResult xmlns="http://s3.amazonaws.com/doc/2006-03-01">
    <Name>bucket</Name>
    <Prefix>my</Prefix>
    <KeyMarker/>
    <VersionIdMarker/>
    <MaxKeys>5</MaxKeys>
    <IsTruncated>false</IsTruncated>
    <Version>
        <Key>my-image.jpg</Key>
        <VersionId>3/L4kqtJl40Nr8X8gdRQBpUMLUo</VersionId>
        <IsLatest>true</IsLatest>
         <LastModified>2009-10-12T17:50:30.000Z</LastModified>
        <ETag>&quot;fba9dede5f27731c9771645a39863328&quot;</ETag>
        <Size>434234</Size>
        <StorageClass>STANDARD</StorageClass>
        <Owner>
            <ID>75aa57f09aa0c8caeab4f8c24e99d10f8e7faeebf76c078efc7c6caea54ba06a</ID>
            <DisplayName>mtd@amazon.com</DisplayName>
        </Owner>
    </Version>
    <DeleteMarker>
        <Key>my-second-image.jpg</Key>
        <VersionId>03jpff543dhffds434rfdsFDN943fdsFkdmqnh892</VersionId>
        <IsLatest>true</IsLatest>
        <LastModified>2009-11-12T17:50:30.000Z</LastModified>
        <Owner>
            <ID>75aa57f09aa0c8caeab4f8c24e99d10f8e7faeebf76c078efc7c6caea54ba06a</ID>
            <DisplayName>mtd@amazon.com</DisplayName>
        </Owner>    
    </DeleteMarker>
*/
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_Prefix = L"//ListVersionsResult/Prefix";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_IsTruncated = L"//ListVersionsResult/IsTruncated";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_NextKeyMarker = L"//ListVersionsResult/NextKeyMarker";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_NextVersionIdMarker = L"//ListVersionsResult/NextVersionIdMarker";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_Key = L"//ListVersionsResult/Version/Key";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_VersionId = L"//ListVersionsResult/Version/VersionId";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_IsLatest = L"//ListVersionsResult/Version/IsLatest";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_LastModified = L"//ListVersionsResult/Version/LastModified";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_ETag = L"//ListVersionsResult/Version/ETag";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_Size = L"//ListVersionsResult/Version/Size";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_Owner_ID = L"//ListVersionsResult/Version/Owner/ID";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_Owner_DisplayName = L"//ListVersionsResult/Version/Owner/DisplayName";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_ELEMENT_Contents = L"//ListVersionsResult/Version";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_DELETED_ELEMENT = L"//ListVersionsResult/DeleteMarker";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_DELETED_Key = L"//ListVersionsResult/DeleteMarker/Key";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_DELETED_VersionId = L"//ListVersionsResult/DeleteMarker/VersionId";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_DELETED_IsLatest = L"//ListVersionsResult/DeleteMarker/IsLatest";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_DELETED_LastModified = L"//ListVersionsResult/DeleteMarker/LastModified";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_DELETED_Owner_ID = L"//ListVersionsResult/DeleteMarker/Owner/ID";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_DELETED_Owner_DisplayName = L"//ListVersionsResult/DeleteMarker/Owner/DisplayName";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_CommonPrefixes_Prefix = L"//ListVersionsResult/CommonPrefixes/Prefix";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_ELEMENT_CommonPrefixes = L"//ListVersionsResult/CommonPrefixes";
const WCHAR * const XML_S3_DIR_LISTING_VERSIONS_ROOT_ELEMENT = L"//ListVersionsResult";

HRESULT XmlDirListingS3VersionsCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	CECSConnection::XML_DIR_LISTING_CONTEXT *pInfo = (CECSConnection::XML_DIR_LISTING_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (psValue != nullptr)
		{
			if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_Prefix) == 0)
			{
				pInfo->sPrefix = pInfo->sPrefixNoObj = FROM_UNICODE(*psValue);
				if (!pInfo->sObjName.IsEmpty() && (pInfo->sPrefixNoObj.Right(pInfo->sObjName.GetLength()) == pInfo->sObjName))
					(void)pInfo->sPrefixNoObj.Delete(pInfo->sPrefixNoObj.GetLength() - pInfo->sObjName.GetLength(), pInfo->sObjName.GetLength());
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_IsTruncated) == 0)
			{
				pInfo->bIsTruncated = FROM_UNICODE(*psValue) == _T("true");
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_NextKeyMarker) == 0)
			{
				pInfo->sS3NextKeyMarker = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_NextVersionIdMarker) == 0)
			{
				pInfo->sS3NextVersionIdMarker = FROM_UNICODE(*psValue);
			}
			else if ((sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_Key) == 0)
				|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_DELETED_Key) == 0))
			{
				pInfo->bGotKey = true;
				pInfo->Rec.bDir = false;
				pInfo->Rec.sName = FROM_UNICODE(*psValue);
				ASSERT(pInfo->sPrefixNoObj.CompareNoCase(pInfo->Rec.sName.Left(pInfo->sPrefixNoObj.GetLength())) == 0);
				if (pInfo->sPrefixNoObj.CompareNoCase(pInfo->Rec.sName.Left(pInfo->sPrefixNoObj.GetLength())) == 0)
					(void)pInfo->Rec.sName.Delete(0, pInfo->sPrefixNoObj.GetLength());
				if (!pInfo->Rec.sName.IsEmpty())
				{
					if (pInfo->Rec.sName[pInfo->Rec.sName.GetLength() - 1] == _T('/'))
					{
						(void)pInfo->Rec.sName.Delete(pInfo->Rec.sName.GetLength() - 1);
						pInfo->Rec.bDir = true;
					}
				}
			}
			else if ((sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_LastModified) == 0)
				|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_DELETED_LastModified) == 0))
			{
				CECSConnection::S3_ERROR Error = CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->Rec.Properties.ftLastMod);
				if (Error.IfError())
					return Error.dwError;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_ETag) == 0)
			{
				pInfo->Rec.Properties.sETag = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_Size) == 0)
			{
				_stscanf_s(FROM_UNICODE(*psValue), _T("%I64u"), &pInfo->Rec.Properties.llSize);
			}
			else if ((sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_Owner_ID) == 0)
				|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_DELETED_Owner_ID) == 0))
			{
				pInfo->Rec.Properties.sOwnerID = FROM_UNICODE(*psValue);
			}
			else if ((sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_Owner_DisplayName) == 0)
				|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_DELETED_Owner_DisplayName) == 0))
			{
				pInfo->Rec.Properties.sOwnerDisplayName = FROM_UNICODE(*psValue);
			}
			else if ((sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_VersionId) == 0)
				|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_DELETED_VersionId) == 0))
			{
				pInfo->Rec.Properties.sVersionId = FROM_UNICODE(*psValue);
				if (pInfo->Rec.Properties.sVersionId == _T("null"))
					pInfo->Rec.Properties.sVersionId.Empty();
			}
			else if ((sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_IsLatest) == 0)
				|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_DELETED_IsLatest) == 0))
			{
				pInfo->Rec.Properties.bIsLatest = FROM_UNICODE(*psValue) == _T("true");
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_CommonPrefixes_Prefix) == 0)
			{
				pInfo->Rec.sName = FROM_UNICODE(*psValue);
				if (pInfo->sPrefixNoObj == psValue->Left(pInfo->sPrefixNoObj.GetLength()))
				{
					(void)pInfo->Rec.sName.Delete(0, pInfo->sPrefixNoObj.GetLength());
					if (!pInfo->Rec.sName.IsEmpty() && (pInfo->Rec.sName[pInfo->Rec.sName.GetLength() - 1] == _T('/')))
						(void)pInfo->Rec.sName.Delete(pInfo->Rec.sName.GetLength() - 1);
				}
				pInfo->Rec.bDir = true;
			}
		}
		break;

	case XmlNodeType_EndElement:
		if ((sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_ELEMENT_Contents) == 0)
			|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_DELETED_ELEMENT) == 0)
			|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_ELEMENT_CommonPrefixes) == 0))
		{
//			if (!pInfo->Rec.sName.IsEmpty())
			{
				bool bDup = false;
				// first search the list to make sure this entry isn't duplicated (only for folders)
				if (pInfo->Rec.bDir)
				{
					for (CECSConnection::DirEntryList_t::const_iterator itList = pInfo->pDirList->begin(); itList != pInfo->pDirList->end(); ++itList)
						if (itList->bDir && (itList->sName == pInfo->Rec.sName))
						{
							bDup = true;
							break;
						}
				}
				if (!bDup)
				{
					if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_DELETED_ELEMENT) == 0)
						pInfo->Rec.Properties.bDeleted = true;
					pInfo->pDirList->push_back(pInfo->Rec);
					if (pInfo->pszSearchName != nullptr)
					{
						ASSERT(pInfo->psRetSearchName != nullptr);
						if (pInfo->Rec.sName.Compare(pInfo->pszSearchName) == 0)
							*pInfo->psRetSearchName = pInfo->Rec.sName;
					}
				}
				pInfo->EmptyRec();
			}
		}
		break;

	case XmlNodeType_Element:
		if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_VERSIONS_ROOT_ELEMENT) == 0)
		{
			pInfo->bGotRootElement = true;
		}
		break;

	default:
		break;
	}
	return 0;
}

/*
<ListBucketResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Name>example-bucket</Name>
  <Prefix></Prefix>
  <Marker></Marker>
  <MaxKeys>1000</MaxKeys>
  <Delimiter>/</Delimiter>
  <IsTruncated>false</IsTruncated>
  <Contents>
    <Key>sample.html</Key>
    <LastModified>2011-02-26T01:56:20.000Z</LastModified>
    <ETag>&quot;bf1d737a4d46a19f3bced6905cc8b902&quot;</ETag>
    <Size>142863</Size>
    <Owner>
      <ID>canonical-user-id</ID>
      <DisplayName>display-name</DisplayName>
    </Owner>
    <StorageClass>STANDARD</StorageClass>
  </Contents>
  <CommonPrefixes>
    <Prefix>photos/</Prefix>
  </CommonPrefixes>
</ListBucketResult>
*/

const WCHAR * const XML_S3_DIR_LISTING_Prefix = L"//ListBucketResult/Prefix";
const WCHAR * const XML_S3_DIR_LISTING_IsTruncated = L"//ListBucketResult/IsTruncated";
const WCHAR * const XML_S3_DIR_LISTING_NextMarker = L"//ListBucketResult/NextMarker";
const WCHAR * const XML_S3_DIR_LISTING_Key = L"//ListBucketResult/Contents/Key";
const WCHAR * const XML_S3_DIR_LISTING_LastModified = L"//ListBucketResult/Contents/LastModified";
const WCHAR * const XML_S3_DIR_LISTING_ETag = L"//ListBucketResult/Contents/ETag";
const WCHAR * const XML_S3_DIR_LISTING_Size = L"//ListBucketResult/Contents/Size";
const WCHAR * const XML_S3_DIR_LISTING_Owner_ID = L"//ListBucketResult/Contents/Owner/ID";
const WCHAR * const XML_S3_DIR_LISTING_Owner_DisplayName = L"//ListBucketResult/Contents/Owner/DisplayName";
const WCHAR * const XML_S3_DIR_LISTING_ELEMENT_Contents = L"//ListBucketResult/Contents";
const WCHAR * const XML_S3_DIR_LISTING_CommonPrefixes_Prefix = L"//ListBucketResult/CommonPrefixes/Prefix";
const WCHAR * const XML_S3_DIR_LISTING_ELEMENT_CommonPrefixes = L"//ListBucketResult/CommonPrefixes";
const WCHAR * const XML_S3_DIR_LISTING_ROOT_ELEMENT = L"//ListBucketResult";

HRESULT XmlDirListingS3CB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	CECSConnection::XML_DIR_LISTING_CONTEXT *pInfo = (CECSConnection::XML_DIR_LISTING_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (psValue != nullptr)
		{
			if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_Prefix) == 0)
			{
				pInfo->sPrefix = pInfo->sPrefixNoObj = FROM_UNICODE(*psValue);
				if (!pInfo->sObjName.IsEmpty() && (pInfo->sPrefixNoObj.Right(pInfo->sObjName.GetLength()) == pInfo->sObjName))
					(void)pInfo->sPrefixNoObj.Delete(pInfo->sPrefixNoObj.GetLength() - pInfo->sObjName.GetLength(), pInfo->sObjName.GetLength());
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_IsTruncated) == 0)
			{
				pInfo->bIsTruncated = FROM_UNICODE(*psValue) == _T("true");
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_NextMarker) == 0)
			{
				pInfo->sS3NextMarker = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_Key) == 0)
			{
				pInfo->bGotKey = true;
				pInfo->Rec.bDir = false;
				pInfo->Rec.sName = FROM_UNICODE(*psValue);
				ASSERT(pInfo->sPrefixNoObj.CompareNoCase(pInfo->Rec.sName.Left(pInfo->sPrefixNoObj.GetLength())) == 0);
				if (pInfo->sPrefixNoObj.CompareNoCase(pInfo->Rec.sName.Left(pInfo->sPrefixNoObj.GetLength())) == 0)
					(void)pInfo->Rec.sName.Delete(0, pInfo->sPrefixNoObj.GetLength());
				if (!pInfo->Rec.sName.IsEmpty())
				{
					if (pInfo->Rec.sName[pInfo->Rec.sName.GetLength() - 1] == _T('/'))
					{
						(void)pInfo->Rec.sName.Delete(pInfo->Rec.sName.GetLength() - 1);
						pInfo->Rec.bDir = true;
					}
				}
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_LastModified) == 0)
			{
				CECSConnection::S3_ERROR Error = CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->Rec.Properties.ftLastMod);
				if (Error.IfError())
					return Error.dwError;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_ETag) == 0)
			{
				pInfo->Rec.Properties.sETag = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_Size) == 0)
			{
				_stscanf_s(FROM_UNICODE(*psValue), _T("%I64u"), &pInfo->Rec.Properties.llSize);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_Owner_ID) == 0)
			{
				pInfo->Rec.Properties.sOwnerID = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_Owner_DisplayName) == 0)
			{
				pInfo->Rec.Properties.sOwnerDisplayName = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_CommonPrefixes_Prefix) == 0)
			{
				pInfo->Rec.sName = FROM_UNICODE(*psValue);
				if (pInfo->sPrefixNoObj == psValue->Left(pInfo->sPrefixNoObj.GetLength()))
				{
					(void)pInfo->Rec.sName.Delete(0, pInfo->sPrefixNoObj.GetLength());
					if (!pInfo->Rec.sName.IsEmpty() && (pInfo->Rec.sName[pInfo->Rec.sName.GetLength() - 1] == _T('/')))
						(void)pInfo->Rec.sName.Delete(pInfo->Rec.sName.GetLength() - 1);
				}
				pInfo->Rec.bDir = true;
			}
		}
		break;

	case XmlNodeType_EndElement:
		if ((sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_ELEMENT_Contents) == 0)
			|| (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_ELEMENT_CommonPrefixes) == 0))
		{
			if (!pInfo->Rec.sName.IsEmpty())
			{
				bool bDup = false;
				// first search the list to make sure this entry isn't duplicated (only for folders)
				if (pInfo->Rec.bDir)
				{
					for (CECSConnection::DirEntryList_t::const_iterator itList = pInfo->pDirList->begin(); itList != pInfo->pDirList->end(); ++itList)
						if (itList->bDir && (itList->sName == pInfo->Rec.sName))
						{
							bDup = true;
							break;
						}
				}
				if (!bDup)
				{
					pInfo->pDirList->push_back(pInfo->Rec);
					if (pInfo->pszSearchName != nullptr)
					{
						ASSERT(pInfo->psRetSearchName != nullptr);
						if (pInfo->Rec.sName.Compare(pInfo->pszSearchName) == 0)
							*pInfo->psRetSearchName = pInfo->Rec.sName;
					}
				}
				pInfo->EmptyRec();
			}
		}
		break;

	case XmlNodeType_Element:
		if (sXmlPath.CompareNoCase(XML_S3_DIR_LISTING_ROOT_ELEMENT) == 0)
		{
			pInfo->bGotRootElement = true;
		}
		break;

	default:
		break;
	}
	return 0;
}

inline void AppendQuery(CString& sResource, bool& bContinuation, const CString& sQuery)
{
	sResource += (bContinuation ? _T("&") : _T("?")) + sQuery;
	bContinuation = true;
}

CECSConnection::S3_ERROR CECSConnection::DirListingInternal(
	LPCTSTR pszPathIn,
	DirEntryList_t& DirList,
	LPCTSTR pszSearchName,
	CString& sRetSearchName,
	bool bS3Versions,
	bool bSingle,							// if true, don't keep going back for more files. we just want to know if there are SOME
	LPCTSTR pszObjName,
	LISTING_NEXT_MARKER_CONTEXT *pNextRequestMarker,	// if non-nullptr, only one request is done at a time. this holds the next marker for the next page of entries
	TCHAR cDelimiter)						// defaults to L'/'. if NUL, then don't do a delimiter listing
{
	CStateRef State(this);
	S3_ERROR Error;
	XML_DIR_LISTING_CONTEXT Context;
	list<HEADER_REQ> Req;
	CString sS3NextMarker;				// next marker for next page
	CString sS3NextKeyMarker;			// passed to key-marker in next request (version listing)
	CString sS3NextVersionIdMarker;		// passed to version-id-marker in next request (version listing)

	try
	{
		Context.sPathIn = pszPathIn;
		Context.sObjName = pszObjName;
		Context.pszSearchName = pszSearchName;
		Context.psRetSearchName = &sRetSearchName;
		Context.pDirList = &DirList;
		Context.bS3Versions = bS3Versions;
		Context.bSingle = bSingle;
		{
			CSingleLock lockDir(&Context.csDirList, true);
			DirList.clear();
		}
		CBuffer RetData;
		do
		{
			if (pNextRequestMarker != nullptr)
			{
				sS3NextMarker = pNextRequestMarker->sS3NextMarker;
				sS3NextKeyMarker = pNextRequestMarker->sS3NextKeyMarker;
				sS3NextVersionIdMarker = pNextRequestMarker->sS3NextVersionIdMarker;
				pNextRequestMarker->bTruncated = false;
			}
			Req.clear();
			InitHeader();
			CString sResource;
			if (Context.sPathIn.IsEmpty())
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_NAME);
			{
				// gotta take the bucket off the first component of the path
				CString sBucket, sPrefix;
				int iSlash = Context.sPathIn.Find(_T('/'), 1);
				if (iSlash < 0)
					sBucket = Context.sPathIn;
				else
				{
					CString sObjName(pszObjName);
					sBucket = Context.sPathIn.Left(iSlash);					// don't include terminating slash
					sPrefix = Context.sPathIn.Mid(iSlash + 1) + sObjName;
					sPrefix = UriEncode(sPrefix, E_URI_ENCODE::AllSAFE);
				}
				sResource = sBucket + _T("/");
				bool bContinuation = false;
				if (bS3Versions)
					AppendQuery(sResource, bContinuation, _T("versions"));
				if (cDelimiter != _T('\0'))
					AppendQuery(sResource, bContinuation, CString(_T("delimiter=")) + cDelimiter);
				{
					DWORD dwMaxKeys = 0;
					if ((pNextRequestMarker != nullptr) && (pNextRequestMarker->dwMaxNum != 0))
						dwMaxKeys = pNextRequestMarker->dwMaxNum;
					else if (bSingle)
						dwMaxKeys = 10;
					else if ((dwS3BucketListingMax >= 10) && (dwS3BucketListingMax < 1000))
						dwMaxKeys = dwS3BucketListingMax;
					if (dwMaxKeys != 0)
						AppendQuery(sResource, bContinuation, _T("max-keys=") + FmtNum(dwMaxKeys));
				}
				if (!sPrefix.IsEmpty())
					AppendQuery(sResource, bContinuation, _T("prefix=") + sPrefix);
				if (!sS3NextMarker.IsEmpty())
					AppendQuery(sResource, bContinuation, _T("marker=") + UriEncode(sS3NextMarker, E_URI_ENCODE::AllSAFE));
				if (!sS3NextKeyMarker.IsEmpty())
					AppendQuery(sResource, bContinuation, _T("key-marker=") + UriEncode(sS3NextKeyMarker, E_URI_ENCODE::AllSAFE));
				if (!sS3NextVersionIdMarker.IsEmpty())
					AppendQuery(sResource, bContinuation, _T("version-id-marker=") + UriEncode(sS3NextVersionIdMarker, E_URI_ENCODE::AllSAFE));
			}
			Error = SendRequest(_T("GET"), (LPCTSTR)sResource, nullptr, 0, RetData, &Req);
			if (Error.IfError())
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			if (RetData.IsEmpty())
			{
				// it returns SUCCESS but there is no data!
				// this seems to be an invalid condition, return Internal Error
				Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
				Error.S3Error = S3_ERROR_UNKNOWN;
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			}
			if (strncmp((LPCSTR)RetData.GetData(), "<?xml", 5) != 0)
			{
				// XML doesn't look valid. maybe we are connected to the wrong server?
				// maybe there is a man-in-middle attack?
				Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
				Error.S3Error = S3_ERROR_MalformedXML;
				Error.sS3Code = _T("MalformedXML");
				Error.sS3RequestID = _T("GET");
				Error.sS3Resource = sResource;
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			}
			Context.bGotRootElement = false;
			{
				XMLLITE_READER_CB procXmlDirListingCB;
				sS3NextMarker.Empty();
				sS3NextKeyMarker.Empty();
				sS3NextVersionIdMarker.Empty();
				if (bS3Versions)
				{
					procXmlDirListingCB = XmlDirListingS3VersionsCB;
				}
				else
				{
					procXmlDirListingCB = XmlDirListingS3CB;
				}
				{
					CSingleLock lockDir(&Context.csDirList, true);
					HRESULT hr = ScanXml(&RetData, &Context, procXmlDirListingCB);
					if (FAILED(hr))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, hr);
					if (!Context.sPrefix.IsEmpty() && !Context.bIsTruncated && Context.pDirList->empty() && !Context.bGotKey)
					{
						Error = CECSConnection::S3_ERROR();
						Error.dwHttpError = HTTP_STATUS_NOT_FOUND;
						Error.S3Error = S3_ERROR_NoSuchKey;
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
					}
					if (Context.bIsTruncated)
					{
						// listing was truncated
						sS3NextMarker = Context.sS3NextMarker;
						sS3NextKeyMarker = Context.sS3NextKeyMarker;
						sS3NextVersionIdMarker = Context.sS3NextVersionIdMarker;
						if (pNextRequestMarker != nullptr)
						{
							pNextRequestMarker->sS3NextMarker = sS3NextMarker;
							pNextRequestMarker->sS3NextKeyMarker = sS3NextKeyMarker;
							pNextRequestMarker->sS3NextVersionIdMarker = sS3NextVersionIdMarker;
							pNextRequestMarker->bTruncated = true;
						}
					}
					else
					{
						if (pNextRequestMarker != nullptr)
							pNextRequestMarker->Clear();
					}
				}
			}
			// validate XML
			// make sure that it appears to be basically correct
			// we don't want total garbage making it look like there are NO entries in the directory
			// and therefore locally deleting all the files in the dir
			if (!Context.bGotRootElement)
			{
				// XML doesn't look valid. maybe we are connected to the wrong server?
				// maybe there is a man-in-middle attack?
				Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
				Error.S3Error = S3_ERROR_MalformedXML;
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			}
			if (pNextRequestMarker != nullptr)
				break;
		} while ((!sS3NextMarker.IsEmpty() || !sS3NextKeyMarker.IsEmpty() || !sS3NextVersionIdMarker.IsEmpty()) && !bSingle);
		DirList.sort();
	}
	catch (const CS3ErrorInfo& E)
	{
		Error = E.Error;
	}
	catch (const CErrorInfo& E)
	{
		Error.dwError = E.dwError;
	}
	return Error;
}

CECSConnection::S3_ERROR CECSConnection::DirListing(LPCTSTR pszPath, DirEntryList_t& DirList, bool bSingle, LPCTSTR pszObjName, LISTING_NEXT_MARKER_CONTEXT *pNextRequestMarker, TCHAR cDelimiter)
{
	CString sRetSearchName;
	return DirListingInternal(pszPath, DirList, nullptr, sRetSearchName, false, bSingle, pszObjName, pNextRequestMarker, cDelimiter);
}

CECSConnection::S3_ERROR CECSConnection::DirListingS3Versions(LPCTSTR pszPath, DirEntryList_t& DirList, LPCTSTR pszObjName, LISTING_NEXT_MARKER_CONTEXT *pNextRequestMarker, TCHAR cDelimiter)
{
	CString sRetSearchName;
	return DirListingInternal(pszPath, DirList, nullptr, sRetSearchName, true, false, pszObjName, pNextRequestMarker, cDelimiter);
}

CString CECSConnection::S3_ERROR_BASE::Format(bool bOneLine) const
{
	CString sMsg, sLineEnd;
	if (bOneLine)
		sLineEnd = _T(", ");
	else
		sLineEnd = _T("\r\n");
	if (dwError != ERROR_SUCCESS)
		sMsg = GetNTErrorText(dwError);
	// if HTTP error unexpected, be sure to display it
	if (((dwHttpError != 0) && (dwHttpError != 200)) || (dwError == (DWORD)HTTP_E_STATUS_UNEXPECTED))
	{
		CString sHttpMsg;
		sHttpMsg = FmtNum(dwHttpError);
		if (!sMsg.IsEmpty())
			sMsg += sLineEnd;
		sMsg += _T("Http: ") + sHttpMsg;
	}
	if (S3Error != S3_ERROR_SUCCESS)
	{
		CString sErrorText;
		if (S3ErrorInfo(S3Error, nullptr, &sErrorText))
		{
			if (!sMsg.IsEmpty())
				sMsg += sLineEnd;
			sMsg += L"S3Error = " + sErrorText;
		}
	}
	if (!sS3Code.IsEmpty())
	{
		if (!sMsg.IsEmpty())
			sMsg += sLineEnd;
		sMsg += _T("S3 Code: ") + sS3Code;
	}
	if (!sS3Resource.IsEmpty())
	{
		if (!sMsg.IsEmpty())
			sMsg += sLineEnd;
		sMsg += _T("S3 Resource: ") + sS3Resource;
	}
	if (!sS3RequestID.IsEmpty())
	{
		if (!sMsg.IsEmpty())
			sMsg += sLineEnd;
		sMsg += _T("S3 RequestID: ") + sS3RequestID;
	}
	if (dwSecureError != 0)
	{
		if (!sMsg.IsEmpty())
			sMsg += sLineEnd;
		sMsg += _T("Secure Error: ") + GetSecureErrorText(dwSecureError) + sLineEnd;
		// with a secure error, chances are the problem has to do with the certificate
		// so we'll dump the certificate with the error text
		CString sCert;
		sCert.Format(_T("Cert Name: %s\n\nCert Subject:\n%s\n\nCert Subject Alternate Names:\n%s\n"),
			(LPCTSTR)CertInfo.sCertName, (LPCTSTR)CertInfo.sCertSubject, (LPCTSTR)CertInfo.sCertSubjectAltName);
		sMsg += sCert;
	}
	if (!sDetails.IsEmpty())
	{
		if (!sMsg.IsEmpty())
			sMsg += sLineEnd;
		sMsg += _T("Details: ") + sDetails;
	}
	if (!sHostAddr.IsEmpty() && IfError())
	{
		if (!sMsg.IsEmpty())
			sMsg += sLineEnd;
		sMsg += _T("IP: ") + sHostAddr;
	}
	if (bOneLine)
	{
		// there still may be line ends in there because some error text generated by MS contain line ends
		int iEnd;
		for (;;)
		{
			iEnd = sMsg.Find(_T("\r\n"));
			if (iEnd < 0)
				break;
			(void)sMsg.Delete(iEnd);
			sMsg.SetAt(iEnd, _T(' '));
		} 
		for (;;)
		{
			iEnd = sMsg.Find(_T("\n\r"));
			if (iEnd < 0)
				break;
			(void)sMsg.Delete(iEnd);
			sMsg.SetAt(iEnd, _T(' '));
		}
		for (;;)
		{
			iEnd = sMsg.Find(_T("\n"));
			if (iEnd < 0)
				break;
			sMsg.SetAt(iEnd, _T(' '));
		}
		for (;;)
		{
			iEnd = sMsg.Find(_T("\r"));
			if (iEnd < 0)
				break;
			sMsg.SetAt(iEnd, _T(' '));
		}
	}
	return sMsg;
}

void CECSConnection::SetThrottle(
	LPCTSTR pszHost,						// host name to apply throttle to
	int iUploadThrottleRate,				// upload throttle rate in bytes/sec. set to 0 to remove throttle for this host
	int iDownloadThrottleRate)				// download throttle
{
	CSingleLock lock(&csThrottleMap, true);

	if ((iUploadThrottleRate == 0) && (iDownloadThrottleRate == 0))
	{
		(void)ThrottleMap.erase(pszHost);		// erase any entry for the specified host
		if (ThrottleMap.empty())
		{
			lock.Unlock();
			TimerThread.KillThreadWait();
		}
	}
	else
	{
		THROTTLE_REC Rec;
		Rec.Upload.iBytesCurInterval = Rec.Upload.iBytesSec = iUploadThrottleRate;
		Rec.Download.iBytesCurInterval = Rec.Download.iBytesSec = iDownloadThrottleRate;
		pair<map<CString,THROTTLE_REC>::iterator, bool> ret = ThrottleMap.insert(make_pair(pszHost, Rec));
		if (!ret.second)
		{
			ret.first->second.Upload.iBytesCurInterval = ret.first->second.Upload.iBytesSec = iUploadThrottleRate;
			ret.first->second.Download.iBytesCurInterval = ret.first->second.Download.iBytesSec = iDownloadThrottleRate;
		}
		if (!TimerThread.IfActive())
			(void)TimerThread.CreateThread();
		TimerThread.StartWork();
		// wait for the thread to start, so we don't have another thread trying to start it during this period
		// don't wait more than a couple seconds
		for (UINT i = 0; i < 200; i++)
		{
			if (TimerThread.GetThreadInitialized())
				break;
			Sleep(10);
		}
	}
}

void CECSConnection::TerminateThrottle(void)
{
	TimerThread.KillThreadWait();
	CSingleLock lock(&csThrottleMap, true);
	ThrottleMap.clear();
}

void CECSConnection::IfThrottle(bool *pbDownloadThrottle, bool *pbUploadThrottle)
{
	bool bUploadThrottle = false;
	bool bDownloadThrottle = false;
	CSingleLock lock(&csThrottleMap, true);
	map<CString, THROTTLE_REC>::iterator itMap;
	itMap = ThrottleMap.find(sHost);
	if (itMap != ThrottleMap.end())
	{
		if (itMap->second.Upload.iBytesSec != 0)
			bUploadThrottle = true;
		if (itMap->second.Download.iBytesSec != 0)
			bDownloadThrottle = true;
	}
	if (pbDownloadThrottle != nullptr)
		*pbDownloadThrottle = bDownloadThrottle;
	if (pbUploadThrottle != nullptr)
		*pbUploadThrottle = bUploadThrottle;
}

struct XML_S3_SERVICE_INFO_CONTEXT
{
	CECSConnection::S3_SERVICE_INFO *pServiceInfo;
	CECSConnection::S3_BUCKET_INFO Entry;

	XML_S3_SERVICE_INFO_CONTEXT()
		: pServiceInfo(nullptr)
	{}
};

const WCHAR * const XML_S3_SERVICE_OWNER_ID = L"//ListAllMyBucketsResult/Owner/ID";
const WCHAR * const XML_S3_SERVICE_OWNER_NAME = L"//ListAllMyBucketsResult/Owner/DisplayName";
const WCHAR * const XML_S3_SERVICE_BUCKET_NAME = L"//ListAllMyBucketsResult/Buckets/Bucket/Name";
const WCHAR * const XML_S3_SERVICE_BUCKET_DATE = L"//ListAllMyBucketsResult/Buckets/Bucket/CreationDate";
const WCHAR * const XML_S3_SERVICE_BUCKET_ELEMENT = L"//ListAllMyBucketsResult/Buckets/Bucket";

HRESULT XmlS3ServiceInfoCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_SERVICE_INFO_CONTEXT *pInfo = (XML_S3_SERVICE_INFO_CONTEXT *)pContext;
	if ((pInfo == nullptr) || (pInfo->pServiceInfo == nullptr))
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (sXmlPath.CompareNoCase(XML_S3_SERVICE_OWNER_ID) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->pServiceInfo->sOwnerID = FROM_UNICODE(*psValue);
		}
		else if (sXmlPath.CompareNoCase(XML_S3_SERVICE_OWNER_NAME) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->pServiceInfo->sOwnerDisplayName = FROM_UNICODE(*psValue);
		}
		else if (sXmlPath.CompareNoCase(XML_S3_SERVICE_BUCKET_NAME) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->Entry.sName = FROM_UNICODE(*psValue);
		}
		else if (sXmlPath.CompareNoCase(XML_S3_SERVICE_BUCKET_DATE) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
			{
				CECSConnection::S3_ERROR Error = CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->Entry.ftCreationDate);
				if (Error.IfError())
					ZeroFT(pInfo->Entry.ftCreationDate);
			}
		}
		break;

	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_S3_SERVICE_BUCKET_ELEMENT) == 0)
		{
			// finished receiving a BUCKET element
			if (!pInfo->Entry.sName.IsEmpty())
			{
				pInfo->pServiceInfo->BucketList.push_back(pInfo->Entry);
				pInfo->Entry.Empty();
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

// S3ServiceInformation
// return service version information
// this is used mostly as way to know if the server is up and can be connected to
CECSConnection::S3_ERROR CECSConnection::S3ServiceInformation(S3_SERVICE_INFO& ServiceInfo)
{
	CStateRef State(this);
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	Error = SendRequest(_T("GET"), _T("/"), nullptr, 0, RetData, &Req);
	if (Error.IfError())
		return Error;
	ServiceInfo.BucketList.clear();
	ServiceInfo.sOwnerDisplayName.Empty();
	ServiceInfo.sOwnerID.Empty();
	LPCSTR pXML = (LPCSTR)RetData.GetData();
	if ((RetData.GetBufSize() > 5) && (pXML != nullptr) && (strncmp(pXML, "<?xml", 5) == 0))
	{
		XML_S3_SERVICE_INFO_CONTEXT Context;
		Context.pServiceInfo = &ServiceInfo;
		HRESULT hr = ScanXml(&RetData, &Context, XmlS3ServiceInfoCB);
		if (FAILED(hr))
			return hr;
	}
	else
	{
		// XML doesn't look valid. maybe we are connected to the wrong server?
		// maybe there is a man-in-middle attack?
		Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
		Error.S3Error = S3_ERROR_MalformedXML;
		Error.sS3Code = _T("MalformedXML");
		Error.sS3RequestID = _T("GET");
		Error.sS3Resource = _T("/");
	}
	return Error;
}

// CECSConnection::GetHost
CString CECSConnection::GetHost(void) const
{
	return sHost;
}

// WriteMetadata
// set specified metadata to path
// metadata is encoded using base64 and there is a maximum of 1k characters for each field
// and a maximum of 8k for the header
// it will encode it. if it is bigger than 7k, it is rejected
// if not, it splits it into 1k segments. The first has the name specified (pszTag)
// the overflow adds "_n", where n is 2 to 7
void CECSConnection::WriteMetadataEntry(list<HEADER_STRUCT>& MDList, LPCTSTR pszTag, const CBuffer& Data)
{
	// convert to ANSI string
	WriteMetadataEntry(MDList, pszTag, Data.EncodeBase64());
}

void CECSConnection::WriteMetadataEntry(list<HEADER_STRUCT>& MDList, LPCTSTR pszTag, const CString& sStr)
{
	// if the specified tag already exists, erase it
	CString sTag(pszTag);
	for (list<HEADER_STRUCT>::iterator itList = MDList.begin(); itList != MDList.end(); )
	{
		if (sTag.CompareNoCase(itList->sHeader) == 0)
			itList = MDList.erase(itList);
		else
			++itList;
	}
	MDList.emplace_back(HEADER_STRUCT(pszTag, sStr));
}

CECSConnection::S3_ERROR CECSConnection::CopyS3(
	LPCTSTR pszSrcPath,			// S3 path of source object
	LPCTSTR pszTargetPath,		// S3 path of target object
	LPCTSTR pszVersionId,		// nonNULL: version ID to copy
	bool bCopyMD,				// true - copy metadata, false - replace
	ULONGLONG ullObjSize,		// optional - if object size supplied it doesn't have to query it
	const list<HEADER_STRUCT> *pMDList)	// list of metadata to apply to object
{
	const ULONGLONG MULTIPART_SIZE = GIGABYTES(1ULL);		// each part will be 1GB

	CStateRef State(this);
	S3_ERROR Error;
	CBuffer RetData;
	list<HEADER_REQ> Req;
	bool bMultiPartInitiated = false;
	list<shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY>> PartList;
	S3_UPLOAD_PART_INFO MultiPartInfo;

	try
	{
		InitHeader();
		// if object is < 5GB, a single copy will do it.
		// now construct the new metadata list
		if (ullObjSize < GIGABYTES(5ULL))
		{
			CString sSrcPath(UriEncode(pszSrcPath, E_URI_ENCODE::AllSAFE));
			if (pszVersionId != nullptr)
				sSrcPath += CString(_T("?versionId=")) + pszVersionId;
			AddHeader(_T("x-amz-copy-source"), sSrcPath);							// specify source of copy
			AddHeader(_T("x-amz-metadata-directive"), bCopyMD ? _T("COPY") : _T("REPLACE"));
			if ((pMDList != nullptr) && !pMDList->empty())
			{
				// add in all metadata
				for (list<HEADER_STRUCT>::const_iterator itList = pMDList->begin(); itList != pMDList->end(); ++itList)
				{
					if (!itList->sContents.IsEmpty() && (itList->sHeader.Find(sAmzMetaPrefix) == 0))
						AddHeader(itList->sHeader, itList->sContents);
				}
			}
			Error = SendRequest(_T("PUT"), UriEncode(pszTargetPath), nullptr, 0, RetData, &Req);
			return Error;
		}
		Error = S3MultiPartInitiate(pszTargetPath, MultiPartInfo, pMDList);
		if (Error.IfError())
			return Error;
		bMultiPartInitiated = true;
		// copy all parts
		ULONGLONG ullOffset = 0ULL;
		for (UINT uPartNum = 1; ; ++uPartNum)
		{
			shared_ptr<S3_UPLOAD_PART_ENTRY> pPartEntry = make_shared<S3_UPLOAD_PART_ENTRY>();
			pPartEntry->uPartNum = uPartNum;
			pPartEntry->ullBaseOffset = ullOffset;
			pPartEntry->Checksum.Empty();
			pPartEntry->sETag.Empty();
			pPartEntry->ullPartSize = ((ullObjSize - ullOffset) < (ULONGLONG)MULTIPART_SIZE) ? (ullObjSize - ullOffset) : MULTIPART_SIZE;
			if ((pPartEntry->ullPartSize == 0) || (ullObjSize <= ullOffset))
				break;
			Error = S3MultiPartUpload(MultiPartInfo, *pPartEntry, nullptr, pPartEntry->ullPartSize, pszSrcPath, ullOffset, pszVersionId);
			if (Error.IfError())
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
			PartList.push_back(pPartEntry);
			ullOffset += pPartEntry->ullPartSize;
		}
		S3_MPU_COMPLETE_INFO MPUCompleteInfo;
		Error = S3MultiPartComplete(MultiPartInfo, PartList, MPUCompleteInfo);
		if (Error.IfError())
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
	}
	catch (const CS3ErrorInfo& E)
	{
		if (bMultiPartInitiated)
		{
			(void)S3MultiPartAbort(MultiPartInfo);

		}
		return E.Error;
	}

	return Error;
}

CECSConnection::S3_ERROR CECSConnection::UpdateMetadata(LPCTSTR pszPath, const list<HEADER_STRUCT>& MDListParam, const list<CString> *pDeleteTagParam)
{
	CStateRef State(this);
	S3_ERROR Error;
	try
	{
		list<HEADER_STRUCT> MDList = MDListParam;
		CBuffer RetData;
		list<HEADER_REQ> Req;
		CString sPath(pszPath);
		InitHeader();
		Error = SendRequest(_T("HEAD"), UriEncode(sPath), nullptr, 0, RetData, &Req);
		if (Error.IfError())
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);

		for (list<HEADER_REQ>::iterator it = Req.begin(); it != Req.end(); ++it)
		{
			if (!it->ContentList.empty() && (it->sHeader.Find(sAmzMetaPrefix) == 0))
			{
				for (list<HEADER_STRUCT>::const_iterator itList = MDList.begin(); itList != MDList.end();)
				{
					if ((itList->sHeader.CompareNoCase(it->sHeader) == 0) && !it->ContentList.empty())
					{
						it->ContentList.front() = itList->sContents;
						itList = MDList.erase(itList);
					}
					else
						++itList;
				}
			}
		}
		// now construct the new metadata list
		for (list<HEADER_REQ>::const_iterator it = Req.begin(); it != Req.end(); ++it)
			if (!it->ContentList.empty() && (it->sHeader.Find(sAmzMetaPrefix) == 0))
				AddHeader(it->sHeader, it->ContentList.front());
		// add in any new tags
		for (list<HEADER_STRUCT>::const_iterator itList = MDList.begin(); itList != MDList.end(); ++itList)
			AddHeader(itList->sHeader, itList->sContents);
		// delete tags
		if (pDeleteTagParam != nullptr)
		{
			for (list<CString>::const_iterator itDel = pDeleteTagParam->begin(); itDel != pDeleteTagParam->end(); ++itDel)
			{
				CString sTag(*itDel);
				sTag.MakeLower();
				(void)State.Ref->Headers.erase(sTag);
			}
		}
		AddHeader(_T("x-amz-copy-source"), UriEncode(sPath, E_URI_ENCODE::AllSAFE));							// copy it to itself
		AddHeader(_T("x-amz-metadata-directive"), _T("REPLACE"));
		// now compare with the original metadata to see if there has been a change
		Error = SendRequest(_T("PUT"), UriEncode(sPath), nullptr, 0, RetData, &Req);
		if (Error.IfError())
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return ERROR_SUCCESS;
}

// take text permission and translate it to E_S3_ACL_VALUES enum
CECSConnection::E_S3_ACL_VALUES TranslateACLText(LPCTSTR pszEntry)
{
	CString sValue(pszEntry);
	sValue.TrimLeft();
	sValue.TrimRight();
	if (sValue.CompareNoCase(_T("NONE")) == 0)
		return CECSConnection::AAV_NONE;
	if (sValue.CompareNoCase(_T("READ")) == 0)
		return CECSConnection::AAV_READ;
	if (sValue.CompareNoCase(_T("WRITE")) == 0)
		return CECSConnection::AAV_WRITE;
	if (sValue.CompareNoCase(_T("FULL_CONTROL")) == 0)
		return CECSConnection::AAV_FULL_CONTROL;
	if (sValue.CompareNoCase(_T("READ_ACP")) == 0)
		return CECSConnection::AAV_READ_ACP;
	if (sValue.CompareNoCase(_T("WRITE_ACP")) == 0)
		return CECSConnection::AAV_WRITE_ACP;
	return CECSConnection::AAV_INVALID;
}

struct XML_S3_ACL_CONTEXT
{
	deque<CECSConnection::ACL_ENTRY> *pAcls;
	CECSConnection::ACL_ENTRY Rec;
	CString sOwner;
	CString sDisplayName;
	bool bCanonicalUser;
	bool bGroup;

	XML_S3_ACL_CONTEXT()
		: pAcls(nullptr)
		, bCanonicalUser(false)
		, bGroup(false)
	{}
};

const WCHAR * const XML_S3_ACL_OWNER_ID =							L"//AccessControlPolicy/Owner/ID";
const WCHAR * const XML_S3_ACL_OWNER_DISPLAYNAME =					L"//AccessControlPolicy/Owner/DisplayName";
const WCHAR * const XML_S3_ACL_OWNER_GRANT_GRANTEE_ID =				L"//AccessControlPolicy/AccessControlList/Grant/Grantee/ID";
const WCHAR * const XML_S3_ACL_OWNER_GRANT_GRANTEE_URI =			L"//AccessControlPolicy/AccessControlList/Grant/Grantee/URI";
const WCHAR * const XML_S3_ACL_OWNER_GRANT_GRANTEE_DISPLAYNAME =	L"//AccessControlPolicy/AccessControlList/Grant/Grantee/DisplayName";
const WCHAR * const XML_S3_ACL_OWNER_GRANT_PERMISSION =				L"//AccessControlPolicy/AccessControlList/Grant/Permission";
const WCHAR * const XML_S3_ACL_OWNER_GRANT_ELEMENT =				L"//AccessControlPolicy/AccessControlList/Grant";
const WCHAR * const XML_S3_ACL_OWNER_GRANT_GRANTEE_ELEMENT =		L"//AccessControlPolicy/AccessControlList/Grant/Grantee";

HRESULT XmlS3AclContext_CB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_ACL_CONTEXT *pInfo = (XML_S3_ACL_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((psValue != nullptr) && !psValue->IsEmpty())
		{
			if (sXmlPath.CompareNoCase(XML_S3_ACL_OWNER_ID) == 0)
				pInfo->sOwner = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_ACL_OWNER_DISPLAYNAME) == 0)
				pInfo->sDisplayName = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_ACL_OWNER_GRANT_GRANTEE_ID) == 0)
				pInfo->Rec.sID = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_ACL_OWNER_GRANT_GRANTEE_URI) == 0)
				pInfo->Rec.sID = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_ACL_OWNER_GRANT_GRANTEE_DISPLAYNAME) == 0)
				pInfo->Rec.sDisplayName = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_ACL_OWNER_GRANT_PERMISSION) == 0)
				pInfo->Rec.Acl = TranslateACLText(FROM_UNICODE(*psValue));
		}
		break;
	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_S3_ACL_OWNER_GRANT_ELEMENT) == 0)
		{
			if (!pInfo->Rec.sID.IsEmpty())
			{
				pInfo->Rec.bGroup = pInfo->bGroup;
				pInfo->pAcls->push_back(pInfo->Rec);
			}
			pInfo->Rec.Clear();
		}
		break;
	case XmlNodeType_Element:
		if (sXmlPath.CompareNoCase(XML_S3_ACL_OWNER_GRANT_GRANTEE_ELEMENT) == 0)
		{
			pInfo->bCanonicalUser = false;
			pInfo->bGroup = false;

			if (pAttrList != nullptr)
			{
				for (list<XML_LITE_ATTRIB>::const_iterator itList = pAttrList->begin(); itList != pAttrList->end(); ++itList)
				{
					if (itList->sAttrName.Find(L"type") >= 0)
					{
						if (itList->sValue.CompareNoCase(L"CanonicalUser") == 0)
							pInfo->bCanonicalUser = true;
						else if (itList->sValue.CompareNoCase(L"Group") == 0)
							pInfo->bGroup = true;
					}
				}
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

// CECSConnection::ReadACL
// return the list of acl entries for the file
// it is returned in 2 lists: user acls and group acls
CECSConnection::S3_ERROR CECSConnection::ReadACL(
	LPCTSTR pszPath,
	deque<CECSConnection::ACL_ENTRY>& Acls,
	LPCTSTR pszVersion)
{
	CStateRef State(this);
	CBuffer RetData;
	S3_ERROR Error;
	try
	{
		InitHeader();
		list<HEADER_REQ> Req;
		CString sResource(UriEncode(pszPath) + L"?acl");
		CString sVersion(pszVersion);
		if (!sVersion.IsEmpty())
			sResource += _T("&versionId=") + sVersion;
		Error = SendRequest(_T("GET"), sResource, nullptr, 0, RetData, &Req);
		if (Error.IfError())
			return Error;
		// parse the output and fill the output lists
		Acls.clear();

		// parse the returned XML
		XML_S3_ACL_CONTEXT Context;
		HRESULT hr;
		Context.pAcls = &Acls;
		hr = ScanXml(&RetData, &Context, XmlS3AclContext_CB);
		if (FAILED(hr))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, hr);
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

// CECSConnection::WriteACL
// replace the list of acl entries for the file/directory
CECSConnection::S3_ERROR CECSConnection::WriteACL(
	LPCTSTR pszPath,
	const deque<CECSConnection::ACL_ENTRY>& UserAcls,
	LPCTSTR pszVersion)
{
	CStateRef State(this);
	CBuffer RetData;
	S3_ERROR Error;
	S3_SERVICE_INFO S3Info;
	try
	{
		InitHeader();

		// create XML request
		CBufferStream *pBufStream = new CBufferStream;
		CComPtr<IStream> pOutFileStream = pBufStream;
		CComPtr<IXmlWriter> pWriter;

		if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), (void**)&pWriter, nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->SetOutput(pOutFileStream)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartDocument(XmlStandalone_Omit)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"AccessControlPolicy", L"http://s3.amazonaws.com/doc/2006-03-01/")))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"Owner", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteStartElement(nullptr, L"ID", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteString(TO_UNICODE(S3Info.sOwnerID))))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

//			if (FAILED(pWriter->WriteStartElement(nullptr, L"DisplayName", nullptr)))
//				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
//			if (FAILED(pWriter->WriteString(S3Info.sOwnerDisplayName)))
//				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
//			if (FAILED(pWriter->WriteEndElement()))
//				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteEndElement()))						// end Owner
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteStartElement(nullptr, L"AccessControlList", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		for (deque<CECSConnection::ACL_ENTRY>::const_iterator it = UserAcls.begin(); it != UserAcls.end(); ++it)
		{
			if (FAILED(pWriter->WriteStartElement(nullptr, L"Grant", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteStartElement(nullptr, L"Grantee", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteAttributeString(L"xmlns", L"xsi", nullptr, L"http://www.w3.org/2001/XMLSchema-instance")))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteAttributeString(L"xsi", L"type", nullptr, L"CanonicalUser")))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteStartElement(nullptr, L"ID", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(TO_UNICODE(it->sID))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteEndElement()))						// end Grantee
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteStartElement(nullptr, L"Permission", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(TO_UNICODE(it->GetAclString()))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteEndElement()))						// end Grant
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		}

		if (FAILED(pWriter->WriteEndElement()))						// end AccessControlList
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteEndElement()))						// end AccessControlPolicy
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteEndDocument()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->Flush()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		CString sXmlOut(pBufStream->GetXml());
#ifdef _UNICODE
		CAnsiString XmlUTF8(sXmlOut, CP_UTF8);
#else
		CAnsiString XmlUTF8(sXmlOut);
#endif
		XmlUTF8.SetBufSize((DWORD)strlen(XmlUTF8));
		CString sResource(UriEncode(pszPath) + L"?acl");
		CString sVersion(pszVersion);
		if (!sVersion.IsEmpty())
			sResource += _T("&versionId=") + sVersion;
		Error = SendRequest(_T("PUT"), sResource, XmlUTF8.GetData(), XmlUTF8.GetBufSize(), RetData);
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

bool CECSConnection::TestAbort(void)
{
	CStateRef State(this);
	if (!bCheckShutdown)
		return false;
	CRWLockAcquire lock(&State.Ref->rwlAbortList, false);			// read lock
	for (list<ABORT_ENTRY>::const_iterator itList = State.Ref->AbortList.begin(); itList != State.Ref->AbortList.end(); ++itList)
	{
		if ((itList->ShutdownCB != nullptr) && (itList->ShutdownCB)(itList->pShutdownContext))
			return true;
		if (itList->pbAbort != nullptr)
		{
			bool bAbort = *(itList->pbAbort);
			if (itList->bAbortIfTrue)
			{
				if (bAbort)
					return true;
			}
			else
			{
				if (!bAbort)
					return true;
			}
		}
	}
	return false;
}

void CECSConnection::RegisterShutdownCB(TEST_SHUTDOWN_CB ShutdownParamCB, void *pContext)
{
	CStateRef State(this);
	ABORT_ENTRY Rec;
	Rec.ShutdownCB = ShutdownParamCB;
	Rec.pShutdownContext = pContext;
	CRWLockAcquire lock(&State.Ref->rwlAbortList, true);			// write lock
	State.Ref->AbortList.push_back(Rec);
}

void CECSConnection::UnregisterShutdownCB(TEST_SHUTDOWN_CB ShutdownParamCB, void *pContext)
{
	CStateRef State(this);
	CRWLockAcquire lock(&State.Ref->rwlAbortList, true);			// write lock
	for (list<ABORT_ENTRY>::iterator itList = State.Ref->AbortList.begin(); itList != State.Ref->AbortList.end(); )
	{
		if ((itList->ShutdownCB == ShutdownParamCB) && (itList->pShutdownContext == pContext))
			itList = State.Ref->AbortList.erase(itList);
		else
			++itList;
	}
}

void CECSConnection::RegisterAbortPtr(const bool *pbAbort, bool bAbortTrue)
{
	CStateRef State(this);
	ABORT_ENTRY Rec;
	Rec.pbAbort = pbAbort;
	Rec.bAbortIfTrue = bAbortTrue;
	CRWLockAcquire lock(&State.Ref->rwlAbortList, true);			// write lock
	State.Ref->AbortList.push_back(Rec);
}

void CECSConnection::UnregisterAbortPtr(const bool *pbAbort)
{
	CStateRef State(this);
	CRWLockAcquire lock(&State.Ref->rwlAbortList, true);			// write lock
	for (list<ABORT_ENTRY>::iterator itList = State.Ref->AbortList.begin(); itList != State.Ref->AbortList.end(); )
	{
		if (itList->pbAbort == pbAbort)
			itList = State.Ref->AbortList.erase(itList);
		else
			++itList;
	}
}

void CECSConnection::RegisterDisconnectCB(ECS_DISCONNECT_CB DisconnectParamCB)
{
	DisconnectCB = DisconnectParamCB;
}

void CECSConnection::SetTimeouts(
	DWORD dwWinHttpOptionConnectRetriesParam,
	DWORD dwWinHttpOptionConnectTimeoutParam,
	DWORD dwWinHttpOptionReceiveResponseTimeoutParam,
	DWORD dwWinHttpOptionReceiveTimeoutParam,
	DWORD dwWinHttpOptionSendTimeoutParam,
	DWORD dwBadIPAddrAgeParam)
{
	dwWinHttpOptionConnectRetries = dwWinHttpOptionConnectRetriesParam;
	dwWinHttpOptionConnectTimeout = dwWinHttpOptionConnectTimeoutParam;
	dwWinHttpOptionReceiveResponseTimeout = dwWinHttpOptionReceiveResponseTimeoutParam;
	dwWinHttpOptionReceiveTimeout = dwWinHttpOptionReceiveTimeoutParam;
	dwWinHttpOptionSendTimeout = dwWinHttpOptionSendTimeoutParam;
	dwBadIPAddrAge = dwBadIPAddrAgeParam;
}

void CECSConnection::SetRetries(DWORD dwMaxRetryCountParam, DWORD dwPauseBetweenRetriesParam, DWORD dwPauseAfter500ErrorParam)
{
	if (dwMaxRetryCountParam == 0)
		dwMaxRetryCount = MaxRetryCount;
	else
		dwMaxRetryCount = dwMaxRetryCountParam;
	dwPauseBetweenRetries = dwPauseBetweenRetriesParam;
	dwPauseAfter500Error = dwPauseAfter500ErrorParam;
}

void CECSConnection::SetMaxWriteRequest(DWORD dwMaxWriteRequestParam)
{
	CSingleLock lock(&csThrottleMap, true);
	list<CECSConnection *>::iterator itList;
	for (itList = ECSConnectionList.begin() ; itList != ECSConnectionList.end() ; ++itList)
	{
		if ((*itList)->sHost == sHost)
		{
			(*itList)->dwMaxWriteRequest = dwMaxWriteRequestParam;
		}
	}
}

void CECSConnection::SetMaxWriteRequestAll(DWORD dwMaxWriteRequestParam)
{
	CSingleLock lock(&csThrottleMap, true);
	list<CECSConnection *>::iterator itList;
	for (itList = ECSConnectionList.begin() ; itList != ECSConnectionList.end() ; ++itList)
	{
		(*itList)->dwMaxWriteRequest = dwMaxWriteRequestParam;
	}
}

// CheckShutdown
// set this to false if you need to do ops during a shutdown
void CECSConnection::CheckShutdown(bool bCheckShutdownParam)
{
	bCheckShutdown = bCheckShutdownParam;
}

void CECSConnection::SetDisableSecureLog(bool bDisable)
{
	CStateRef State(this);
	State.Ref->bDisableSecureLog = bDisable;
}

void CECSConnection::GetCertInfo(ECS_CERT_INFO& Rec)
{
	CStateRef State(this);
	Rec = State.Ref->CertInfo;
}

void CECSConnection::SetSaveCertInfo(bool bSave)
{
	CStateRef State(this);
	State.Ref->bSaveCertInfo = bSave;
}

DWORD CECSConnection::ChooseAuthScheme(DWORD dwSupportedSchemes)
{
//	DWORD dwMask;
//	if (RegistryGet(HKEY_LOCAL_MACHINE, LgtoavlibData.msgfmt_data.sORK_OCTOPUS, _T("ProxyAuthMask"), &dwMask) == ERROR_SUCCESS)
//		dwSupportedSchemes &= ~dwMask;

	//  It is the server's responsibility only to accept 
	//  authentication schemes that provide a sufficient level
	//  of security to protect the server's resources.
	//
	//  The client is also obligated only to use an authentication
	//  scheme that adequately protects its username and password.
	//
	//  Thus, this sample code does not use Basic authentication  
	//  because Basic authentication exposes the client's username 
	//  and password to anyone monitoring the connection.

	if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_NEGOTIATE)
		return WINHTTP_AUTH_SCHEME_NEGOTIATE;
	if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_NTLM)
		return WINHTTP_AUTH_SCHEME_NTLM;
	if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_PASSPORT)
		return WINHTTP_AUTH_SCHEME_PASSPORT;
	if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_DIGEST)
		return WINHTTP_AUTH_SCHEME_DIGEST;
	if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_BASIC)
		return WINHTTP_AUTH_SCHEME_BASIC;
	return 0;
}

// FormatAuthScheme
// output non-localized authorization scheme name
CString CECSConnection::FormatAuthScheme()
{
	CStateRef State(this);
	//  It is the server's responsibility only to accept 
	//  authentication schemes that provide a sufficient level
	//  of security to protect the server's resources.
	//
	//  The client is also obligated only to use an authentication
	//  scheme that adequately protects its username and password.
	//
	//  Thus, this sample code does not use Basic authentication  
	//  because Basic authentication exposes the client's username 
	//  and password to anyone monitoring the connection.
  
	if (TST_BIT(State.Ref->dwProxyAuthScheme, WINHTTP_AUTH_SCHEME_NEGOTIATE))
		return _T("Negotiate");
	if (TST_BIT(State.Ref->dwProxyAuthScheme, WINHTTP_AUTH_SCHEME_NTLM))
		return _T("NTLM");
	if (TST_BIT(State.Ref->dwProxyAuthScheme, WINHTTP_AUTH_SCHEME_PASSPORT))
		return _T("Passport");
	if (TST_BIT(State.Ref->dwProxyAuthScheme, WINHTTP_AUTH_SCHEME_DIGEST))
		return _T("Digest");
	if (TST_BIT(State.Ref->dwProxyAuthScheme, WINHTTP_AUTH_SCHEME_BASIC))
		return _T("Basic");
	return _T("");
}

DWORD CECSConnection::GetSecureError(void)
{
	CStateRef State(this);
	return State.Ref->dwSecureError;
}

CString CECSConnection::signS3ShareableURL(CString& sResource, const CString& sExpire, const CString& sHostPort)
{
	CStateRef State(this);
	CString sSignature;

	if (!IfS3v4())
		sSignature = signRequestS3v2(sSecret, _T("GET"), sResource, State.Ref->Headers, sExpire);
	else
	{
		// we need to create a new signing key so that it can go for the 7 day maximum
		{
			CSimpleRWLockAcquire lockRead(&lwrS3V4SigningKey, true);				// get write lock
			GlobalS3V4SigningKey.Empty();
			ZeroFT(ftS3V4SigningKey);
		}
		InitHeader();
		AddHeader(_T("host"), sHostPort);
		FILETIME ftRequestTime;
		SYSTEMTIME stRequestTime;
		CBuffer S3SigningKey;
		// remove date header if it exists
		(void)State.Ref->Headers.erase(_T("date"));
		(void)State.Ref->Headers.erase(_T("x-amz-date"));
		GetSystemTimeAsFileTime(&ftRequestTime);
		FileTimeToSystemTime(&ftRequestTime, &stRequestTime);
		CString sQueryString, sCredential;
		sCredential.Format(_T("%s/%04d%02d%02d/%s/s3/aws4_request"),
			(LPCTSTR)sS3KeyID, stRequestTime.wYear, stRequestTime.wMonth, stRequestTime.wDay, (LPCTSTR)sS3Region);
		sQueryString = CString(_T("?X-Amz-Algorithm=AWS4-HMAC-SHA256"))
			+ _T("&X-Amz-Credential=") + UriEncode(sCredential, E_URI_ENCODE::V4AuthSlash)
			+ _T("&X-Amz-Date=") + FormatISO8601Date(ftRequestTime, false, false, true)
			+ _T("&X-Amz-Expires=") + sExpire
			+ _T("&X-Amz-SignedHeaders=host");
		sResource += sQueryString;
		(void)signRequestS3v4(sSecret, _T("GET"), sResource,
			State.Ref->Headers, nullptr, 0, E_S3_V4_PAYLOAD::Unsigned, 0, 0, S3SigningKey, sSignature, stRequestTime);
	}
	return sSignature;
}

CString CECSConnection::GenerateShareableURL(
	LPCTSTR pszPath,									// translated path to object
	SYSTEMTIME *pstExpire)								// must be LOCAL time
{
	CString sPath(pszPath), sURL;
	try
	{
		CString sIP;
		{
			CSimpleRWLockAcquire lock(&rwlIPListHost, false);			// read lock
			if (IPListHost.empty())
				return _T("");
			sIP = IPListHost[0];
		}
		CTime Time(*pstExpire);
		CString sExpire;
		if (IfS3v4())
		{
			CTime CurTime = CTime::GetCurrentTime();
			__time64_t Duration = Time.GetTime() - CurTime.GetTime();
			if (Duration > 604800)
				Duration = 604800LL;
			sExpire = FmtNum(Duration);
		}
		else
			sExpire = FmtNum(Time.GetTime());
		CString sSignature;
		if (bSSL)
			sURL = _T("https:");
		else
			sURL = _T("http:");
		CString sPort;
		if ((bSSL && (Port != INTERNET_DEFAULT_HTTPS_PORT))
				|| (!bSSL && (Port != INTERNET_DEFAULT_HTTP_PORT)))
			sPort = _T(":") + FmtNum(Port);
		CString sResource = UriEncode(sPath);
		if (!IfS3v4())
		{
			sSignature = UriEncode(signS3ShareableURL(sResource, sExpire, sIP + sPort), E_URI_ENCODE::V4AuthSlash);
			CString sURLPart(sIP + sPort + sResource + _T("?AWSAccessKeyId=") + UriEncode(sS3KeyID) + _T("&Expires=") + sExpire + _T("&Signature"));
			sURLPart = EncodeSpecialChars(sURLPart) + _T("=") + sSignature;
			sURL += _T("//") + sURLPart;
		}
		else
		{
			sSignature = UriEncode(signS3ShareableURL(sResource, sExpire, sIP + sPort), E_URI_ENCODE::V4AuthSlash);
			sURL += _T("//") + sIP + sPort + sResource + _T("&X-Amz-Signature");
			sURL = EncodeSpecialChars(sURL) + _T("=") + sSignature;
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		(void)E;
		return _T("");
	}
	return sURL;
}

void CECSConnection::RemoveACLDups(deque<CECSConnection::ACL_ENTRY>& AclList)
{
	bool bRetry;
	do
	{
		bRetry = false;
		for (deque<CECSConnection::ACL_ENTRY>::iterator itOuter=AclList.begin() ; !bRetry && (itOuter!=AclList.end()) ; ++itOuter)
		{
			for (deque<CECSConnection::ACL_ENTRY>::iterator itInner=itOuter + 1 ; itInner!=AclList.end() ; ++itInner)
			{
				if (itOuter->sID == itInner->sID)
				{
					// if both READ and WRITE exist, keep both
					// otherwise, keep only the one with the most permissions
					if (((itOuter->Acl == AAV_READ) && (itInner->Acl == AAV_WRITE))
						|| ((itOuter->Acl == AAV_WRITE) && (itInner->Acl == AAV_READ)))
					{
					}
					else
					{
						if ((int)itOuter->Acl < (int)itInner->Acl)
							itOuter->Acl = itInner->Acl;
						// delete the later one (inner)
						(void)AclList.erase(itInner);
						bRetry = true;
						break;
					}
				}
			}
			if (bRetry)
				break;
		}
	} while (bRetry);
}

bool CECSConnection::IfValidMetadataTag(LPCTSTR pszMDString)
{
	CString sStr(pszMDString);
	if (sStr.FindOneOf(_T("=,")) >= 0)
		return false;
	for (int i=0 ; i<sStr.GetLength() ; i++)
	{
		// if UNICODE, don't allow
		if (sStr[i] > 0x100)
			return false;
		if (iscntrl(sStr[i]))
			return false;
	}
	return true;
}

CECSConnection::CECSConnectionSession::CECSConnectionSession()
	: pValue(nullptr)
{}

void CECSConnection::CECSConnectionSession::AllocSession(LPCTSTR pszHost, LPCTSTR pszIP)
{
	CSingleLock lock(&csSessionMap, true);
	ReleaseSession();
	Key.sHostEntry = pszHost;
	Key.sIP = pszIP;
	Key.lKey = 0;
	map<SESSION_MAP_KEY, SESSION_MAP_VALUE>::iterator itMap;
	for (itMap = SessionMap.lower_bound(Key)
		; itMap != SessionMap.end()
		; ++itMap)
	{
		if ((Key.sHostEntry != itMap->first.sHostEntry)
			|| (Key.sIP != itMap->first.sIP))
			break;
		if (!itMap->second.bInUse)
		{
			// claim it for us!
			itMap->second.bInUse = true;
			Key = itMap->first;					// save the key so we can release it
			pValue = &itMap->second;
			return;
		}
	}
	// there wasn't any entry. create one
	Key.lKey = InterlockedIncrement(&lSessionKeyValue);
	if (Key.lKey == 0)
		Key.lKey = InterlockedIncrement(&lSessionKeyValue);
	pair<map<SESSION_MAP_KEY, SESSION_MAP_VALUE>::iterator, bool> ret = SessionMap.insert(make_pair(Key, SESSION_MAP_VALUE()));
	ASSERT(ret.second);
	ret.first->second.bInUse = true;
	Key = ret.first->first;					// save the key so we can release it
	pValue = &ret.first->second;
}

CECSConnection::CECSConnectionSession::~CECSConnectionSession()
{
	ReleaseSession();
	pValue = nullptr;
}

void CECSConnection::CECSConnectionSession::ReleaseSession(void) throw()
{
	if (pValue != nullptr)
	{
		CSingleLock lock(&csSessionMap, true);
		map<SESSION_MAP_KEY, SESSION_MAP_VALUE>::iterator itMap = SessionMap.find(Key);
		if (itMap != SessionMap.end())
		{
			if (!itMap->second.bInUse)
				ASSERT(itMap->second.bInUse);
			if (itMap->second.bKillWhenDone)
				(void)SessionMap.erase(itMap);
			else
			{
				GetSystemTimeAsFileTime(&itMap->second.ftIdleTime);
				itMap->second.bInUse = false;
			}
		}
	}
	Key.sHostEntry.Empty();
	Key.sIP.Empty();
	Key.lKey = 0;
	pValue = nullptr;
}

CString CECSConnection::CECSConnectionSession::Format(void) const
{
	return Key.Format() + _T(": ") + ((pValue != nullptr) ? (LPCTSTR)pValue->Format() : _T(""));
}

// invalidate all entries for this host
void CECSConnection::KillHostSessions()
{
	CSingleLock lock(&csSessionMap, true);
	map<SESSION_MAP_KEY, SESSION_MAP_VALUE>::iterator itMap;
	for (itMap = SessionMap.begin()
		; itMap != SessionMap.end()
		;)
	{
		if (!itMap->second.bInUse)
		{
			// not currently in use - delete it
			itMap = SessionMap.erase(itMap);
		}
		else
		{
			itMap->second.bKillWhenDone = true;
			++itMap;
		}
	}
}

void CECSConnection::GarbageCollect()
{
	FILETIME ftNow;
	GetSystemTimeAsFileTime(&ftNow);
	{
		FILETIME ftExpire = ftNow - FT_HOURS(1);
		CSingleLock lock(&csSessionMap, true);
		map<SESSION_MAP_KEY, SESSION_MAP_VALUE>::iterator itMap;
		for (itMap = SessionMap.begin()
			; itMap != SessionMap.end()
			;)
		{
			if (!itMap->second.bInUse && (ftExpire > itMap->second.ftIdleTime))
				itMap = SessionMap.erase(itMap);
			else
				++itMap;
		}
	}
	{
		FILETIME ftExpire = ftNow - FT_MINUTES(2);						// any entries not touched for a while are removed
		CSingleLock lockThrottle(&csThrottleMap, true);
		for (list<CECSConnection*>::const_iterator itConn = ECSConnectionList.begin(); itConn != ECSConnectionList.end(); ++itConn)
		{
			CSimpleRWLockAcquire lockState(&(*itConn)->StateList.rwlStateMap, true);			// write lock
			for (map<DWORD, shared_ptr<CECSConnectionState>>::iterator itMap = (*itConn)->StateList.StateMap.begin(); itMap != (*itConn)->StateList.StateMap.end(); )
			{
				if ((InterlockedAdd(&itMap->second->ulReferenceCount, 0) == 0) && !IfFTZero(itMap->second->ftLastUsed) && (itMap->second->ftLastUsed < ftExpire))
					itMap = (*itConn)->StateList.StateMap.erase(itMap);
				else
					++itMap;
			}
		}
	}
}

// CECSConnection::ValidateS3BucketName
// returns false if the name does not conform to all the rules for a bucket name
bool CECSConnection::ValidateS3BucketName(LPCTSTR pszBucketName)
{
	CString sBucket(pszBucketName);
	// verify that the bucket name is valid
	//		Bucket names must be at least 3 and no more than 63 characters long.
	//		Bucket names must be a series of one or more labels.Adjacent labels are separated by a single period(.).Bucket names can contain lowercase letters, numbers, and hyphens.Each label must start and end with a lowercase letter or a number.
	//		Bucket names must not be formatted as an IP address(e.g., 192.168.5.4).
	//		Bucket name cannot start with a period(.).
	//		Bucket name cannot end with a period(.).
	//		There can be only one period between labels.
	if ((sBucket.GetLength() < 3)
		|| (sBucket.GetLength() > 63)
		|| (sBucket[0] == _T('.'))
		|| (sBucket[sBucket.GetLength() - 1] == _T('.'))
		|| (sBucket.Find(_T("..")) >= 0))
		return false;
	for (int i = 0; i < sBucket.GetLength(); i++)
	{
		TCHAR ch = sBucket[i];
		if ((ch != _T('.'))
			&& (ch != _T('-'))
			&& !iswdigit(ch)
			&& !iswlower(ch)
			&& !iswupper(ch)			// relax rules - allow upper case
			&& (ch != _T('_')))			// relax rules - allow underscore
			return false;
	}
	// verify that the name doesn't look like an IP address
	// since we already know that there is no '.' at the beginning or end, and there are not 2 dots in a row
	// all we need to do is see if there are 3 total dots
	// and the rest are all numeric
	int iDots = 0;
	bool bAllNum = true;
	for (int i = 0; i < sBucket.GetLength(); i++)
	{
		TCHAR ch = sBucket[i];
		if (ch == _T('.'))
			iDots++;
		if (!iswdigit(ch))
			bAllNum = true;
	}
	if (bAllNum && (iDots == 3))
		return false;					// looks like nnn.nnn.nnn.nnn, where 'nnn' is any number
	return true;
}

CECSConnection::S3_ERROR CECSConnection::CreateS3Bucket(
	LPCTSTR pszBucketName,
	const S3_BUCKET_OPTIONS *pOptions,
	const list<CECSConnection::HEADER_STRUCT> *pMDList)
{
	CStateRef State(this);
	CBuffer RetData;
	S3_ERROR Error;

	try
	{
		if (!ValidateS3BucketName(pszBucketName))
		{
			Error.S3Error = S3_ERROR_InvalidBucketName;
			Error.dwHttpError = HTTP_STATUS_NOT_FOUND;
			return Error;
		}
		// get the list of buckets. maybe it is already created
		S3_SERVICE_INFO ServiceInfo;
		Error = S3ServiceInformation(ServiceInfo);
		if (Error.IfError())
			return Error;
		for (list<S3_BUCKET_INFO>::const_iterator itList = ServiceInfo.BucketList.begin(); itList != ServiceInfo.BucketList.end(); ++itList)
		{
			if (itList->sName == pszBucketName)
			{
				Error.S3Error = S3_ERROR_BucketAlreadyExists;
				Error.dwHttpError = HTTP_STATUS_NOT_FOUND;
				return Error;
			}
		}
		InitHeader();
		AddHeader(_T("Content-Length"), _T("0"));
		if (pOptions != nullptr)
		{
			if (pOptions->dwRetention != 0)
				AddHeader(_T("x-emc-retention-period"), FmtNum(pOptions->dwRetention));
			AddHeader(_T("x-emc-file-system-access-enabled"), pOptions->bEnableFS ? _T("true") : _T("false"));
			AddHeader(_T("x-emc-is-stale-allowed"), pOptions->bTempSiteOutage ? _T("true") : _T("false"));
			CString sIndexFields;
			for (list<CECSConnection::S3_BUCKET_INDEX_ENTRY>::const_iterator it = pOptions->IndexFieldList.begin();
				it != pOptions->IndexFieldList.end(); ++it)
			{
				if (SystemMDSet.find(it->sFieldName) != SystemMDSet.end())
				{
					if (!sIndexFields.IsEmpty())
						sIndexFields += _T(",");
					sIndexFields += it->sFieldName;
				}
				else
				{
					if ((it->sFieldName.Left(sAmzMetaPrefix.GetLength()) == sAmzMetaPrefix)		// it must start with x-amz-meta-
						&& (it->sFieldName.GetLength() > sAmzMetaPrefix.GetLength())			// has to be more than just x-amz-meta-
						&& (it->Type != E_MD_SEARCH_TYPE::Unknown))								// a type is needed
					{
						if (!sIndexFields.IsEmpty())
							sIndexFields += _T(",");
						sIndexFields += it->sFieldName + _T(";") + TranslateSearchFieldType(it->Type);
					}
					else
					{
						Error.S3Error = S3_ERROR_MetadataSearchNotEnabled;
						Error.dwHttpError = HTTP_STATUS_BAD_REQUEST;
						Error.sDetails = _T("Invalid search field: ") + it->sFieldName + _T(" Type: ") + TranslateSearchFieldType(it->Type);
						return Error;
					}
				}
			}
			if (!sIndexFields.IsEmpty())
				AddHeader(_T("x-emc-metadata-search"), sIndexFields);
		}

		// set location constraint if any other region except for us-east-1
		CAnsiString XmlUTF8;
		if (!sS3Region.IsEmpty() && (sS3Region != _T("us-east-1")))
		{
			// create XML request
			CBufferStream *pBufStream = new CBufferStream;
			CComPtr<IStream> pOutFileStream = pBufStream;
			CComPtr<IXmlWriter> pWriter;

			if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), (void**)&pWriter, nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->SetOutput(pOutFileStream)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteStartDocument(XmlStandalone_Omit)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteStartElement(nullptr, _T("CreateBucketConfiguration"), _T("http://s3.amazonaws.com/doc/2006-03-01/"))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteStartElement(nullptr, _T("LocationConstraint"), nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(TO_UNICODE(sS3Region))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteFullEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteFullEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteEndDocument()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->Flush()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			CString sXmlOut(pBufStream->GetXml());
#ifdef _UNICODE
			CAnsiString XmlUTF8_east(sXmlOut, CP_UTF8);
#else
			CAnsiString XmlUTF8_east(sXmlOut);
#endif
			XmlUTF8_east.SetBufSize((DWORD)strlen(XmlUTF8_east));
			XmlUTF8 = XmlUTF8_east;
		}
		if (pMDList != nullptr)
		{
			for (list<CECSConnection::HEADER_STRUCT>::const_iterator itMD = pMDList->begin(); itMD != pMDList->end(); ++itMD)
				AddHeader(itMD->sHeader, itMD->sContents);
		}
		Error = SendRequest(_T("PUT"), CString(_T("/")) + pszBucketName + _T("/"), XmlUTF8.GetData(), XmlUTF8.GetBufSize(), RetData);
		if (Error.IfError())
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}


CECSConnection::S3_ERROR CECSConnection::DeleteS3Bucket(LPCTSTR pszBucketName)
{
	CStateRef State(this);
	CBuffer RetData;
	S3_ERROR Error;

	try
	{
		InitHeader();
		Error = SendRequest(_T("DELETE"), CString(_T("/")) + pszBucketName + _T("/"), nullptr, 0, RetData);
		if (Error.IfError())
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

// parse response from multipart initiate:
//<InitiateMultipartUploadResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
//  <Bucket>example-bucket</Bucket>
//  <Key>example-object</Key>
//  <UploadId>VXBsb2FkIElEIGZvciA2aWWpbmcncyBteS1tb3ZpZS5tMnRzIHVwbG9hZA</UploadId>
//</InitiateMultipartUploadResult>
const WCHAR * const XML_MULTI_PART_BUCKET = L"//InitiateMultipartUploadResult/Bucket";
const WCHAR * const XML_MULTI_PART_KEY = L"//InitiateMultipartUploadResult/Key";
const WCHAR * const XML_MULTI_PART_UPLOAD_ID = L"//InitiateMultipartUploadResult/UploadId";

struct XML_MULTI_PART_CONTEXT
{
	CECSConnection::S3_UPLOAD_PART_INFO *pMultiPartInfo;
};

HRESULT XmlMultiPartCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_MULTI_PART_CONTEXT *pInfo = (XML_MULTI_PART_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (psValue != nullptr)
		{
			if (sXmlPath.CompareNoCase(XML_MULTI_PART_BUCKET) == 0)
			{
				pInfo->pMultiPartInfo->sBucket = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_MULTI_PART_KEY) == 0)
			{
				pInfo->pMultiPartInfo->sKey = FROM_UNICODE(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_MULTI_PART_UPLOAD_ID) == 0)
			{
				pInfo->pMultiPartInfo->sUploadId = FROM_UNICODE(*psValue);
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

// S3 multipart upload support
CECSConnection::S3_ERROR CECSConnection::S3MultiPartInitiate(LPCTSTR pszPath, S3_UPLOAD_PART_INFO& MultiPartInfo, const list<HEADER_STRUCT> *pMDList)
{
	CStateRef State(this);
	CECSConnection::S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	MultiPartInfo.sResource = pszPath;
	if (MultiPartInfo.sResource.IsEmpty())
		return ERROR_INVALID_NAME;
	// add in all metadata
	if (pMDList != nullptr)
	{
		for (list<HEADER_STRUCT>::const_iterator itList = pMDList->begin(); itList != pMDList->end(); ++itList)
		{
			AddHeader(itList->sHeader, itList->sContents);
		}
	}
	AddHeader(_T("content-type"), _T("application/octet-stream"));
	Error = SendRequest(_T("POST"), UriEncode(MultiPartInfo.sResource) + _T("?uploads"), nullptr, 0, RetData);
	if (Error.IfError())
		return Error;
	// parse XML
	XML_MULTI_PART_CONTEXT Context;
	Context.pMultiPartInfo = &MultiPartInfo;
	HRESULT hr;
	hr = ScanXml(&RetData, &Context, XmlMultiPartCB);
	if (FAILED(hr))
		return hr;
	return ERROR_SUCCESS;
}

struct XML_MPU_COMPLETE_CONTEXT
{
	CECSConnection::S3_MPU_COMPLETE_INFO *pMPUComplete;
	XML_MPU_COMPLETE_CONTEXT()
		: pMPUComplete(nullptr)
	{}
};

const WCHAR * const XML_MPU_COMPLETE_LOCATION = L"//CompleteMultipartUploadResult/Location";
const WCHAR * const XML_MPU_COMPLETE_BUCKET = L"//CompleteMultipartUploadResult/Bucket";
const WCHAR * const XML_MPU_COMPLETE_KEY = L"//CompleteMultipartUploadResult/Key";
const WCHAR * const XML_MPU_COMPLETE_ETAG = L"//CompleteMultipartUploadResult/ETag";

HRESULT XmlMPUCompleteCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_MPU_COMPLETE_CONTEXT *pInfo = (XML_MPU_COMPLETE_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((psValue != nullptr) && !psValue->IsEmpty())
		{
			if (sXmlPath.CompareNoCase(XML_MPU_COMPLETE_LOCATION) == 0)
				pInfo->pMPUComplete->sLocation = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_MPU_COMPLETE_BUCKET) == 0)
				pInfo->pMPUComplete->sBucket = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_MPU_COMPLETE_KEY) == 0)
				pInfo->pMPUComplete->sKey = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_MPU_COMPLETE_ETAG) == 0)
				pInfo->pMPUComplete->sETag = FROM_UNICODE(*psValue);
		}
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::S3MultiPartComplete(
	const S3_UPLOAD_PART_INFO& MultiPartInfo,
	const list<shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY>>& PartList,
	S3_MPU_COMPLETE_INFO& MPUCompleteInfo)
{
	CStateRef State(this);
	CECSConnection::S3_ERROR Error;
	CBuffer RetData;
	try
	{
		// create XML request
		//<CompleteMultipartUpload>
		//  <Part>
		//    <PartNumber>PartNumber</PartNumber>
		//    <ETag>ETag</ETag>
		//  </Part>
		//  ...
		//</CompleteMultipartUpload>
		CBufferStream *pBufStream = new CBufferStream;
		CComPtr<IStream> pOutFileStream = pBufStream;
		CComPtr<IXmlWriter> pWriter;

		if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), (void**)&pWriter, nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->SetOutput(pOutFileStream)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartDocument(XmlStandalone_Omit)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"CompleteMultipartUpload", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		for (list<shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY>>::const_iterator itList = PartList.begin(); itList != PartList.end(); ++itList)
		{
			if (FAILED(pWriter->WriteStartElement(nullptr, L"Part", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteStartElement(nullptr, L"PartNumber", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(TO_UNICODE(FmtNum((*itList)->uPartNum)))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteStartElement(nullptr, L"ETag", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(TO_UNICODE((*itList)->sETag))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		}
		if (FAILED(pWriter->WriteEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteEndDocument()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->Flush()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		CString sXmlOut(pBufStream->GetXml());
#ifdef _UNICODE
		CAnsiString XmlUTF8(sXmlOut, CP_UTF8);
#else
		CAnsiString XmlUTF8(sXmlOut);
#endif
		XmlUTF8.SetBufSize((DWORD)strlen(XmlUTF8));

		InitHeader();
		Error = SendRequest(_T("POST"), UriEncode(MultiPartInfo.sResource) + _T("?uploadId=") + MultiPartInfo.sUploadId, XmlUTF8.GetData(), XmlUTF8.GetBufSize(), RetData);
		if (!Error.IfError() && !RetData.IsEmpty())
		{
			{
				// even though no error was returned, this is a very special call that may still fail
				XML_ECS_ERROR_CONTEXT Context;
				Context.pError = &Error;
				(void)ScanXml(&RetData, &Context, XmlS3ErrorCB);
			}
			if (!Error.sS3Code.IsEmpty())
			{
				Error.dwHttpError = HTTP_STATUS_BAD_REQUEST;			// make sure this is recognized as a failure
			}
			else
			{
				// parse successful XML response
				XML_MPU_COMPLETE_CONTEXT Context;
				Context.pMPUComplete = &MPUCompleteInfo;
				(void)ScanXml(&RetData, &Context, XmlMPUCompleteCB);
			}
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		Error = E.Error;
	}
	return Error;
}

CECSConnection::S3_ERROR CECSConnection::S3MultiPartAbort(const S3_UPLOAD_PART_INFO& MultiPartInfo)
{
	CECSConnection::S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	Error = SendRequest(_T("DELETE"), UriEncode(MultiPartInfo.sResource) + _T("?uploadId=") + MultiPartInfo.sUploadId, nullptr, 0, RetData);
	return Error;
}

const WCHAR * const XML_S3_MULTIPART_LIST_BUCKET =							L"//ListMultipartUploadsResult/Bucket";
const WCHAR * const XML_S3_MULTIPART_LIST_NEXTKEYMARKER=					L"//ListMultipartUploadsResult/NextKeyMarker";
const WCHAR * const XML_S3_MULTIPART_LIST_NEXTUPLOADIDMARKER =				L"//ListMultipartUploadsResult/NextUploadIdMarker";
const WCHAR * const XML_S3_MULTIPART_LIST_MAXUPLOADS =						L"//ListMultipartUploadsResult/MaxUploads";
const WCHAR * const XML_S3_MULTIPART_LIST_ISTRUNCATED =						L"//ListMultipartUploadsResult/IsTruncated";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_ELEMENT =					L"//ListMultipartUploadsResult/Upload";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_KEY =						L"//ListMultipartUploadsResult/Upload/Key";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_UPLOADID =					L"//ListMultipartUploadsResult/Upload/UploadId";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_INITIATOR_ID =				L"//ListMultipartUploadsResult/Upload/Initiator/ID";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_INITIATOR_DISPLAYNAME =	L"//ListMultipartUploadsResult/Upload/Initiator/DisplayName";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_OWNER_ID =					L"//ListMultipartUploadsResult/Upload/Owner/ID";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_OWNER_DISPLAYNAME =		L"//ListMultipartUploadsResult/Upload/Owner/DisplayName";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_STORAGECLASS =				L"//ListMultipartUploadsResult/Upload/StorageClass";
const WCHAR * const XML_S3_MULTIPART_LIST_UPLOAD_INITIATED =				L"//ListMultipartUploadsResult/Upload/Initiated";

struct S3_MULTIPART_LIST_CONTEXT
{
	CECSConnection::S3_LIST_MULTIPART_UPLOADS *pMultiPartList;
	CECSConnection::S3_LIST_MULTIPART_UPLOADS_ENTRY Rec;
	CString sNextKeyMarker;
	CString sNextUploadIdMarker;
	bool bIsTruncated;

	S3_MULTIPART_LIST_CONTEXT()
		: pMultiPartList(nullptr)
		, bIsTruncated(false)
	{}
};

HRESULT XmlS3MultiPartListCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	S3_MULTIPART_LIST_CONTEXT *pInfo = (S3_MULTIPART_LIST_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((psValue != nullptr) && !psValue->IsEmpty())
		{
			if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_BUCKET) == 0)
				pInfo->pMultiPartList->sBucket = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_NEXTKEYMARKER) == 0)
				pInfo->sNextKeyMarker = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_NEXTUPLOADIDMARKER) == 0)
				pInfo->sNextUploadIdMarker = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_MAXUPLOADS) == 0)
				pInfo->pMultiPartList->uMaxUploads = _ttoi(FROM_UNICODE(*psValue));
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_ISTRUNCATED) == 0)
				pInfo->bIsTruncated = FROM_UNICODE(*psValue) == _T("true");
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_KEY) == 0)
				pInfo->Rec.sKey = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_UPLOADID) == 0)
				pInfo->Rec.sUploadId = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_INITIATOR_ID) == 0)
				pInfo->Rec.sInitiatorId = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_INITIATOR_DISPLAYNAME) == 0)
				pInfo->Rec.sInitiatorDisplayName = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_OWNER_ID) == 0)
				pInfo->Rec.sOwnerId = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_OWNER_DISPLAYNAME) == 0)
				pInfo->Rec.sOwnerDisplayName = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_STORAGECLASS) == 0)
				pInfo->Rec.sStorageClass = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_INITIATED) == 0)
			{
				CECSConnection::S3_ERROR Error = CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->Rec.ftInitiated);
				if (Error.IfError())
					return Error.dwError;
			}
		}
		break;

	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_S3_MULTIPART_LIST_UPLOAD_ELEMENT) == 0)
		{
			if (!pInfo->Rec.sKey.IsEmpty())
			{
				pInfo->pMultiPartList->ObjectList.push_back(pInfo->Rec);
				pInfo->Rec.EmptyRec();
			}
		}
		break;
	default:
		break;
	}
	return 0;
}

// get a list of all multipart uploads currently active
CECSConnection::S3_ERROR CECSConnection::S3MultiPartList(LPCTSTR pszBucketName, S3_LIST_MULTIPART_UPLOADS& MultiPartList)
{
	CStateRef State(this);
	CECSConnection::S3_ERROR Error;
	S3_MULTIPART_LIST_CONTEXT Context;
	CBuffer RetData;

	Context.pMultiPartList = &MultiPartList;
	InitHeader();
	CString sResource(CString(_T("/")) + pszBucketName + _T("/?uploads"));
	for (;;)
	{
		CString sTempResource(sResource);
		if (!Context.sNextKeyMarker.IsEmpty())
			sTempResource += _T("&key-marker=") + UriEncode(Context.sNextKeyMarker, E_URI_ENCODE::AllSAFE);
		if (!Context.sNextUploadIdMarker.IsEmpty())
			sTempResource += _T("&upload-id-marker=") + Context.sNextUploadIdMarker;
		Error = SendRequest(_T("GET"), sTempResource, nullptr, 0, RetData);
		if (Error.IfError())
			return Error;
		// parse XML
		HRESULT hr;
		hr = ScanXml(&RetData, &Context, XmlS3MultiPartListCB);
		if (FAILED(hr))
			return hr;
		if (!Context.bIsTruncated)
			break;
	}
	return Error;
}

struct XML_MULTIPART_COPY_CONTEXT
{
	CString sETag;
};

const WCHAR * const XML_MULTIPART_COPY_ETAG = L"//CopyPartResult/ETag";

HRESULT XmlMultipartCopyCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_MULTIPART_COPY_CONTEXT *pInfo = (XML_MULTIPART_COPY_CONTEXT *)pContext;
	if (pInfo == nullptr)
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (sXmlPath.CompareNoCase(XML_MULTIPART_COPY_ETAG) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->sETag = FROM_UNICODE(*psValue);
		}
		break;
	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::S3MultiPartUpload(
	const S3_UPLOAD_PART_INFO& MultiPartInfo,		// (in) info from multipart initiate
	S3_UPLOAD_PART_ENTRY& PartEntry,				// (in, out) get part number, set ETag if successful
	STREAM_CONTEXT *pStreamSend,					// (in) context for data (null if copy)
	ULONGLONG ullTotalLen,							// (in) total length of data
	LPCTSTR pszCopySource,							// (in) optional - source object to copy from
	ULONGLONG ullStartRange,						// (in) if pszCopySource, this is the start of the range to copy
	LPCTSTR pszVersionId)							// (in) if pszCopySource, nonNULL: version ID to copy
{
	CStateRef State(this);
	PartEntry.sETag.Empty();
	CECSConnection::S3_ERROR Error;
	list<HEADER_REQ> Req;
	CBuffer RetData;
	InitHeader();
	Req.emplace_back(_T("ETag"));
	if (!PartEntry.Checksum.IsEmpty())
		AddHeader(_T("Content-MD5"), PartEntry.Checksum.EncodeBase64());
	
	// extract the bucket
	CString sBucket;
	int iSlash = MultiPartInfo.sResource.Find(_T('/'), 1);
	sBucket = MultiPartInfo.sResource.Mid(1, iSlash - 1);
	CString sResource;
	sResource = _T('/') + sBucket + UriEncode(MultiPartInfo.sResource.Mid(iSlash));
	sResource += _T("?partNumber=") + FmtNum(PartEntry.uPartNum) + _T("&uploadId=") + MultiPartInfo.sUploadId;
	if (pszCopySource != nullptr)
	{
		CString sSource(UriEncode(pszCopySource));
		if ((pszVersionId != nullptr) && (*pszVersionId != NUL))
			sSource += CString(_T("?versionId=")) + pszVersionId;
		AddHeader(_T("x-amz-copy-source"), sSource);
		AddHeader(_T("x-amz-copy-source-range"), _T("bytes=") + FmtNum(ullStartRange) + _T("-") + FmtNum(ullStartRange + ullTotalLen - 1));
		Error = SendRequest(_T("PUT"), sResource, nullptr, 0, RetData, &Req, 0, 0, nullptr, nullptr, 0ULL);
		if (!Error.IfError())
		{
			XML_MULTIPART_COPY_CONTEXT Context;
			HRESULT hr = ScanXml(&RetData, &Context, XmlMultipartCopyCB);
			if (FAILED(hr))
				return hr;
			if (Context.sETag.IsEmpty())
				return ERROR_INVALID_DATA;
			PartEntry.sETag = Context.sETag;
		}
	}
	else
	{
		Error = SendRequest(_T("PUT"), sResource, nullptr, 0, RetData, &Req, 0, 0, pStreamSend, nullptr, ullTotalLen);
		if (!Error.IfError())
		{
			for (list<HEADER_REQ>::const_iterator it = Req.begin(); it != Req.end(); ++it)
			{
				if (it->sHeader.CompareNoCase(_T("ETag")) == 0)
				{
					if (it->ContentList.size() != 1)
						return ERROR_INVALID_DATA;
					PartEntry.sETag = it->ContentList.front();
					break;
				}
			}
			if (PartEntry.sETag.IsEmpty())
				return ERROR_INVALID_DATA;
		}
	}
	return Error;
}

struct XML_S3_VERSIONING_CONTEXT
{
	CECSConnection::E_S3_VERSIONING Versioning;
	XML_S3_VERSIONING_CONTEXT()
		: Versioning(CECSConnection::E_S3_VERSIONING::Off)
	{}
};

const WCHAR * const XML_S3_VERSIONING_STATUS = L"//VersioningConfiguration/Status";

HRESULT XmlS3VersioningCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_VERSIONING_CONTEXT *pInfo = (XML_S3_VERSIONING_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((psValue != nullptr) && !psValue->IsEmpty())
		{
			if (sXmlPath.CompareNoCase(XML_S3_VERSIONING_STATUS) == 0)
			{
				if (psValue->CompareNoCase(L"Suspended") == 0)
					pInfo->Versioning = CECSConnection::E_S3_VERSIONING::Suspended;
				else if (psValue->CompareNoCase(L"Enabled") == 0)
					pInfo->Versioning = CECSConnection::E_S3_VERSIONING::On;
				else
					return ERROR_INVALID_DATA;
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

// S3GetBucketVersioning
// return bucket versioning information
CECSConnection::S3_ERROR CECSConnection::S3GetBucketVersioning(LPCTSTR pszBucket, E_S3_VERSIONING& Versioning)
{
	CStateRef State(this);
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	Error = SendRequest(_T("GET"), CString(_T("/")) + pszBucket + _T("?versioning"), nullptr, 0, RetData, &Req);
	if (Error.IfError())
		return Error;
	if ((RetData.GetBufSize() > 5) && (RetData.GetData() != nullptr) && (strncmp((LPCSTR)RetData.GetData(), "<?xml", 5) == 0))
	{
		XML_S3_VERSIONING_CONTEXT Context;
		HRESULT hr;
		hr = ScanXml(&RetData, &Context, XmlS3VersioningCB);
		if (hr != ERROR_SUCCESS)
			return hr;
		Versioning = Context.Versioning;
	}
	else
	{
		// XML doesn't look valid. maybe we are connected to the wrong server?
		// maybe there is a man-in-middle attack?
		Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
		Error.S3Error = S3_ERROR_MalformedXML;
		Error.sS3Code = _T("MalformedXML");
		Error.sS3RequestID = _T("GET");
		Error.sS3Resource = _T("/");
	}
	return Error;
}

// S3PutBucketVersioning
// set version state for bucket
CECSConnection::S3_ERROR CECSConnection::S3PutBucketVersioning(LPCTSTR pszBucket, CECSConnection::E_S3_VERSIONING Versioning)
{
	CStateRef State(this);
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	try
	{
		// create XML request
		CBufferStream *pBufStream = new CBufferStream;
		CComPtr<IStream> pOutFileStream = pBufStream;
		CComPtr<IXmlWriter> pWriter;

		if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), (void**)&pWriter, nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->SetOutput(pOutFileStream)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartDocument(XmlStandalone_Omit)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"VersioningConfiguration", L"http://s3.amazonaws.com/doc/2006-03-01/")))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteStartElement(nullptr, L"Status", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		CString sStatus;
		if (Versioning == E_S3_VERSIONING::On)
			sStatus = _T("Enabled");
		else
			sStatus = _T("Suspended");
		if (FAILED(pWriter->WriteString(TO_UNICODE(sStatus))))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteEndDocument()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->Flush()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		CString sXmlOut(pBufStream->GetXml());
#ifdef _UNICODE
		CAnsiString XmlUTF8(sXmlOut, CP_UTF8);
#else
		CAnsiString XmlUTF8(sXmlOut);
#endif
		XmlUTF8.SetBufSize((DWORD)strlen(XmlUTF8));
		AddHeader(_T("Content-Type"), _T("application/xml"));
		Error = SendRequest(_T("PUT"), CString(_T("/")) + pszBucket + _T("?versioning"), XmlUTF8.GetData(), XmlUTF8.GetBufSize(), RetData, &Req);
	}
	catch (const CS3ErrorInfo& E)
	{
		Error = E.Error;
	}
	return Error;
}

// SetHTTPSecurityFlags
// Sets the security flags for the connection.It can be a combination of these values (OR'ed):
//
// SECURITY_FLAG_IGNORE_CERT_CN_INVALID
// Allows an invalid common name in a certificate; that is, the server name specified
// by the application does not match the common name in the certificate.
//
// SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
// Allows an invalid certificate date, that is, an expired or not- yet - effective certificate.
//
// SECURITY_FLAG_IGNORE_UNKNOWN_CA
// Allows an invalid certificate authority.
//
// SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE
// Allows the identity of a server to be established with a non - server certificate(for example, a client certificate).
void CECSConnection::SetHTTPSecurityFlags(DWORD dwHTTPSecurityFlagsParam)
{
	dwHttpSecurityFlags = dwHTTPSecurityFlagsParam;
}

CECSConnection::S3_ERROR CECSConnection::ECSAdminLogin(LPCTSTR pszUser, LPCTSTR pszPassword)
{
	INTERNET_PORT wSavePort = Port;
	SetPort(4443);
	CStateRef State(this);
	State.Ref->sHTTPUser = pszUser;
	State.Ref->sHTTPPassword = pszPassword;
	State.Ref->dwSecurityFlagsAdd = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
	State.Ref->dwSecurityFlagsSub = 0;
	CBoolSet TurnOffSignature(&State.Ref->bS3Admin);				// set this flag, and reset it on exit
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	State.Ref->Headers.clear();
	AddHeader(_T("accept"), _T("*/*"));
	CString sDate(GetCanonicalTime());
	AddHeader(_T("Date"), sDate);
	AddHeader(_T("host"), GetCurrentServerIP());
	Error = SendRequest(_T("GET"), _T("/login"), nullptr, 0, RetData, &Req);
	if (!Error.IfError())
	{
		for (list<HEADER_REQ>::const_iterator it = Req.begin(); it != Req.end(); ++it)
		{
			if ((it->sHeader.CompareNoCase(_T("X-SDS-AUTH-TOKEN")) == 0) && (it->ContentList.size() == 1))
				State.Ref->sX_SDS_AUTH_TOKEN = it->ContentList.front();
		}
		if (State.Ref->sX_SDS_AUTH_TOKEN.IsEmpty())
			Error.dwError = ERROR_INVALID_DATA;				// didn't get the auth token
	}
	SetPort(wSavePort);
	return Error;
}

CECSConnection::S3_ERROR CECSConnection::ECSAdminLogout()
{
	INTERNET_PORT wSavePort = Port;
	SetPort(4443);
	CStateRef State(this);
	State.Ref->dwSecurityFlagsAdd = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
	State.Ref->dwSecurityFlagsSub = 0;
	CBoolSet TurnOffSignature(&State.Ref->bS3Admin);				// set this flag, and reset it on exit
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	State.Ref->Headers.clear();
	AddHeader(_T("accept"), _T("*/*"));
	CString sDate(GetCanonicalTime());
	AddHeader(_T("Date"), sDate);
	AddHeader(_T("host"), GetCurrentServerIP());
	AddHeader(_T("X-SDS-AUTH-TOKEN"), State.Ref->sX_SDS_AUTH_TOKEN);
	Error = SendRequest(_T("GET"), _T("/logout"), nullptr, 0, RetData, &Req);
	if (!Error.IfError())
		State.Ref->sX_SDS_AUTH_TOKEN.Empty();
	SetPort(wSavePort);
	return Error;
}

struct XML_ECS_GET_USERS_CONTEXT
{
	CECSConnection::S3_ADMIN_USER_INFO User;
	list<CECSConnection::S3_ADMIN_USER_INFO> *pUserList;
	XML_ECS_GET_USERS_CONTEXT()
		: pUserList(nullptr)
	{}
};

const WCHAR * const XML_ECS_GET_USERS_USERID = L"//users/blobuser/userid";
const WCHAR * const XML_ECS_GET_USERS_NAMESPACE = L"//users/blobuser/namespace";
const WCHAR * const XML_ECS_GET_USERS_ELEMENT = L"//users/blobuser";

HRESULT XmlECSGetUsersCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_ECS_GET_USERS_CONTEXT *pInfo = (XML_ECS_GET_USERS_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (sXmlPath.CompareNoCase(XML_ECS_GET_USERS_USERID) == 0)
		{
			pInfo->User.sUser = FROM_UNICODE(*psValue);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_GET_USERS_NAMESPACE) == 0)
		{
			pInfo->User.sNamespace = FROM_UNICODE(*psValue);
		}
		break;
	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_ECS_GET_USERS_ELEMENT) == 0)
		{
			if (!pInfo->User.IsEmpty())
			{
				pInfo->pUserList->push_back(pInfo->User);
				pInfo->User.EmptyRec();
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::ECSAdminGetUserList(list<S3_ADMIN_USER_INFO>& UserList)
{
	UserList.clear();
	INTERNET_PORT wSavePort = Port;
	SetPort(4443);
	CStateRef State(this);
	State.Ref->dwSecurityFlagsAdd = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
	State.Ref->dwSecurityFlagsSub = 0;
	CBoolSet TurnOffSignature(&State.Ref->bS3Admin);				// set this flag, and reset it on exit
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	State.Ref->Headers.clear();
	AddHeader(_T("accept"), _T("*/*"));
	CString sDate(GetCanonicalTime());
	AddHeader(_T("Date"), sDate);
	AddHeader(_T("host"), GetCurrentServerIP());
	AddHeader(_T("X-SDS-AUTH-TOKEN"), State.Ref->sX_SDS_AUTH_TOKEN);
	Error = SendRequest(_T("GET"), _T("/object/users"), nullptr, 0, RetData, &Req);
	SetPort(wSavePort);
	if (!Error.IfError())
	{
		XML_ECS_GET_USERS_CONTEXT Context;
		Context.pUserList = &UserList;
		HRESULT hr;
		hr = ScanXml(&RetData, &Context, XmlECSGetUsersCB);
		if (hr != ERROR_SUCCESS)
			return hr;
	}
	return Error;
}

struct XML_ECS_CREATE_USER_CONTEXT
{
	CECSConnection::S3_ADMIN_USER_INFO User;
};

const WCHAR * const XML_ECS_CREATE_USER_SECRET = L"//user_secret_key/secret_key";
const WCHAR * const XML_ECS_CREATE_USER_KEY_TS = L"//user_secret_key/key_timestamp";
const WCHAR * const XML_ECS_CREATE_USER_KEY_EXPIRY_TS = L"//user_secret_key/key_expiry_timestamp";
const WCHAR * const XML_ECS_CREATE_USER_LINK = L"//user_secret_key/link";

HRESULT XmlECSCreateUserCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_ECS_CREATE_USER_CONTEXT *pInfo = (XML_ECS_CREATE_USER_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (sXmlPath.CompareNoCase(XML_ECS_CREATE_USER_SECRET) == 0)
		{
			pInfo->User.sSecret = FROM_UNICODE(*psValue);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_CREATE_USER_KEY_TS) == 0)
		{
			(void)CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->User.ftKeyCreate);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_CREATE_USER_KEY_EXPIRY_TS) == 0)
		{
			(void)CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->User.ftKeyExpiry);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_CREATE_USER_LINK) == 0)
		{
			pInfo->User.sLink = FROM_UNICODE(*psValue);
		}
		break;

	default:
		break;
	}
	return 0;
}

// Create ECS User
// User.sUser and sNamespace must be filled in. sTags is optional
CECSConnection::S3_ERROR CECSConnection::ECSAdminCreateUser(S3_ADMIN_USER_INFO& User)
{
	INTERNET_PORT wSavePort = Port;
	SetPort(4443);
	CStateRef State(this);
	State.Ref->dwSecurityFlagsAdd = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
	State.Ref->dwSecurityFlagsSub = 0;
	CBoolSet TurnOffSignature(&State.Ref->bS3Admin);				// set this flag, and reset it on exit
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	State.Ref->Headers.clear();
	AddHeader(_T("accept"), _T("*/*"));
	CString sDate(GetCanonicalTime());
	AddHeader(_T("Date"), sDate);
	AddHeader(_T("host"), GetCurrentServerIP());
	AddHeader(_T("X-SDS-AUTH-TOKEN"), State.Ref->sX_SDS_AUTH_TOKEN);
	AddHeader(_T("Content-Type"), _T("application/xml"));
	try
	{
		// create XML request
		CBufferStream *pBufStream = new CBufferStream;
		CComPtr<IStream> pOutFileStream = pBufStream;
		CComPtr<IXmlWriter> pWriter;

		if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), (void**)&pWriter, nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->SetOutput(pOutFileStream)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartDocument(XmlStandalone_Omit)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"user_create_param", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteStartElement(nullptr, L"user", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteString(TO_UNICODE(User.sUser))))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteStartElement(nullptr, L"namespace", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteString(TO_UNICODE(User.sNamespace))))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (!User.sTags.IsEmpty())
		{
			if (FAILED(pWriter->WriteStartElement(nullptr, L"tags", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(TO_UNICODE(User.sTags))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteFullEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		}
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteEndDocument()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->Flush()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		CString sXmlOut(pBufStream->GetXml());
#ifdef _UNICODE
		CAnsiString XmlUTF8(sXmlOut, CP_UTF8);
#else
		CAnsiString XmlUTF8(sXmlOut);
#endif
		XmlUTF8.SetBufSize((DWORD)strlen(XmlUTF8));
		Error = SendRequest(_T("POST"), _T("/object/users"), XmlUTF8.GetData(), XmlUTF8.GetBufSize(), RetData, &Req);
		SetPort(wSavePort);
		if (!Error.IfError())
		{
			XML_ECS_CREATE_USER_CONTEXT Context;
			Context.User = User;
			HRESULT hr;
			hr = ScanXml(&RetData, &Context, XmlECSCreateUserCB);
			if (hr != ERROR_SUCCESS)
				return hr;
			User = Context.User;
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		Error = E.Error;
	}
	return Error;
}

struct XML_ECS_GET_KEYS_FOR_USER_CONTEXT
{
	CECSConnection::S3_ADMIN_USER_KEY_INFO Keys;
};

const WCHAR * const XML_ECS_GET_KEYS_FOR_USER_KEY1 = L"//user_secret_key/secret_key_1";
const WCHAR * const XML_ECS_GET_KEYS_FOR_USER_TS1 = L"//user_secret_key/key_timestamp_1";
const WCHAR * const XML_ECS_GET_KEYS_FOR_USER_EXPIRY_TS1 = L"//user_secret_key/key_expiry_timestamp_1";
const WCHAR * const XML_ECS_GET_KEYS_FOR_USER_KEY2 = L"//user_secret_key/secret_key_2";
const WCHAR * const XML_ECS_GET_KEYS_FOR_USER_TS2 = L"//user_secret_key/key_timestamp_2";
const WCHAR * const XML_ECS_GET_KEYS_FOR_USER_EXPIRY_TS2 = L"//user_secret_key/key_expiry_timestamp_2";
const WCHAR * const XML_ECS_GET_KEYS_FOR_USER = L"//user_secret_key/link";

HRESULT XmlECSGetKeysForUserCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_ECS_GET_KEYS_FOR_USER_CONTEXT *pInfo = (XML_ECS_GET_KEYS_FOR_USER_CONTEXT *)pContext;
	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (sXmlPath.CompareNoCase(XML_ECS_GET_KEYS_FOR_USER_KEY1) == 0)
		{
			pInfo->Keys.sSecret1 = FROM_UNICODE(*psValue);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_GET_KEYS_FOR_USER_KEY2) == 0)
		{
			pInfo->Keys.sSecret2 = FROM_UNICODE(*psValue);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_GET_KEYS_FOR_USER_TS1) == 0)
		{
			(void)CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->Keys.ftKeyCreate1);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_GET_KEYS_FOR_USER_TS2) == 0)
		{
			(void)CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->Keys.ftKeyCreate2);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_GET_KEYS_FOR_USER_EXPIRY_TS1) == 0)
		{
			(void)CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->Keys.ftKeyExpiry1);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_GET_KEYS_FOR_USER_EXPIRY_TS2) == 0)
		{
			(void)CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->Keys.ftKeyExpiry2);
		}
		else if (sXmlPath.CompareNoCase(XML_ECS_CREATE_USER_LINK) == 0)
		{
			pInfo->Keys.sLink = FROM_UNICODE(*psValue);
		}
		break;

	default:
		break;
	}
	return 0;
}

// Create ECS User
// User.sUser and sNamespace must be filled in. sTags is optional
CECSConnection::S3_ERROR CECSConnection::ECSAdminGetKeysForUser(LPCTSTR pszUser, LPCTSTR pszNamespace, S3_ADMIN_USER_KEY_INFO& Keys)
{
	INTERNET_PORT wSavePort = Port;
	SetPort(4443);
	CStateRef State(this);
	State.Ref->dwSecurityFlagsAdd = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
	State.Ref->dwSecurityFlagsSub = 0;
	CBoolSet TurnOffSignature(&State.Ref->bS3Admin);				// set this flag, and reset it on exit
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	State.Ref->Headers.clear();
	AddHeader(_T("accept"), _T("*/*"));
	CString sDate(GetCanonicalTime());
	AddHeader(_T("Date"), sDate);
	AddHeader(_T("host"), GetCurrentServerIP());
	AddHeader(_T("X-SDS-AUTH-TOKEN"), State.Ref->sX_SDS_AUTH_TOKEN);
	try
	{
		CString sMethod(_T("/object/user-secret-keys/"));
		sMethod += pszUser;
		if ((pszNamespace != nullptr) && (*pszNamespace != NUL))
			sMethod += CString(_T("/")) + pszNamespace;
		Error = SendRequest(_T("GET"), UriEncode(sMethod, E_URI_ENCODE::AllSAFE), nullptr, 0, RetData, &Req);
		SetPort(wSavePort);
		if (!Error.IfError())
		{
			XML_ECS_GET_KEYS_FOR_USER_CONTEXT Context;
			HRESULT hr;
			hr = ScanXml(&RetData, &Context, XmlECSGetKeysForUserCB);
			if (hr != ERROR_SUCCESS)
				return hr;
			Keys = Context.Keys;
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		Error = E.Error;
	}
	return Error;
}

// ECSAdminCreateKeyForUser
// given the user, create a secret key
// User.sUser and sNamespace must be filled in
CECSConnection::S3_ERROR CECSConnection::ECSAdminCreateKeyForUser(S3_ADMIN_USER_INFO& User)
{
	INTERNET_PORT wSavePort = Port;
	SetPort(4443);
	CStateRef State(this);
	State.Ref->dwSecurityFlagsAdd = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
	State.Ref->dwSecurityFlagsSub = 0;
	CBoolSet TurnOffSignature(&State.Ref->bS3Admin);				// set this flag, and reset it on exit
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	State.Ref->Headers.clear();
	AddHeader(_T("accept"), _T("*/*"));
	CString sDate(GetCanonicalTime());
	AddHeader(_T("Date"), sDate);
	AddHeader(_T("host"), GetCurrentServerIP());
	AddHeader(_T("X-SDS-AUTH-TOKEN"), State.Ref->sX_SDS_AUTH_TOKEN);
	AddHeader(_T("Content-Type"), _T("application/xml"));
	try
	{
		// create XML request
		CBufferStream *pBufStream = new CBufferStream;
		CComPtr<IStream> pOutFileStream = pBufStream;
		CComPtr<IXmlWriter> pWriter;

		if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), (void**)&pWriter, nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->SetOutput(pOutFileStream)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartDocument(XmlStandalone_Omit)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"user_secret_key_create", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteStartElement(nullptr, L"existing_key_expiry_time_mins", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
//		if (FAILED(pWriter->WriteAttributeString(nullptr, _T("null"), nullptr, _T("true"))))
//			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteString(L"2")))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteStartElement(nullptr, L"namespace", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteString(TO_UNICODE(User.sNamespace))))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteStartElement(nullptr, L"secretkey", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteString(TO_UNICODE(User.sSecret))))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteEndDocument()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->Flush()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		CString sXmlOut(pBufStream->GetXml());
#ifdef _UNICODE
		CAnsiString XmlUTF8(sXmlOut, CP_UTF8);
#else
		CAnsiString XmlUTF8(sXmlOut);
#endif
		XmlUTF8.SetBufSize((DWORD)strlen(XmlUTF8));
		Error = SendRequest(_T("POST"), UriEncode(_T("/object/user-secret-keys/") + User.sUser, E_URI_ENCODE::AllSAFE), XmlUTF8.GetData(), XmlUTF8.GetBufSize(), RetData, &Req);
		SetPort(wSavePort);
		if (!Error.IfError())
		{
			XML_ECS_CREATE_USER_CONTEXT Context;
			Context.User = User;
			HRESULT hr;
			hr = ScanXml(&RetData, &Context, XmlECSCreateUserCB);
			if (hr != ERROR_SUCCESS)
				return hr;
			User = Context.User;
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		Error = E.Error;
	}
	return Error;
}

CECSConnection::E_MD_SEARCH_TYPE CECSConnection::TranslateSearchFieldType(LPCWSTR pszType)
{
	if (lstrcmpiW(pszType, L"String") == 0)
		return E_MD_SEARCH_TYPE::String;
	if (lstrcmpiW(pszType, L"Integer") == 0)
		return E_MD_SEARCH_TYPE::Integer;
	if (lstrcmpiW(pszType, L"Decimal") == 0)
		return E_MD_SEARCH_TYPE::Decimal;
	if (lstrcmpiW(pszType, L"Datetime") == 0)
		return E_MD_SEARCH_TYPE::Datetime;
	return E_MD_SEARCH_TYPE::Unknown;
}

CString CECSConnection::TranslateSearchFieldType(E_MD_SEARCH_TYPE Type)
{
	if (Type == E_MD_SEARCH_TYPE::String)
		return L"String";
	if (Type == E_MD_SEARCH_TYPE::Integer)
		return L"Integer";
	if (Type == E_MD_SEARCH_TYPE::Decimal)
		return L"Decimal";
	if (Type == E_MD_SEARCH_TYPE::Datetime)
		return L"Datetime";
	return L"Unknown";
}

struct XML_S3_METADATA_SEARCH_FIELDS_CONTEXT
{
	CECSConnection::S3_METADATA_SEARCH_FIELDS *pMDFields;
	CECSConnection::S3_METADATA_SEARCH_ENTRY Rec;

	XML_S3_METADATA_SEARCH_FIELDS_CONTEXT()
		: pMDFields(nullptr)
	{}
};

const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_ATTRIBUTES_ATTRIBUTE = L"//MetadataSearchList/OptionalAttributes/Attribute";
const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_ATTRIBUTES_ATTRIBUTE_NAME = L"//MetadataSearchList/OptionalAttributes/Attribute/Name";
const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_ATTRIBUTES_ATTRIBUTE_DATATYPE = L"//MetadataSearchList/OptionalAttributes/Attribute/Datatype";
const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_INDEXABLE_KEY = L"//MetadataSearchList/IndexableKeys/Key";
const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_INDEXABLE_KEY_NAME = L"//MetadataSearchList/IndexableKeys/Key/Name";
const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_INDEXABLE_KEY_DATATYPE = L"//MetadataSearchList/IndexableKeys/Key/Datatype";

HRESULT XmlS3GetMDSearchFieldsCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_METADATA_SEARCH_FIELDS_CONTEXT *pInfo = (XML_S3_METADATA_SEARCH_FIELDS_CONTEXT *)pContext;
	if ((pInfo == nullptr) || (pInfo->pMDFields == nullptr))
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_ATTRIBUTES_ATTRIBUTE_NAME) == 0)
			|| (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_INDEXABLE_KEY_NAME) == 0))
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->Rec.sName = *psValue;
		}
		else if ((sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_INDEXABLE_KEY_DATATYPE) == 0)
			|| (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_ATTRIBUTES_ATTRIBUTE_DATATYPE) == 0))
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
			{
				pInfo->Rec.Type = CECSConnection::TranslateSearchFieldType(*psValue);
			}
		}
		break;

	case XmlNodeType_Element:
		if ((sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_INDEXABLE_KEY) == 0)
			|| (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_ATTRIBUTES_ATTRIBUTE) == 0))
		{
			pInfo->Rec.sName.Empty();
			pInfo->Rec.Type = CECSConnection::E_MD_SEARCH_TYPE::Unknown;
		}
		break;
	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_INDEXABLE_KEY) == 0)
		{
			if (!pInfo->Rec.sName.IsEmpty() && (pInfo->Rec.Type != CECSConnection::E_MD_SEARCH_TYPE::Unknown))
			{
				pInfo->pMDFields->KeyList.push_back(pInfo->Rec);
			}
		}
		else if (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_ATTRIBUTES_ATTRIBUTE) == 0)
		{
			if (!pInfo->Rec.sName.IsEmpty() && (pInfo->Rec.Type != CECSConnection::E_MD_SEARCH_TYPE::Unknown))
			{
				pInfo->pMDFields->AttributeList.push_back(pInfo->Rec);
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::S3GetMDSearchFields(S3_METADATA_SEARCH_FIELDS& MDFields)
{
	CStateRef State(this);
	S3_ERROR Error;
	try
	{
		CBuffer RetData;
		InitHeader();
		CString sResource(_T("/?searchmetadata"));
		Error = SendRequest(_T("GET"), sResource, nullptr, 0, RetData);
		if (!RetData.IsEmpty())
		{
			XML_S3_METADATA_SEARCH_FIELDS_CONTEXT Context;
			Context.pMDFields = &MDFields;
			HRESULT hr = ScanXml(&RetData, &Context, XmlS3GetMDSearchFieldsCB);
			if (FAILED(hr))
				return hr;
		}
		else
		{
			// XML doesn't look valid. maybe we are connected to the wrong server?
			// maybe there is a man-in-middle attack?
			Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
			Error.S3Error = S3_ERROR_MalformedXML;
			Error.sS3Code = _T("MalformedXML");
			Error.sS3RequestID = _T("GET");
			Error.sS3Resource = sResource;
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

struct XML_S3_METADATA_SEARCH_FIELDS_BUCKET_CONTEXT
{
	CECSConnection::S3_METADATA_SEARCH_FIELDS_BUCKET *pMDFieldBucket;
	CECSConnection::S3_METADATA_SEARCH_ENTRY Rec;

	XML_S3_METADATA_SEARCH_FIELDS_BUCKET_CONTEXT()
		: pMDFieldBucket(nullptr)
	{}
};


const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_BUCKET_SEARCHENABLED = L"//MetadataSearchList/MetadataSearchEnabled";
const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_BUCKET_INDEXABLEKEYS_KEY = L"//MetadataSearchList/IndexableKeys/Key";
const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_BUCKET_INDEXABLEKEYS_KEY_NAME = L"//MetadataSearchList/IndexableKeys/Key/Name";
const WCHAR * const XML_S3_METADATA_SEARCH_FIELDS_BUCKET_INDEXABLEKEYS_KEY_DATATYPE = L"//MetadataSearchList/IndexableKeys/Key/Datatype";

HRESULT XmlS3GetMDSearchFieldsBucketCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_METADATA_SEARCH_FIELDS_BUCKET_CONTEXT *pInfo = (XML_S3_METADATA_SEARCH_FIELDS_BUCKET_CONTEXT *)pContext;
	if ((pInfo == nullptr) || (pInfo->pMDFieldBucket == nullptr))
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_BUCKET_SEARCHENABLED) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->pMDFieldBucket->bSearchEnabled = psValue->CompareNoCase(L"true") == 0;
		}
		else if (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_BUCKET_INDEXABLEKEYS_KEY_NAME) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->Rec.sName = *psValue;
		}
		else if (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_BUCKET_INDEXABLEKEYS_KEY_DATATYPE) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
			{
				pInfo->Rec.Type = CECSConnection::TranslateSearchFieldType(*psValue);
			}
		}
		break;

	case XmlNodeType_Element:
		if (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_BUCKET_INDEXABLEKEYS_KEY) == 0)
		{
			pInfo->Rec.sName.Empty();
			pInfo->Rec.Type = CECSConnection::E_MD_SEARCH_TYPE::Unknown;
		}
		break;
	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_S3_METADATA_SEARCH_FIELDS_BUCKET_INDEXABLEKEYS_KEY) == 0)
		{
			if (!pInfo->Rec.sName.IsEmpty() && (pInfo->Rec.Type != CECSConnection::E_MD_SEARCH_TYPE::Unknown))
			{
				pInfo->pMDFieldBucket->KeyList.push_back(pInfo->Rec);
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::S3GetMDSearchFields(LPCTSTR pszBucket, S3_METADATA_SEARCH_FIELDS_BUCKET& MDFieldBucket)
{
	CStateRef State(this);
	S3_ERROR Error;
	try
	{
		CBuffer RetData;
		InitHeader();
		CString sResource = CString(_T("/")) + pszBucket + _T("/?searchmetadata");
		Error = SendRequest(_T("GET"), sResource, nullptr, 0, RetData);
		if (!RetData.IsEmpty())
		{
			XML_S3_METADATA_SEARCH_FIELDS_BUCKET_CONTEXT Context;
			Context.pMDFieldBucket = &MDFieldBucket;
			HRESULT hr = ScanXml(&RetData, &Context, XmlS3GetMDSearchFieldsBucketCB);
			if (FAILED(hr))
				return hr;
		}
		else
		{
			// XML doesn't look valid. maybe we are connected to the wrong server?
			// maybe there is a man-in-middle attack?
			Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
			Error.S3Error = S3_ERROR_MalformedXML;
			Error.sS3Code = _T("MalformedXML");
			Error.sS3RequestID = _T("GET");
			Error.sS3Resource = sResource;
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

struct XML_S3_SEARCH_MD_CONTEXT
{
	CECSConnection::S3_METADATA_SEARCH_RESULT *pMDSearchResult;
	CString sMarker;
	bool bIsTruncated;
	CECSConnection::S3_METADATA_SEARCH_RESULT_OBJECT_MATCH ObjectRec;
	CECSConnection::S3_METADATA_SEARCH_RESULT_QUERY_MD QueryMDRec;
	CECSConnection::S3_METADATA_SEARCH_RESULT_MD_MAP MDMapRec;
	XML_S3_SEARCH_MD_CONTEXT()
		: pMDSearchResult(nullptr)
		, bIsTruncated(false)
	{}
};

const WCHAR * const XML_S3_SEARCH_MD_NAME = L"//BucketQueryResult/Name";
const WCHAR * const XML_S3_SEARCH_MD_NEXTMARKER = L"//BucketQueryResult/NextMarker";
const WCHAR * const XML_S3_SEARCH_MD_ISTRUNCATED = L"//BucketQueryResult/IsTruncated";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT = L"//BucketQueryResult/ObjectMatches/object";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT_NAME = L"//BucketQueryResult/ObjectMatches/object/objectName";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT_ID = L"//BucketQueryResult/ObjectMatches/object/objectId";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT_VERSIONID = L"//BucketQueryResult/ObjectMatches/object/versionId";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT_QUERYMDS = L"//BucketQueryResult/ObjectMatches/object/queryMds";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT_QUERYMDS_TYPE = L"//BucketQueryResult/ObjectMatches/object/queryMds/type";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT_QUERYMDS_MDMAP = L"//BucketQueryResult/ObjectMatches/object/queryMds/mdMap/entry";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT_QUERYMDS_MDMAP_KEY = L"//BucketQueryResult/ObjectMatches/object/queryMds/mdMap/entry/key";
const WCHAR * const XML_S3_SEARCH_MD_OBJECT_QUERYMDS_MDMAP_VALUE = L"//BucketQueryResult/ObjectMatches/object/queryMds/mdMap/entry/value";

HRESULT XmlS3SearchMDCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_SEARCH_MD_CONTEXT *pInfo = (XML_S3_SEARCH_MD_CONTEXT *)pContext;
	if ((pInfo == nullptr) || (pInfo->pMDSearchResult == nullptr))
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (psValue != nullptr)
		{
			if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_NAME) == 0)
			{
				pInfo->pMDSearchResult->sBucket = *psValue;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_NEXTMARKER) == 0)
			{
				pInfo->sMarker = *psValue;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_ISTRUNCATED) == 0)
			{
				pInfo->bIsTruncated = psValue->CompareNoCase(L"false") != 0;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_NAME) == 0)
			{
				pInfo->ObjectRec.sObjectName = *psValue;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_ID) == 0)
			{
				pInfo->ObjectRec.sObjectId = *psValue;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_VERSIONID) == 0)
			{
				pInfo->ObjectRec.sVersionId = *psValue;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_QUERYMDS_TYPE) == 0)
			{
				if (psValue->CompareNoCase(L"SYSMD") == 0)
					pInfo->QueryMDRec.FieldType = CECSConnection::E_MD_SEARCH_FIELD::SYSMD;
				if (psValue->CompareNoCase(L"USERMD") == 0)
					pInfo->QueryMDRec.FieldType = CECSConnection::E_MD_SEARCH_FIELD::USERMD;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_QUERYMDS_MDMAP_KEY) == 0)
			{
				pInfo->MDMapRec.sKey = *psValue;
			}
			else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_QUERYMDS_MDMAP_VALUE) == 0)
			{
				pInfo->MDMapRec.sValue = *psValue;
			}
		}
		break;

	case XmlNodeType_Element:
		if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT) == 0)
		{
			pInfo->ObjectRec.sObjectId.Empty();
			pInfo->ObjectRec.sObjectName.Empty();
			pInfo->ObjectRec.QueryMDList.clear();
		}
		else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_QUERYMDS) == 0)
		{
			pInfo->QueryMDRec.FieldType = CECSConnection::E_MD_SEARCH_FIELD::Unknown;
			pInfo->QueryMDRec.MDMapList.clear();
		}
		else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_QUERYMDS_MDMAP) == 0)
		{
			pInfo->MDMapRec.sKey.Empty();
			pInfo->MDMapRec.sValue.Empty();
		}
		break;
	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT) == 0)
		{
			pInfo->pMDSearchResult->ObjectMatchList.push_back(pInfo->ObjectRec);
		}
		else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_QUERYMDS) == 0)
		{
			pInfo->ObjectRec.QueryMDList.push_back(pInfo->QueryMDRec);
		}
		else if (sXmlPath.CompareNoCase(XML_S3_SEARCH_MD_OBJECT_QUERYMDS_MDMAP) == 0)
		{
			pInfo->QueryMDRec.MDMapList.push_back(pInfo->MDMapRec);
		}
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::S3SearchMD(
	const S3_METADATA_SEARCH_PARAMS& Params,
	S3_METADATA_SEARCH_RESULT& MDSearchResult)
{
	CStateRef State(this);
	S3_ERROR Error;
	try
	{
		MDSearchResult.ObjectMatchList.clear();
		CBuffer RetData;
		InitHeader();
		CString sExpr(Params.sExpression);
		sExpr.TrimLeft();
		sExpr.TrimRight();
		// check for literal string that has to match a unicode metadata field
		for (;;)
		{
			int iOpen = sExpr.Find(_T("`"));
			if (iOpen < 0)
				break;
			int iClose = sExpr.Find(_T("`"), iOpen + 1);
			if (iClose < 0)
				break;
			// got both open and close quotes
			// get string inside quotes
			CString sStr(sExpr.Mid(iOpen + 1, iClose - iOpen - 1));
			CBuffer Buf;
			int iBuf = 0;
			Buf.SetBufSize(DWORD(sStr.GetLength() * sizeof(WCHAR)));
			// convert each character to 4 hex bytes, MSD first
			for (UINT i = 0; i < (UINT)sStr.GetLength(); i++)
			{
				WCHAR Ch = sStr.GetAt(i);
				Buf.SetAt(iBuf++, Ch >> 8);
				Buf.SetAt(iBuf++, BYTE(Ch & 0xff));
			}
			sStr = BinaryToHexString(Buf);
			// now reconstruct the string, changing the ` to a "
			CString sNewStr(sExpr.Left(iOpen) + _T('"') + sStr + _T('"') + sExpr.Mid(iClose + 1));
			sExpr = sNewStr;
		}
		CString sResource = CString(_T("/")) + UriEncode(Params.sBucket) + _T("/?query=") + UriEncode(sExpr, E_URI_ENCODE::AllSAFE);
		if (!Params.sAttributes.IsEmpty())
			sResource += _T("&attributes=") + UriEncode(Params.sAttributes, E_URI_ENCODE::AllSAFE);
		if (!Params.sSorted.IsEmpty())
			sResource += _T("&sorted=") + UriEncode(Params.sSorted, E_URI_ENCODE::AllSAFE);
		if (Params.bOlderVersions)
			sResource += CString(_T("&include-older-versions=")) + (Params.bOlderVersions ? _T("true") : _T("false"));
		CString sMarker;
		for (;;)
		{
			CString sMarkerResource;
			if (!sMarker.IsEmpty())
				sMarkerResource = _T("&marker=") + sMarker;
			Error = SendRequest(_T("GET"), sResource + sMarkerResource, nullptr, 0, RetData);
			if (Error.IfError())
				return Error;
			if (!RetData.IsEmpty())
			{
				XML_S3_SEARCH_MD_CONTEXT Context;
				Context.pMDSearchResult = &MDSearchResult;
				HRESULT hr = ScanXml(&RetData, &Context, XmlS3SearchMDCB);
				if (FAILED(hr))
					return hr;
				sMarker = Context.sMarker;
				if (sMarker == _T("NO MORE PAGES"))
					break;
				if (sMarker.IsEmpty())
					break;
			}
			else
			{
				// XML doesn't look valid. maybe we are connected to the wrong server?
				// maybe there is a man-in-middle attack?
				Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
				Error.S3Error = S3_ERROR_MalformedXML;
				Error.sS3Code = _T("MalformedXML");
				Error.sS3RequestID = _T("GET");
				Error.sS3Resource = sResource;
				return Error;
			}
		}
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}
	return Error;
}

struct XML_DT_QUERY_CONTEXT
{
	CECSConnection::DT_QUERY_RESPONSE *pResponse;
};

// DT Query XML
//<?xml version=“1.0" encoding=“UTF-8” standalone=“no”?>
//<Summary>
//<status>false</status>
//<Version_0>
//<total_data_size>9</total_data_size>
//<shipped_data_size>0</shipped_data_size>
//<shipped_data_percentage>0</shipped_data_percentage>
//</Version_0>
//</Summary>

const WCHAR * const XML_DT_QUERY_STATUS = L"//Summary/status";
const WCHAR * const XML_DT_QUERY_VERSION_0_TOTAL_DATA_SIZE = L"//Summary/Version_0/total_data_size";
const WCHAR * const XML_DT_QUERY_VERSION_0_SHIPPED_DATA_SIZE = L"//Summary/Version_0/shipped_data_size";
const WCHAR * const XML_DT_QUERY_VERSION_0_SHIPPED_DATA_PCT = L"//Summary/Version_0/shipped_data_percentage";
const WCHAR * const XML_DT_QUERY_VERSION_0_SHIPPED_DATA_RANGE = L"//Summary/Version_0/Data_Range_Shipping_Details/Range";

HRESULT XmlDTQueryCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_DT_QUERY_CONTEXT *pResponse = (XML_DT_QUERY_CONTEXT *)pContext;
	if ((pResponse == nullptr) || (pResponse->pResponse == nullptr))
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (psValue != nullptr)
		{
			if (sXmlPath.CompareNoCase(XML_DT_QUERY_STATUS) == 0)
			{
				pResponse->pResponse->bStatus = psValue->CompareNoCase(L"false") != 0;
			}
			else if (sXmlPath.CompareNoCase(XML_DT_QUERY_VERSION_0_TOTAL_DATA_SIZE) == 0)
			{
				pResponse->pResponse->ullTotalDataSize = _wtoi64(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_DT_QUERY_VERSION_0_SHIPPED_DATA_SIZE) == 0)
			{
				pResponse->pResponse->ullShippedDataSize = _wtoi64(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_DT_QUERY_VERSION_0_SHIPPED_DATA_PCT) == 0)
			{
				pResponse->pResponse->uShippedDataPercentage = _wtol(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_DT_QUERY_VERSION_0_SHIPPED_DATA_RANGE) == 0)
			{
				pResponse->pResponse->DataRangeShippingDetails.push_back(FROM_UNICODE(*psValue));
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::ECSDTQuery(LPCTSTR pszNamespace, LPCTSTR pszBucket, LPCTSTR pszObject, bool bShowValue, LPCTSTR pszRandom, DT_QUERY_RESPONSE& Response)
{
	CStateRef State(this);
	INTERNET_PORT wSavePort = Port;
	list<HEADER_REQ> Req;
	CBuffer RetData;
	CString sResource;
	sResource.Format(_T("/diagnostic/object/checkRepoReplicationStatus?poolname=%s.%s&objectname=%s"), pszNamespace, pszBucket, pszObject);
	if (bShowValue)
		sResource += _T("&showvalue=true");
	if (pszRandom != nullptr)
		sResource += CString(_T("&random=")) + pszRandom;
	SetPort(9101);
	InitHeader();
	S3_ERROR Error = SendRequest(_T("GET"), sResource, nullptr, 0, RetData, &Req);
	SetPort(wSavePort);
	if (Error.IfError())
	{
		CWideString Str((LPCSTR)RetData.GetData(), RetData.GetBufSize());
		Error.sDetails = Str;
		return Error;
	}
	// now interpret the response
	XML_DT_QUERY_CONTEXT Context;
	Context.pResponse = &Response;
	HRESULT hr = ScanXml(&RetData, &Context, XmlDTQueryCB);
	if (FAILED(hr))
		return hr;
	return Error;
}

// global initialized flag. must be called to set regular timeouts
void CECSConnection::SetInitialized(void)
{
	bInitialized = true;
}

struct XML_S3_ENDPOINT_INFO_CONTEXT
{
	CECSConnection::S3_ENDPOINT_INFO *pEndpointInfo;
	CString sLastDataNode;

	XML_S3_ENDPOINT_INFO_CONTEXT()
		: pEndpointInfo(nullptr)
	{}
};

const WCHAR * const XML_S3_ENDPOINT_INFO_DATA_NODE = L"//ListDataNode/DataNodes";
const WCHAR * const XML_S3_ENDPOINT_INFO_VERSION = L"//ListDataNode/VersionInfo";

HRESULT XmlS3EndpointInfoCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_ENDPOINT_INFO_CONTEXT *pInfo = (XML_S3_ENDPOINT_INFO_CONTEXT *)pContext;
	if ((pInfo == nullptr) || (pInfo->pEndpointInfo == nullptr))
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if (sXmlPath.CompareNoCase(XML_S3_ENDPOINT_INFO_DATA_NODE) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->sLastDataNode = FROM_UNICODE(*psValue);
		}
		else if (sXmlPath.CompareNoCase(XML_S3_ENDPOINT_INFO_VERSION) == 0)
		{
			if ((psValue != nullptr) && !psValue->IsEmpty())
				pInfo->pEndpointInfo->sVersion = FROM_UNICODE(*psValue);
		}
		break;

	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_S3_ENDPOINT_INFO_DATA_NODE) == 0)
		{
			// finished receiving a BUCKET element
			if (!pInfo->sLastDataNode.IsEmpty())
			{
				pInfo->pEndpointInfo->EndpointList.push_back(pInfo->sLastDataNode);
				pInfo->sLastDataNode.Empty();
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::DataNodeEndpointS3(S3_ENDPOINT_INFO& Endpoint)
{
	CStateRef State(this);
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	Error = SendRequest(_T("GET"), _T("/?endpoint"), nullptr, 0, RetData, &Req);
	if (Error.IfError())
		return Error;
	if (!RetData.IsEmpty())
	{
		XML_S3_ENDPOINT_INFO_CONTEXT Context;
		Context.pEndpointInfo = &Endpoint;
		HRESULT hr = ScanXml(&RetData, &Context, XmlS3EndpointInfoCB);
		if (FAILED(hr))
			return hr;
	}
	else
	{
		// XML doesn't look valid. maybe we are connected to the wrong server?
		// maybe there is a man-in-middle attack?
		Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
		Error.S3Error = S3_ERROR_MalformedXML;
		Error.sS3Code = _T("MalformedXML");
		Error.sS3RequestID = _T("GET");
		Error.sS3Resource = _T("/");
	}
	return Error;
}

struct XML_S3_LIFECYCLE_INFO_CONTEXT
{
	CECSConnection::S3_LIFECYCLE_INFO *pLifecycleInfo;
	CECSConnection::S3_LIFECYCLE_RULE LastRule;

	XML_S3_LIFECYCLE_INFO_CONTEXT()
		: pLifecycleInfo(nullptr)
	{}
};

const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE = L"//LifecycleConfiguration/Rule";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_ID = L"//LifecycleConfiguration/Rule/ID";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_PREFIX = L"//LifecycleConfiguration/Rule/Prefix";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_STATUS = L"//LifecycleConfiguration/Rule/Status";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_EXPIRATION = L"//LifecycleConfiguration/Rule/Expiration";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_EXPIRATION_DAYS = L"//LifecycleConfiguration/Rule/Expiration/Days";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_EXPIRATION_DATE = L"//LifecycleConfiguration/Rule/Expiration/Date";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_NONCURRENT_EXPIRATION = L"//LifecycleConfiguration/Rule/NoncurrentVersionExpiration";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_NONCURRENT_EXPIRATION_DAYS = L"//LifecycleConfiguration/Rule/NoncurrentVersionExpiration/NoncurrentDays";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_ABORTUPLOAD = L"//LifecycleConfiguration/Rule/AbortIncompleteMultipartUpload";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_ABORTUPLOAD_DAYS = L"//LifecycleConfiguration/Rule/AbortIncompleteMultipartUpload/DaysAfterInitiation";
const WCHAR * const XML_S3_LIFECYCLE_INFO_RULE_EXPIRATION_EXPIRE_DELETE_MARKER = L"//LifecycleConfiguration/Rule/Expiration/ExpiredObjectDeleteMarker";

HRESULT XmlS3LifecycleInfoCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_LIFECYCLE_INFO_CONTEXT *pInfo = (XML_S3_LIFECYCLE_INFO_CONTEXT *)pContext;
	if ((pInfo == nullptr) || (pInfo->pLifecycleInfo == nullptr))
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((psValue != nullptr) && !psValue->IsEmpty())
		{
			if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE_ID) == 0)
				pInfo->LastRule.sRuleID = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE_PREFIX) == 0)
				pInfo->LastRule.sPath = FROM_UNICODE(*psValue);
			else if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE_STATUS) == 0)
				pInfo->LastRule.bEnabled = psValue->CompareNoCase(L"enabled") == 0;
			else if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE_EXPIRATION_DAYS) == 0)
			{
				pInfo->LastRule.dwDays = _ttoi(FROM_UNICODE(*psValue));
				ZeroFT(pInfo->LastRule.ftDate);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE_EXPIRATION_DATE) == 0)
			{
				pInfo->LastRule.dwDays = 0;
				(void)CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), pInfo->LastRule.ftDate);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE_NONCURRENT_EXPIRATION_DAYS) == 0)
			{
				pInfo->LastRule.dwNoncurrentDays = _ttoi(FROM_UNICODE(*psValue));
			}
			else if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE_ABORTUPLOAD_DAYS) == 0)
			{
				pInfo->LastRule.dwAbortIncompleteMultipartUploadDays = _wtoi(*psValue);
			}
			else if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE_EXPIRATION_EXPIRE_DELETE_MARKER) == 0)
			{
				pInfo->LastRule.bExpiredDeleteMarkers = psValue->CompareNoCase(L"true") == 0;
			}
		}
		break;

	case XmlNodeType_Element:
		if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE) == 0)
		{
			pInfo->LastRule.Empty();
		}
		break;
	case XmlNodeType_EndElement:
		if (sXmlPath.CompareNoCase(XML_S3_LIFECYCLE_INFO_RULE) == 0)
		{
			// finished receiving a lifecycle rule
			if (!pInfo->LastRule.IsEmpty())
			{
				pInfo->pLifecycleInfo->LifecycleRules.push_back(pInfo->LastRule);
				pInfo->LastRule.Empty();
			}
		}
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::S3GetLifecycle(LPCTSTR pszBucket, S3_LIFECYCLE_INFO & Lifecycle)
{
	CStateRef State(this);
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	CString sResource(CString(_T("/")) + pszBucket + _T("?lifecycle"));
	Error = SendRequest(_T("GET"), sResource, nullptr, 0, RetData, &Req);
	if (Error.IfError())
		return Error;
	if (!RetData.IsEmpty())
	{
		XML_S3_LIFECYCLE_INFO_CONTEXT Context;
		Context.pLifecycleInfo = &Lifecycle;
		HRESULT hr = ScanXml(&RetData, &Context, XmlS3LifecycleInfoCB);
		if (FAILED(hr))
			return hr;
	}
	else
	{
		// XML doesn't look valid. maybe we are connected to the wrong server?
		// maybe there is a man-in-middle attack?
		Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
		Error.S3Error = S3_ERROR_MalformedXML;
		Error.sS3Code = _T("MalformedXML");
		Error.sS3RequestID = _T("GET");
		Error.sS3Resource = _T("/");
	}
	return Error;
}

CECSConnection::S3_ERROR CECSConnection::S3DeleteLifecycle(LPCTSTR pszBucket)
{
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	CString sResource(CString(_T("/")) + pszBucket + _T("?lifecycle"));
	Error = SendRequest(_T("DELETE"), sResource, nullptr, 0, RetData, &Req);
	if (Error.IfError())
		return Error;
	return Error;
}

CECSConnection::S3_ERROR CECSConnection::S3PutLifecycle(LPCTSTR pszBucket, const S3_LIFECYCLE_INFO & Lifecycle)
{
	CStateRef State(this);
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	try
	{
		// create XML request
		CBufferStream *pBufStream = new CBufferStream;
		CComPtr<IStream> pOutFileStream = pBufStream;
		CComPtr<IXmlWriter> pWriter;

		if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), (void**)&pWriter, nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->SetOutput(pOutFileStream)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartDocument(XmlStandalone_Omit)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteStartElement(nullptr, L"LifecycleConfiguration", nullptr)))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

		for (list<S3_LIFECYCLE_RULE>::const_iterator it = Lifecycle.LifecycleRules.begin(); it != Lifecycle.LifecycleRules.end(); ++it)
		{
			if (FAILED(pWriter->WriteStartElement(nullptr, L"Rule", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteStartElement(nullptr, L"ID", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(TO_UNICODE(it->sRuleID))))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteFullEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteStartElement(nullptr, L"Prefix", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			CString sPath(it->sPath);
			if (!sPath.IsEmpty() && (sPath[sPath.GetLength() - 1] != L'/'))
				sPath += L'/';
			if (FAILED(pWriter->WriteString(TO_UNICODE(sPath))))							// path, if not empty, should always terminate with /
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteFullEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			if (FAILED(pWriter->WriteStartElement(nullptr, L"Status", nullptr)))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteString(it->bEnabled ? L"Enabled" : L"Disabled")))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			if (FAILED(pWriter->WriteFullEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

			// check if the Expiration clause has anything in it
			if (!IfFTZero(it->ftDate)
				|| (it->dwDays != 0)
				|| it->bExpiredDeleteMarkers)
			{
				if (FAILED(pWriter->WriteStartElement(nullptr, L"Expiration", nullptr)))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
				if (!IfFTZero(it->ftDate))
				{
					// specify a date
					if (FAILED(pWriter->WriteStartElement(nullptr, L"Date", nullptr)))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
					if (FAILED(pWriter->WriteString(TO_UNICODE(CECSConnection::FormatISO8601Date(it->ftDate, false)))))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
					if (FAILED(pWriter->WriteFullEndElement()))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
				}
				else if (it->dwDays != 0)
				{
					// specify # of days
					if (FAILED(pWriter->WriteStartElement(nullptr, L"Days", nullptr)))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
					if (FAILED(pWriter->WriteString(TO_UNICODE(FmtNum(it->dwDays)))))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
					if (FAILED(pWriter->WriteFullEndElement()))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
				}
				if (it->bExpiredDeleteMarkers)
				{
					if (FAILED(pWriter->WriteStartElement(nullptr, L"ExpiredObjectDeleteMarker", nullptr)))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
					if (FAILED(pWriter->WriteString(L"true")))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
					if (FAILED(pWriter->WriteFullEndElement()))
						throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
				}
				if (FAILED(pWriter->WriteFullEndElement()))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			}
			if (it->dwNoncurrentDays != 0)
			{
				// non-current objects
				if (FAILED(pWriter->WriteStartElement(nullptr, L"NoncurrentVersionExpiration", nullptr)))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

				// specify # of days
				if (FAILED(pWriter->WriteStartElement(nullptr, L"NoncurrentDays", nullptr)))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
				if (FAILED(pWriter->WriteString(TO_UNICODE(FmtNum(it->dwNoncurrentDays)))))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
				if (FAILED(pWriter->WriteFullEndElement()))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

				if (FAILED(pWriter->WriteFullEndElement()))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			}

			if (it->dwAbortIncompleteMultipartUploadDays != 0)
			{
				// abort incomplete multipart uploads
				if (FAILED(pWriter->WriteStartElement(nullptr, L"AbortIncompleteMultipartUpload", nullptr)))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

				// specify # of days
				if (FAILED(pWriter->WriteStartElement(nullptr, L"DaysAfterInitiation", nullptr)))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
				if (FAILED(pWriter->WriteString(TO_UNICODE(FmtNum(it->dwAbortIncompleteMultipartUploadDays)))))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
				if (FAILED(pWriter->WriteFullEndElement()))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);

				if (FAILED(pWriter->WriteFullEndElement()))
					throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
			}

			// End "Rule" clause
			if (FAILED(pWriter->WriteFullEndElement()))
				throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		}

		if (FAILED(pWriter->WriteFullEndElement()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->WriteEndDocument()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		if (FAILED(pWriter->Flush()))
			throw CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_XML_PARSE_ERROR);
		CString sXmlOut(pBufStream->GetXml());
#ifdef _UNICODE
		CAnsiString XmlUTF8(sXmlOut, CP_UTF8);
#else
		CAnsiString XmlUTF8(sXmlOut);
#endif
		XmlUTF8.SetBufSize((DWORD)strlen(XmlUTF8));
		CCngAES_GCM HashObj;
		HashObj.CreateHash(BCRYPT_MD5_ALGORITHM);
		HashObj.AddHashData(XmlUTF8);
		CBuffer MD5Hash;
		HashObj.GetHashData(MD5Hash);
		AddHeader(_T("Content-MD5"), MD5Hash.EncodeBase64());
		CString sResource(CString(_T("/")) + pszBucket + _T("/?lifecycle"));
		Error = SendRequest(_T("PUT"), sResource, XmlUTF8.GetData(), XmlUTF8.GetBufSize(), RetData, &Req);
		if (Error.IfError())
			return Error;
	}
	catch (const CS3ErrorInfo& E)
	{
		Error = E.Error;
	}
	return Error;
}

int CECSConnection::S3_METADATA_SEARCH_PARAMS::Compare(const S3_METADATA_SEARCH_PARAMS & Rec) const
{
	int iDiff = sBucket.Compare(Rec.sBucket);
	if (iDiff != 0)
		return iDiff;
	iDiff = sExpression.Compare(Rec.sExpression);
	if (iDiff != 0)
		return iDiff;
	iDiff = sAttributes.Compare(Rec.sAttributes);
	if (iDiff != 0)
		return iDiff;
	iDiff = sSorted.Compare(Rec.sSorted);
	if (iDiff != 0)
		return iDiff;
	if (bOlderVersions != Rec.bOlderVersions)
		return 1;

	return iDiff;
}

bool CECSConnection::S3_METADATA_SEARCH_PARAMS::operator==(const S3_METADATA_SEARCH_PARAMS & Rec) const
{
	return Compare(Rec) == 0;
}

bool CECSConnection::S3_METADATA_SEARCH_PARAMS::operator!=(const S3_METADATA_SEARCH_PARAMS & Rec) const
{
	return Compare(Rec) != 0;
}

CString CECSConnection::S3_METADATA_SEARCH_PARAMS::Format(void) const
{
	return sBucket + _T(": ") + sExpression;
}

// ReadSystemMetadata
// get specified system metadata for specified object
// it gets all metadata fields
CECSConnection::S3_ERROR CECSConnection::ReadProperties(
	LPCTSTR pszPath,						// (in) S3 path to object
	S3_SYSTEM_METADATA& Properties,			// (out) object properties
	LPCTSTR pszVersionId,					// (in, optional) version ID
	list<HEADER_STRUCT> *pMDList,		// (out, optional) metadata list
	list<HEADER_REQ> *pReq)					// (out, optional) full header list
{
	CStateRef State(this);
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	try
	{
		CBuffer RetData;
		if (pReq == nullptr)
			pReq = &Req;
		Properties.Empty();
		InitHeader();
		CString sPath(UriEncode(pszPath));
		// first get the complete list of system metadata for this object
		if ((pszVersionId != nullptr) && (*pszVersionId != NUL))
			sPath += CString(_T("?versionId=")) + pszVersionId;
		Error = SendRequest(_T("HEAD"), sPath, nullptr, 0, RetData, pReq);
		if (Error.IfError())
			return Error;
		Properties.Empty();
		FILETIME ftEMCMtime = { 0, 0 };
		for (list<HEADER_REQ>::const_iterator it = pReq->begin(); it != pReq->end(); ++it)
		{
			if ((it->sHeader.CompareNoCase(_T("Last-Modified")) == 0) && !it->ContentList.empty())
			{
				Properties.ftLastMod = ParseCanonicalTime(it->ContentList.front());
			}
			else if ((it->sHeader.CompareNoCase(_T("ETag")) == 0) && !it->ContentList.empty())
			{
				Properties.sETag = it->ContentList.front();
			}
			else if ((it->sHeader.CompareNoCase(_T("Content-Length")) == 0) && !it->ContentList.empty())
			{
				_stscanf_s(it->ContentList.front(), _T("%I64u"), &Properties.llSize);
			}
			else if ((it->sHeader.CompareNoCase(_T("x-emc-retention-period")) == 0) && !it->ContentList.empty())
			{
				Properties.dwRetentionSeconds = _ttoi(it->ContentList.front());
			}
			else if ((it->sHeader.CompareNoCase(L"x-emc-mtime") == 0) && !it->ContentList.empty())
			{
				LONGLONG llTime = _wtoll(it->ContentList.front());
				if (llTime > 0)
					ftEMCMtime = ECSmtimeToFileTime(llTime);
			}
			else if ((pMDList != nullptr) && (it->sHeader.Find(sAmzMetaPrefix) == 0))
			{
				HEADER_STRUCT Rec;
				Rec.sHeader = it->sHeader;
				if (!it->ContentList.empty())
					Rec.sContents = it->ContentList.front();
				pMDList->push_back(Rec);
			}
		}
		// if we saw x-emc-mtime, use that since its resolution is better (millisec vs sec)
		if (!IfFTZero(ftEMCMtime))
			Properties.ftLastMod = ftEMCMtime;
	}
	catch (const CS3ErrorInfo& E)
	{
		return E.Error;
	}

	return Error;
}

bool CECSConnectionAbortBase::IfShutdownCommon(void *pContext)
{
	if (pContext == nullptr)
		return false;
	// pContext in this case is a point to an instance of CECSConnectionAbort
	CECSConnectionAbortBase *pAbort = (CECSConnectionAbortBase *)pContext;
	return pAbort->IfShutdown();
}

void CECSConnection::S3_ERROR::SetError(void)
{
	if ((dwError != ERROR_SUCCESS) || !IfError())
		return;
	map<DWORD, HRESULT>::const_iterator it = HttpErrorMap.find(dwHttpError);
	if (it == HttpErrorMap.end())
		dwError = (DWORD)HTTP_E_STATUS_UNEXPECTED;
	else
		dwError = it->second;
}

// SetRootCertificate
// pass in a serialized SLL certificate
// set it in the trusted root area of the certificate store
// returns error code
DWORD CECSConnection::SetRootCertificate(
	const ECS_CERT_INFO& CertInfo,
	DWORD dwCertOpenFlags,
	LPCTSTR pszStoreName)					// Such as L"My" or L"Root"
{
	DWORD dwError;
	// open a certificate store
	HCERTSTORE hCertStore = CertOpenStore(CERT_STORE_PROV_SYSTEM,
		0,
		0,
		dwCertOpenFlags,
		pszStoreName);
	if (hCertStore == nullptr)
		return GetLastError();

	// add the certificate
	if (!CertAddSerializedElementToStore(hCertStore, CertInfo.SerializedCert.GetData(), CertInfo.SerializedCert.GetBufSize(),
		CERT_STORE_ADD_REPLACE_EXISTING, 0, CERT_STORE_CERTIFICATE_CONTEXT_FLAG, nullptr, nullptr))
	{
		dwError = GetLastError();
		(void)CertCloseStore(hCertStore, 0);
		return dwError;
	}

	// close the store
	if (!CertCloseStore(hCertStore, 0))
		return GetLastError();
	return ERROR_SUCCESS;
}

struct SECURE_ERROR_TEXT
{
	DWORD dwWinHttpErrorMask;
	LPCTSTR pszErrorText;
} ErrorArray[] =
{
	{ WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED, _T("Certification revocation checking has been enabled, but the revocation check failed to verify whether a certificate has been revoked. The server used to check for revocation might be unreachable.\r\n") },
	{ WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT, _T("SSL certificate is invalid.\r\n") },
	{ WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED, _T("SSL certificate was revoked.\r\n") },
	{ WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA, _T("The function is unfamiliar with the Certificate Authority that generated the server's certificate.\r\n") },
	{ WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID, _T("SSL certificate common name (host name field) is incorrect, for example, if you entered www.microsoft.com and the common name on the certificate says www.msn.com.\r\n") },
	{ WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID, _T("SSL certificate date that was received from the server is bad. The certificate is expired.\r\n") },
	{ WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR, _T("The application experienced an internal error loading the SSL libraries.\r\n") },
};

CString CECSConnection::GetSecureErrorText(DWORD dwSecureError)
{
	CString sMsg;
	for (UINT i = 0; i < _countof(ErrorArray); i++)
	{
		if (TST_BIT(dwSecureError, ErrorArray[i].dwWinHttpErrorMask))
			sMsg += ErrorArray[i].pszErrorText;
	}
	return sMsg;
}

DWORD CECSConnection::RetrieveServerCertificate(ECS_CERT_INFO& CertInfo)
{
	CStateRef State(this);
	CERT_CONTEXT *pCert;
	DWORD dwLen = sizeof pCert;
	if (!WinHttpQueryOption(State.Ref->hRequest, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &pCert, &dwLen))
		return GetLastError();
	TCHAR NameBuf[1024];
	(void)CertGetNameString(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, NameBuf, _countof(NameBuf));
	CertInfo.sCertName = NameBuf;
	DWORD dwStrLen;
	LPTSTR pszSubject = CertInfo.sCertSubject.GetBuffer(dwStrLen = CertNameToStr(pCert->dwCertEncodingType, &(pCert->pCertInfo->Subject), CERT_X500_NAME_STR | CERT_NAME_STR_CRLF_FLAG, nullptr, 0) + 2);
	(void)CertNameToStr(pCert->dwCertEncodingType, &(pCert->pCertInfo->Subject), CERT_X500_NAME_STR | CERT_NAME_STR_CRLF_FLAG, pszSubject, dwStrLen);
	CertInfo.sCertSubject.ReleaseBuffer();

	// get subject alternate name (SAN) if available
	PCERT_EXTENSION pCertExt = CertFindExtension(szOID_SUBJECT_ALT_NAME2, pCert->pCertInfo->cExtension, pCert->pCertInfo->rgExtension);
	if (pCertExt != nullptr)
	{
		DWORD dwNameBuf = _countof(NameBuf);
		if (CryptFormatObject(pCert->dwCertEncodingType, 0, CRYPT_FORMAT_STR_MULTI_LINE, nullptr, szOID_ISSUER_ALT_NAME2, pCertExt->Value.pbData,
			pCertExt->Value.cbData, NameBuf, &dwNameBuf))
		{
			CertInfo.sCertSubjectAltName = NameBuf;
		}
	}

	// Find out how much memory to allocate for the serialized element.
	DWORD dwElementLen = 0;
	if (!CertSerializeCertificateStoreElement(pCert, 0, nullptr, &dwElementLen))
		return GetLastError();
	CertInfo.SerializedCert.SetBufSize(dwElementLen);
	if (!CertSerializeCertificateStoreElement(pCert, 0, CertInfo.SerializedCert.GetData(), &dwElementLen))
		return GetLastError();
	CertInfo.SerializedCert.SetBufSize(dwElementLen);				// it might have gotten smaller. make sure we have the exact size
	(void)CertFreeCertificateContext(pCert);
	return ERROR_SUCCESS;
}

CString CECSConnection::DumpBadIPMap(void)
{
	CSingleLock csBad(&csBadIPMap, true);
	CString sEntry, sMsg;

	for (map<BAD_IP_KEY, BAD_IP_ENTRY>::iterator itMap = BadIPMap.begin(); itMap != BadIPMap.end(); ++itMap)
	{
		sEntry = _T("Host: ") + itMap->first.sHostName;
		sEntry.Format(_T("Host: %s\r\nIP: %s\r\nTime: %s\r\nError: %s\r\n\r\n"),
			(LPCTSTR)itMap->first.sHostName,
			(LPCTSTR)itMap->first.sIP,
			(LPCTSTR)DateTimeStr(&itMap->second.ftError, true, true, true, false, true, true),
			(LPCTSTR)itMap->second.ErrorInfo.Format());
		sMsg += sEntry;
	}
	return sMsg;
}

struct XML_S3_REPLICATION_INFO_CONTEXT
{
	CECSConnection::S3_REPLICATION_INFO *pReplicationInfo;
};

const WCHAR * const XML_S3_REPLICATION_INFO_INDEX_REPLICATED = L"//ObjectReplicationInfo/IndexReplicated";
const WCHAR * const XML_S3_REPLICATION_INFO_REPLICATED_DATA_PERCENTAGE = L"//ObjectReplicationInfo/ReplicatedDataPercentage";

HRESULT XmlS3ReplicationInfoCB(const CStringW& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CStringW *psValue)
{
	(void)pReader;
	(void)pAttrList;
	XML_S3_REPLICATION_INFO_CONTEXT *pInfo = (XML_S3_REPLICATION_INFO_CONTEXT *)pContext;
	if ((pInfo == nullptr) || (pInfo->pReplicationInfo == nullptr))
		return ERROR_INVALID_DATA;

	switch (NodeType)
	{
	case XmlNodeType_Text:
		if ((psValue != nullptr) && !psValue->IsEmpty())
		{
			wchar_t *pszStop = nullptr;
			if (sXmlPath.CompareNoCase(XML_S3_REPLICATION_INFO_INDEX_REPLICATED) == 0)
				pInfo->pReplicationInfo->bIndexReplicated = psValue->CompareNoCase(L"true") == 0;
			else if (sXmlPath.CompareNoCase(XML_S3_REPLICATION_INFO_REPLICATED_DATA_PERCENTAGE) == 0)
				pInfo->pReplicationInfo->dReplicatedDataPercentage = wcstold(*psValue, &pszStop);
		}
		break;

	case XmlNodeType_Element:
		break;
	case XmlNodeType_EndElement:
		break;

	default:
		break;
	}
	return 0;
}

CECSConnection::S3_ERROR CECSConnection::S3GetReplicationInfo(LPCTSTR pszPath, S3_REPLICATION_INFO& RepInfo)
{
	CStateRef State(this);
	list<HEADER_REQ> Req;
	S3_ERROR Error;
	CBuffer RetData;
	InitHeader();
	CString sResource(CString(pszPath) + _T("?replicationInfo"));
	Error = SendRequest(_T("GET"), sResource, nullptr, 0, RetData, &Req);
	if (Error.IfError())
		return Error;
	if (!RetData.IsEmpty())
	{
		XML_S3_REPLICATION_INFO_CONTEXT Context;
		Context.pReplicationInfo = &RepInfo;
		HRESULT hr = ScanXml(&RetData, &Context, XmlS3ReplicationInfoCB);
		if (FAILED(hr))
			return hr;
	}
	else
	{
		// XML doesn't look valid. maybe we are connected to the wrong server?
		// maybe there is a man-in-middle attack?
		Error.dwHttpError = HTTP_STATUS_SERVER_ERROR;
		Error.S3Error = S3_ERROR_MalformedXML;
		Error.sS3Code = _T("MalformedXML");
		Error.sS3RequestID = _T("GET");
		Error.sS3Resource = sResource;
	}
	return Error;
}
