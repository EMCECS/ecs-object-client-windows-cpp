/*
 * Copyright (c) 2017 - 2019, Dell Technologies, Inc. All Rights Reserved.
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

#include <afxsock.h>
#include <list>
#include <deque>
#include "ECSUtil.h"
#include "ECSConnection.h"
#include "NTERRTXT.H"
#include "SimpleWorkerThread.h"
#include "ThreadPool.h"
#include "CngAES_GCM.h"
#include "FileSupport.h"

CECSConnection::S3_ERROR S3Read(
	LPCWSTR pszFile,								// path to file
	DWORD grfMode,									// examples: for read: STGM_READ | STGM_SHARE_DENY_WRITE, for write: STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_WRITE
	DWORD dwAttributes,								// attribute of file if created
	bool bCreate,									// see SHCreateStreamOnFileEx on how to use
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	ULONGLONG lwLen,								// if lwOffset == 0 and dwLen == 0, read entire file
	ULONGLONG lwOffset,								// if dwLen != 0, read 'dwLen' bytes starting from lwOffset
													// if lwOffset != 0 and dwLen == 0, read from lwOffset to the end of the file
	list<CECSConnection::HEADER_REQ> *pRcvHeaders,			// optional return all headers
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext,											// context for UpdateProgressCB
	ULONGLONG *pullReturnedLength)					// optional output returned size
{
	CComPtr<IStream> pFileStream;
	HRESULT hr;
	if (FAILED(hr = SHCreateStreamOnFileEx(pszFile, grfMode, dwAttributes, bCreate, NULL, &pFileStream)))
		return hr;
	return S3Read(Conn, pszECSPath, pFileStream, lwLen, lwOffset, pRcvHeaders, UpdateProgressCB, pContext, pullReturnedLength);
}

CECSConnection::S3_ERROR S3Write(
	LPCWSTR pszFile,								// path to file
	DWORD grfMode,									// examples: for read: STGM_READ | STGM_SHARE_DENY_WRITE, for write: STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_WRITE
	DWORD dwAttributes,								// attribute of file if created
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	const DWORD dwBufSize,							// size of buffer to use
	bool bChecksum,									// if set, include content-MD5 header
	DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
	const list<CECSConnection::HEADER_STRUCT> *pMDList,	// optional metadata to send to object
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext)											// context for UpdateProgressCB
{
	CComPtr<IStream> pFileStream;
	HRESULT hr;
	if (FAILED(hr = SHCreateStreamOnFileEx(pszFile, grfMode, dwAttributes, false, NULL, &pFileStream)))
		return hr;
	return S3Write(Conn, pszECSPath, pFileStream, dwBufSize, bChecksum, dwMaxQueueSize, pMDList, UpdateProgressCB, pContext);
}

bool DoS3MultiPartUpload(
	LPCWSTR pszFile,								// path to file
	DWORD grfMode,									// examples: for read: STGM_READ | STGM_SHARE_DENY_WRITE, for write: STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_WRITE
	DWORD dwAttributes,								// attribute of file if created
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	const DWORD dwBufSize,							// size of buffer to use
	const DWORD dwPartSize,							// part size (in MB)
	const DWORD dwMaxThreads,						// maxiumum number of threads to spawn
	bool bChecksum,									// if set, include content-MD5 header
	const list<CECSConnection::HEADER_STRUCT> *pMDList,	// optional metadata to send to object
	DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
	DWORD dwMaxRetries,									// how many times to retry a part before giving up
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext,											// context for UpdateProgressCB
	CECSConnection::S3_ERROR& Error)						// returned error
{
	CComPtr<IStream> pFileStream;
	HRESULT hr;
	if (FAILED(hr = SHCreateStreamOnFileEx(pszFile, grfMode, dwAttributes, false, NULL, &pFileStream)))
	{
		Error = hr;
		return false;
	}
	return DoS3MultiPartUpload(Conn, pszECSPath, pFileStream, dwBufSize, dwPartSize, dwMaxThreads, bChecksum, pMDList, dwMaxQueueSize,
		dwMaxRetries, UpdateProgressCB, pContext, Error);
}

// TestShutdownThread
// pContext must point to CSimpleWorkerThread
static bool TestShutdownThread(void *pContext)
{
	CSimpleWorkerThread *pThread = (CSimpleWorkerThread *)pContext;
	// test if thread is exiting
	if (pThread != nullptr)
		return pThread->GetExitFlag();
	return false;
}

class CTestShutdown : public CECSConnectionAbortBase
{
private:
	const CSimpleWorkerThread *pThread;
public:
	CTestShutdown(const CSimpleWorkerThread *pThreadParam, CECSConnection *pHostParam = nullptr)
		: CECSConnectionAbortBase(pHostParam)
		, pThread(pThreadParam)
	{}

	~CTestShutdown()
	{
		pThread = nullptr;
	}
	bool IfShutdown(void)
	{
		if (pThread == nullptr)
			return false;
		return pThread->GetExitFlag();
	}
};

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// S3Read //////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

struct CS3ReadThread : public CSimpleWorkerThread
{
	// define here any thread-specific variables, if any
	CECSConnection::STREAM_CONTEXT ReadContext;
	CString sECSPath;					// path to ECS object to read
	CECSConnection *pConn;				// ECS connection object
	CSharedQueueEvent *pMsgEvent;		// event that the main thread will wait on
	CECSConnection::S3_ERROR Error;		// returned status
	ULONGLONG lwLen;								// if lwOffset == 0 and dwLen == 0, read entire file
	ULONGLONG lwOffset;								// if dwLen != 0, read 'dwLen' bytes starting from lwOffset
													// if lwOffset != 0 and dwLen == 0, read from lwOffset to the end of the file
	list<CECSConnection::HEADER_REQ> *pRcvHeaders;	// optional return all headers
	ULONGLONG ullReturnedLength;		// returned length from Read
	bool bWorkerDone;					// set so it only runs once

	CS3ReadThread()
		: pConn(nullptr)
		, pMsgEvent(nullptr)
		, lwLen(0ULL)
		, lwOffset(0ULL)
		, pRcvHeaders(nullptr)
		, ullReturnedLength(0ULL)
		, bWorkerDone(false)
	{}
	~CS3ReadThread()
	{
		pConn = nullptr;
		pMsgEvent = nullptr;
		pRcvHeaders = nullptr;
		KillThreadWait();
	}
	void DoWork();
};

// S3Read
// Set up a worker thread that will read the data from ECS and fill a memory queue
// the original thread will read the data off of the queue and write it to disk
CECSConnection::S3_ERROR S3Read(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	IStream *pStream,								// open stream to file
	ULONGLONG lwLen,								// if lwOffset == 0 and dwLen == 0, read entire file
	ULONGLONG lwOffset,								// if dwLen != 0, read 'dwLen' bytes starting from lwOffset
													// if lwOffset != 0 and dwLen == 0, read from lwOffset to the end of the file
	list<CECSConnection::HEADER_REQ> *pRcvHeaders,			// optional return all headers
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext,											// context for UpdateProgressCB
	ULONGLONG *pullReturnedLength)					// optional output returned size
{
	CS3ReadThread ReadThread;						// thread object
	CSharedQueueEvent MsgEvent;						// event that new data was pushed on the read queue
	DWORD dwError;
	DWORD dwMainThreadError = ERROR_SUCCESS;

	ReadThread.pConn = &Conn;
	ReadThread.pMsgEvent = &MsgEvent;
	ReadThread.sECSPath = pszECSPath;
	ReadThread.lwLen = lwLen;
	ReadThread.lwOffset = lwOffset;
	ReadThread.pRcvHeaders = pRcvHeaders;

	MsgEvent.Link(&ReadThread.ReadContext.StreamData);					// link the queue to the event
	MsgEvent.DisableAllTriggerEvents();
	MsgEvent.EnableTriggerEvents(TRIGGEREVENTS_PUSH | TRIGGEREVENTS_INSERTAT);
	MsgEvent.SetAllEvents();
	MsgEvent.Enable();

	// file is open and ready, now start up the worker thread so it starts reading from ECS
	ReadThread.CreateThread();				// create the thread
	ReadThread.StartWork();					// kick it off

	bool bDone = false;
	while (!bDone)
	{
		// wait then check if we are terminating
		dwError = WaitForSingleObject(MsgEvent.Event.evQueue.m_hObject, SECONDS(2));
		// check for thread exit (but not if we are waiting for the handle to close)
		if (Conn.TestAbort())
		{
			dwMainThreadError = ERROR_OPERATION_ABORTED;
			break;
		}
		if (ReadThread.bWorkerDone)
			break;
		// check if the background thread has ended with an error
		if (!ReadThread.IfActive() && ReadThread.Error.IfError())
			break;
		if (ReadThread.GetExitFlag())
		{
			// the thread has called KillThread()
			dwMainThreadError = ERROR_OPERATION_ABORTED;
			break;
		}
		if (dwError == WAIT_FAILED)
		{
			dwMainThreadError = GetLastError();
			break;
		}
		if (dwError == WAIT_OBJECT_0)
		{
			// got an event that something was pushed on the queue
			while (!ReadThread.ReadContext.StreamData.empty())
			{
				CECSConnection::STREAM_DATA_ENTRY StreamData;
				DWORD dwNumWritten;
				{
					CRWLockAcquire lockQueue(&ReadThread.ReadContext.StreamData.GetLock(), true);			// write lock
					StreamData = ReadThread.ReadContext.StreamData.front();
					ReadThread.ReadContext.StreamData.pop_front();
				}
				// write out the data
				if (!StreamData.Data.IsEmpty())
				{
					dwError = pStream->Write(StreamData.Data.GetData(), StreamData.Data.GetBufSize(), &dwNumWritten);
					if (dwError != S_OK)
						return dwError;
					if (UpdateProgressCB != nullptr)
						UpdateProgressCB(dwNumWritten, pContext);
				}
				if (StreamData.bLast)
				{
					bDone = true;
					break;								// done!
				}
			}
		}
	}
							// now wait for the worker thread to terminate to get its error code
	ReadThread.KillThreadWait();
	if (pullReturnedLength != nullptr)
		*pullReturnedLength = ReadThread.ullReturnedLength;
	if ((dwMainThreadError == ERROR_SUCCESS) || ReadThread.Error.IfError())
		return ReadThread.Error;
	return dwMainThreadError;
}

void CS3ReadThread::DoWork()
{
	if (!bWorkerDone && (dwEventRet == WAIT_OBJECT_0))
	{
		CBuffer RetData;
		pConn->RegisterShutdownCB(TestShutdownThread, this);
		Error = pConn->Read(sECSPath, lwLen, lwOffset, RetData, 0UL, &ReadContext, pRcvHeaders, &ullReturnedLength);
		pConn->UnregisterShutdownCB(TestShutdownThread, this);
		bWorkerDone = true;
	}
}

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// S3Write //////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

struct CS3WriteThread : public CSimpleWorkerThread
{
	// define here any thread-specific variables, if any
	CECSConnection::STREAM_CONTEXT WriteContext;
	CString sECSPath;					// path to ECS object to read
	CECSConnection *pConn;				// ECS connection object
	CECSConnection::S3_ERROR Error;		// returned status
	LARGE_INTEGER FileSize;
	CCngAES_GCM Hash;					// optional MD5 hash
	bool bGotHash;						// set if MD5 hash is complete
	bool bWorkerDone;					// set so it only runs once
	const list<CECSConnection::HEADER_STRUCT> *pMDList;	// optional metadata to send to object
	CEvent evWriteComplete;				// set when worker thread is finished writing to S3

	CS3WriteThread()
		: pConn(nullptr)
		, bGotHash(false)
		, bWorkerDone(false)
		, pMDList(nullptr)
		, evWriteComplete(FALSE, TRUE)
	{
		FileSize.QuadPart = 0;
	}
	~CS3WriteThread()
	{
		pConn = nullptr;
		pMDList = nullptr;
		KillThreadWait();
	}
	void DoWork();
};

// TestShutdownWriteThread
// pContext must point to CSimpleWorkerThread
static bool TestShutdownWriteThread(void *pContext)
{
	CS3WriteThread *pThread = (CS3WriteThread *)pContext;
	// test if thread is exiting
	if (pThread != nullptr)
	{
		if (pThread->bWorkerDone)
			return true;
		return pThread->GetExitFlag();
	}
	return false;
}

static DWORD CalcUploadChecksum(
	IStream *pStream,							// open stream to file
	ULONGLONG ullOffset,						// offset if reading a portion of the file
	ULONGLONG ullLength,						// length (if reading a portion) if 0 && ullOffset == 0, read the whole file
	CECSConnection& Conn,						// used for abort test
	CBuffer& ReadBuf,							// buffer to use
	CCngAES_GCM& Hash)							// out: return MD5 hash
{
	try
	{
		Hash.CreateHash(BCRYPT_MD5_ALGORITHM);
		DWORD dwNumRead;
		DWORD dwError;
		LARGE_INTEGER liOffset;
		liOffset.QuadPart = ullOffset;
		bool bPart = (ullOffset != 0ULL) || (ullLength != 0ULL);
		dwError = pStream->Seek(liOffset, STREAM_SEEK_SET, nullptr);
		if (dwError != S_OK)
			throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
		for (;;)
		{
			DWORD dwBytesToRead = ReadBuf.GetBufSize();
			if (bPart)
			{
				if ((ULONGLONG)dwBytesToRead > ullLength)
					dwBytesToRead = (DWORD)ullLength;
			}
			dwError = pStream->Read(ReadBuf.GetData(), dwBytesToRead, &dwNumRead);
			if ((dwError != S_OK) && (dwError != S_FALSE))
				throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
			if (dwNumRead == 0)
				break;						// didn't read anything
			liOffset.QuadPart += dwNumRead;
			Hash.AddHashData(ReadBuf.GetData(), dwNumRead);
			if (bPart)
			{
				ullLength -= (ULONGLONG)dwNumRead;
				if (ullLength == 0ULL)
					break;
			}
			if (Conn.TestAbort())
				throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_OPERATION_ABORTED);
		}
	}
	catch (const CErrorInfo& E)
	{
		return E.dwError;
	}
	return ERROR_SUCCESS;
}

// S3Write
// Set up a worker thread that will read the data from ECS and fill a memory queue
// the original thread will read the data off of the queue and write it to disk
CECSConnection::S3_ERROR S3Write(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	IStream *pStream,								// open stream to file
	const DWORD dwBufSize,							// size of buffer to use
	bool bChecksum,									// if set, include content-MD5 header
	DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
	const list<CECSConnection::HEADER_STRUCT> *pMDList,	// optional metadata to send to object
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext)											// context for UpdateProgressCB
{
	CS3WriteThread WriteThread;						// thread object
	CECSConnection::STREAM_DATA_ENTRY WriteRec;
	CBuffer WriteBuf;
	DWORD dwBytesRead;
	DWORD dwError;
	STATSTG FileStat;

	try
	{
		WriteBuf.SetBufSize(dwBufSize);				// how much data to write at a time
		WriteThread.pConn = &Conn;
		WriteThread.sECSPath = pszECSPath;
		WriteThread.pMDList = pMDList;
		dwError = pStream->Stat(&FileStat, STATFLAG_NONAME);
		if (dwError != S_OK)
			return dwError;
		WriteThread.FileSize.QuadPart = FileStat.cbSize.QuadPart;
		if (bChecksum)
		{
			dwError = CalcUploadChecksum(pStream, 0ULL, 0ULL, Conn, WriteBuf, WriteThread.Hash);
			if (dwError != ERROR_SUCCESS)
				return dwError;						// error creating the hash
			WriteThread.bGotHash = true;
		}
		WriteThread.WriteContext.UpdateProgressCB = UpdateProgressCB;
		WriteThread.WriteContext.pContext = pContext;
		// file is open and ready, now start up the worker thread so it starts writing to ECS
		WriteThread.CreateThread();				// create the thread
		WriteThread.StartWork();					// kick it off

		bool bDone = false;
		LARGE_INTEGER liOffset;
		liOffset.QuadPart = 0LL;
		dwError = pStream->Seek(liOffset, STREAM_SEEK_SET, nullptr);
		if (dwError != S_OK)
			throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
		while (!bDone)
		{
			if (WriteThread.bWorkerDone)
				break;
			if (Conn.TestAbort())
				break;
			dwError = pStream->Read(WriteBuf.GetData(), WriteBuf.GetBufSize(), &dwBytesRead);
			if ((dwError != S_OK) && (dwError != S_FALSE))
				throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
			if ((dwError == S_FALSE) || (dwBytesRead == 0))
				bDone = WriteRec.bLast = true;
			WriteRec.Data.Load(WriteBuf.GetData(), dwBytesRead);
			WriteRec.ullOffset = liOffset.QuadPart;
			liOffset.QuadPart += dwBytesRead;
			WriteThread.WriteContext.StreamData.push_back(WriteRec, dwMaxQueueSize, TestShutdownWriteThread, &WriteThread);
		}
		// wait for the worker thread to finish the S3 command
		for (;;)
		{
			if (WriteThread.bWorkerDone)
				break;
			dwError = WaitForSingleObject(WriteThread.evWriteComplete.m_hObject, SECONDS(2));
			if (dwError == WAIT_OBJECT_0)
				break;
			if (dwError != WAIT_TIMEOUT)
				throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
			// check for thread exit (but not if we are waiting for the handle to close)
			if (Conn.TestAbort())
				throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_OPERATION_ABORTED);
		}
		if (WriteThread.FileSize.QuadPart != liOffset.QuadPart)
		{
			DebugF(_T("Size Mismatch!!! Size at start: %I64d, Size of PUT: %I64d"), WriteThread.FileSize.QuadPart, liOffset.QuadPart);
			throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_BAD_LENGTH);
		}
		// now wait for the worker thread to terminate to get its error code
		WriteThread.KillThreadWait();			// kill background thread
	}
	catch (const CErrorInfo& E)
	{
		WriteThread.KillThreadWait();			// kill background thread
		if (WriteThread.Error.IfError())
			return WriteThread.Error;			// if the S3 call failed, that error is most likely more important than this one
		return E.dwError;
	}
	return WriteThread.Error;
}

void CS3WriteThread::DoWork()
{
	if (!bWorkerDone && (dwEventRet == WAIT_OBJECT_0))
	{
		CBuffer HashData;
		if (bGotHash)
			Hash.GetHashData(HashData);
		pConn->RegisterShutdownCB(TestShutdownWriteThread, this);
		Error = pConn->Create(sECSPath, nullptr, 0UL, pMDList, bGotHash ? &HashData : nullptr, &WriteContext, FileSize.QuadPart);
		pConn->UnregisterShutdownCB(TestShutdownWriteThread, this);
		VERIFY(evWriteComplete.SetEvent());							// notify parent thread that we're done here
		bWorkerDone = true;
	}
}

//////////////////////////////////////////////////////////////////////////////
/////////////////////////////// DoS3MultiPartUpload //////////////////////////
//////////////////////////////////////////////////////////////////////////////

struct CMPUPoolMsg;
struct CMPUPoolMsgEvents
{
	list<shared_ptr<CMPUPoolMsg>> *pMsgList;
	CEvent *pevMsg;						// event is set whenever an entry is complete
	bool bComplete;						// set when operation is complete

	CMPUPoolMsgEvents()
		: pMsgList(nullptr)
		, pevMsg(nullptr)
		, bComplete(false)
	{}

	// copy constructor
	CMPUPoolMsgEvents(const CMPUPoolMsgEvents& src)
	{
		(void)src;
		pMsgList = nullptr;
		pevMsg = nullptr;
		bComplete = false;
	};

	const CMPUPoolMsgEvents& operator =(const CMPUPoolMsgEvents& src)
	{
		if (&src == this)
			return *this;
		return *this;
	};
};

struct CMPUPoolMsg
{
	CECSConnection Conn;				// connection to S3 host
	CECSConnection::S3_ERROR Error;		// error code from part upload
	ULONGLONG lwWriteComplete;			// how many bytes have been written
	CMPUPoolMsgEvents Events;			// fields that 
	CBuffer Buf;						// (read,write) buffer to read into or write from
	CECSConnection::STREAM_CONTEXT *pStreamQueue;		// if stream send/receive, pointer to queue supplying the data
	ULONGLONG ullTotalLen;				// total length of the file, used if bStreamSend/StreamSendQueue are being used
	DWORD dwThreadId;					// used for debugging
										// S3 multipart upload
	shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY> pUploadPartEntry;		// if non-empty, multipart upload
	shared_ptr<CECSConnection::S3_UPLOAD_PART_INFO> MultiPartInfo;			// if non-empty, multipart upload

	CMPUPoolMsg()
		: lwWriteComplete(0ULL)
		, pStreamQueue(nullptr)
		, ullTotalLen(0ULL)
		, dwThreadId(GetCurrentThreadId())
	{
		(void)dwThreadId;
	}

	CMPUPoolMsg(
		const CECSConnection& ConnParam,
		list<shared_ptr<CMPUPoolMsg>> *pMsgListParam,
		CEvent *pevMsgParam,
		CECSConnection::STREAM_CONTEXT *pStreamQueueParam,
		ULONGLONG ullTotalLenParam
	)
		: Conn(ConnParam)
		, lwWriteComplete(0ULL)
		, pStreamQueue(pStreamQueueParam)
		, ullTotalLen(ullTotalLenParam)
		, dwThreadId(GetCurrentThreadId())
	{
		Events.pMsgList = pMsgListParam;
		Events.pevMsg = pevMsgParam;
		Events.bComplete = false;
	}
};

class CMPUPoolList
{
public:
	list<shared_ptr<CMPUPoolMsg>> PendingList;
	CEvent evPendingList;
	CCriticalSection csPendingList;
	CCngAES_GCM MsgHash;

	CMPUPoolList()
	{}

	virtual ~CMPUPoolList()
	{
		CSingleLock lock(&csPendingList, true);
		list<shared_ptr<CMPUPoolMsg>>::iterator itPendingList;
		for (itPendingList = PendingList.begin(); itPendingList != PendingList.end(); ++itPendingList)
		{
			(*itPendingList)->Events.pevMsg = nullptr;
			(*itPendingList)->Events.pMsgList = nullptr;
		}
	}
};

class CMPUPool : public CThreadPool<shared_ptr<CMPUPoolMsg>>
{
public:
	CMPUPoolList Pending;
	bool DoProcess(const CSimpleWorkerThread *pThread, const shared_ptr<CMPUPoolMsg>& Msg);
	bool SearchEntry(const shared_ptr<CMPUPoolMsg>& Msg1, const shared_ptr<CMPUPoolMsg>& Msg2) const;
	CMPUPool()
	{}
	~CMPUPool()
	{
		CThreadPool<shared_ptr<CMPUPoolMsg>>::Terminate();
	}
};

// used where a class method can't be used
static bool TestAbortStatic(void *pContext)
{
	CECSConnection *pConn = (CECSConnection *)pContext;
	return pConn->TestAbort();
}


// DoS3MultiPartUpload
// manage a S3 multipart upload
// the S3PartList must have at least 1 entry
// "throw" any errors
// this thread manages reading the file to feed the thread pool and the streaming write operations
// returns 'false' if it didn't do the upload
bool DoS3MultiPartUpload(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	IStream *pStream,								// open stream to file
	const DWORD dwBufSize,							// size of buffer to use
	const DWORD dwPartSize,							// part size (in MB)
	const DWORD dwMaxThreads,						// maxiumum number of threads to spawn
	bool bChecksum,									// if set, include content-MD5 header
	const list<CECSConnection::HEADER_STRUCT> *pMDList,	// optional metadata to send to object
	DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
	DWORD dwMaxRetries,									// how many times to retry a part before giving up
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext,											// context for UpdateProgressCB
	CECSConnection::S3_ERROR& Error)						// returned error
{
	const DWORD dwMaxParts = 1000;							// don't go over 1000 parts
	CSyncObject *EventArray[MAXIMUM_WAIT_OBJECTS];
	DWORD nEventList;
	CBuffer Buf;
	shared_ptr<CECSConnection::S3_UPLOAD_PART_INFO> MultiPartInfo;
	bool bStartedMultipartUpload = false;
	list<shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY>> S3PartList;
	CMPUPool MPUPool;
	STATSTG FileStat;
	DWORD dwError;

	Error = CECSConnection::S3_ERROR();			// clear the error return
	if (dwMaxQueueSize == 0)
		dwMaxQueueSize = 10;					// reasonable default
	try
	{
		dwError = pStream->Stat(&FileStat, STATFLAG_NONAME);
		if (dwError != S_OK)
		{
			Error = dwError;
			return false;
		}
		Buf.SetBufSize(dwBufSize);
		S3PartList.clear();
		if ((FileStat.cbSize.QuadPart < MEGABYTES(dwPartSize)))
			return false;
		ULONGLONG ullPartLength = ALIGN_ANY(MEGABYTES((ULONGLONG)dwPartSize), 0x10000);
		// don't let the number of parts go over dwMaxParts
		if (FileStat.cbSize.QuadPart / ullPartLength > dwMaxParts)
		{
			ullPartLength = FileStat.cbSize.QuadPart / (ULONGLONG)(dwMaxParts - 1);
		}
		if (ullPartLength < MEGABYTES(5))					// if the part size goes below the minimum
			return false;									// don't do a multipart upload
		ULONGLONG ullOffset = 0ULL;
		DWORD uPartNum = 0;
		// now create the part list
		for (;;)
		{
			if (FileStat.cbSize.QuadPart <= ullOffset)
				break;
			shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY> Rec = make_shared<CECSConnection::S3_UPLOAD_PART_ENTRY>();
			Rec->ullBaseOffset = ullOffset;
			Rec->uPartNum = ++uPartNum;
			Rec->ullPartSize = ((FileStat.cbSize.QuadPart - ullOffset) < ullPartLength) ? (FileStat.cbSize.QuadPart - ullOffset) : ullPartLength;
			Rec->sETag.Empty();
			S3PartList.push_back(Rec);
			ullOffset += Rec->ullPartSize;
		}
		// if there is only 1 entry, don't bother doing a multipart upload
		if (S3PartList.size() <= 1)
			return false;
		// make sure the thread pool is at least 1 thread
		MPUPool.SetMinThreads(1);
		if (dwMaxThreads == 0)
			throw CECSConnection::CS3ErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_PARAMETER);
		MPUPool.SetMaxThreads(dwMaxThreads);
		CThreadPoolBase::SetPoolInitialized();
		MultiPartInfo.reset(new CECSConnection::S3_UPLOAD_PART_INFO);
		// start up a multipart upload
		Error = Conn.S3MultiPartInitiate(pszECSPath, *MultiPartInfo, ((pMDList != nullptr) && (!pMDList->empty())) ? pMDList : nullptr);
		if (Error.IfError())
			throw CECSConnection::CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
		bStartedMultipartUpload = true;
		for (list<shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY>>::iterator itList = S3PartList.begin(); itList != S3PartList.end(); ++itList)
		{
			CECSConnection::S3_UPLOAD_PART_ENTRY *pEntry = itList->get();
			pEntry->bInProcess = false;
			pEntry->bComplete = false;
			pEntry->dwRetryNum = 0;
			pEntry->Checksum.Empty();
			pEntry->StreamQueue.StreamData.clear();
			pEntry->ullCursor = 0ULL;
			pEntry->StreamQueue.bMultiPart = true;
			pEntry->StreamQueue.UpdateProgressCB = UpdateProgressCB;
			pEntry->StreamQueue.pContext = pContext;
		}
		for (;;)
		{
			bool bS3PartListEmpty = true;
			for (list<shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY>>::iterator itList = S3PartList.begin(); itList != S3PartList.end(); ++itList)
			{
				if ((*itList)->bComplete)
					continue;							// skip over complete entries
				bS3PartListEmpty = false;				// at least one non-complete entry exists
				if ((*itList)->bInProcess)
					continue;							// skip over in-process entries
				if (MPUPool.GetMsgQueueCount() >= (dwMaxThreads + 4))
					break;													// we have enough for now
				if (Conn.TestAbort())
					throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_OPERATION_ABORTED);
				shared_ptr<CMPUPoolMsg> Msg = make_shared<CMPUPoolMsg>(Conn, &MPUPool.Pending.PendingList, &MPUPool.Pending.evPendingList, &(*itList)->StreamQueue, (*itList)->ullPartSize);
				{
					CSingleLock lock(&MPUPool.Pending.csPendingList, true);
					Msg->Events.bComplete = false;
					Msg->pUploadPartEntry = *itList;
					Msg->MultiPartInfo = MultiPartInfo;
					MPUPool.Pending.PendingList.push_back(Msg);
					(*itList)->Event.QueueEvent.Link(&(*itList)->StreamQueue.StreamData);					// link the queue to the event
					(*itList)->Event.QueueEvent.DisableAllTriggerEvents();
					(*itList)->Event.QueueEvent.EnableTriggerEvents(TRIGGEREVENTS_DELETE | TRIGGEREVENTS_POP);
					(*itList)->Event.QueueEvent.SetAllEvents();
					(*itList)->Event.QueueEvent.Enable();
				}
				(*itList)->bInProcess = true;											// mark the entry as being in-process
																						// if checksum, calculate it now
				{
					(*itList)->StreamQueue.StreamData.clear();
					if (bChecksum)
						MPUPool.Pending.MsgHash.CreateHash(BCRYPT_MD5_ALGORITHM);			// make sure it is a clean slate
					LARGE_INTEGER liChecksumOffset;
					liChecksumOffset.QuadPart = (*itList)->ullBaseOffset;
					ULONGLONG ullPartSize = (*itList)->ullPartSize;
					DWORD dwNumRead, dwReadBufSize;
					DWORD dwRecCount = 0;
					dwError = pStream->Seek(liChecksumOffset, STREAM_SEEK_SET, nullptr);
					if (dwError != S_OK)
						throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
					while (ullPartSize > 0ULL)
					{
						dwReadBufSize = Buf.GetBufSize();
						if ((ULONGLONG)dwReadBufSize > ullPartSize)
							dwReadBufSize = (DWORD)ullPartSize;
						dwError = pStream->Read(Buf.GetData(), dwReadBufSize, &dwNumRead);
						if ((dwError != S_OK) && (dwError != S_FALSE))
							throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
						if (bChecksum)
							MPUPool.Pending.MsgHash.AddHashData(Buf.GetData(), dwNumRead);
						// as long as we've read the data, might as well queue it to be uploaded
						// save another read in a short while
						if (dwRecCount < dwMaxQueueSize)
						{
							CECSConnection::STREAM_DATA_ENTRY StreamMsg;
							StreamMsg.Data.Load(Buf.GetData(), dwNumRead);
							StreamMsg.bLast = ullPartSize == (ULONGLONG)dwNumRead;
							(*itList)->StreamQueue.StreamData.push_back(StreamMsg, 0, TestAbortStatic, &Conn);
							(*itList)->ullCursor += (ULONGLONG)dwNumRead;
							dwRecCount++;
						}
						liChecksumOffset.QuadPart += dwNumRead;
						ullPartSize -= (ULONGLONG)dwNumRead;
						if (!bChecksum && (dwRecCount >= dwMaxQueueSize))
							break;									// if no checksum, we don't need to go through the whole segment
					}
					if (bChecksum)
					{
						MPUPool.Pending.MsgHash.GetHashData((*itList)->Checksum);
					}
				}
				{
					shared_ptr<shared_ptr<CMPUPoolMsg>> AutoMsg;
					AutoMsg.reset(new shared_ptr<CMPUPoolMsg>(Msg));
					MPUPool.SendMessageToPool(__LINE__, AutoMsg, 0, 0, nullptr);
				}
			}
			// check if the part list is empty. if so, we're done!
			if (bS3PartListEmpty)
				break;
			// if it gets here, there are enough entries in the thread pool queue to keep it busy for a while
			// now we need to collect events to feed the queues and check for completion
			nEventList = 0;
			// first get all events for filling the stream queues
			{
				CSingleLock lock(&MPUPool.Pending.csPendingList, true);
				for (list<shared_ptr<CMPUPoolMsg>>::const_iterator itPending = MPUPool.Pending.PendingList.begin();
					itPending != MPUPool.Pending.PendingList.end();
					++itPending)
				{
					if (nEventList >= (MAXIMUM_WAIT_OBJECTS - 2))
					{
						ASSERT(false);						// this should never happen
						break;
					}
					if (!(*itPending)->Events.bComplete && (*itPending)->pUploadPartEntry)
					{
						// stream data events
						EventArray[nEventList] = &(*itPending)->pUploadPartEntry->Event.QueueEvent.Event.evQueue;
						nEventList++;
						// completion events
						if ((*itPending)->Events.pevMsg != nullptr)
						{
							EventArray[nEventList] = (*itPending)->Events.pevMsg;
							nEventList++;
						}
					}
				}
			}
			CMultiLock cml(EventArray, nEventList);
			for (;;)
			{
				bool bDeletedAtLeastOneEntry = false;
				(void)cml.Lock(SECONDS(5), FALSE, QS_ALLINPUT);	// wait for event or timeout
				(void)cml.Unlock();
				if (Conn.TestAbort())
					throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_OPERATION_ABORTED);
				// first check completion codes and get rid of any entries that are complete
				{
					CSingleLock lock(&MPUPool.Pending.csPendingList, true);
					for (list<shared_ptr<CMPUPoolMsg>>::const_iterator itPending = MPUPool.Pending.PendingList.begin();
						itPending != MPUPool.Pending.PendingList.end();
						)
					{
						bool bDeleteEntry = false;
						CECSConnection::S3_UPLOAD_PART_ENTRY *pPartEntry = (*itPending)->pUploadPartEntry.get();
						if ((*itPending)->Events.bComplete)
						{
							// done! see if it was successful
							if ((*itPending)->Error.IfError())
							{
								// error, check if we have any retries left
								if ((*itPending)->pUploadPartEntry->dwRetryNum >= dwMaxRetries)
									throw CECSConnection::CS3ErrorInfo(_T(__FILE__), __LINE__, (*itPending)->Error);
								// reset eveything so it gets retried
								pPartEntry->bInProcess = false;
								pPartEntry->bComplete = false;
								pPartEntry->dwRetryNum++;
								pPartEntry->Checksum.Empty();
								pPartEntry->StreamQueue.StreamData.clear();
								pPartEntry->ullCursor = 0ULL;
								(*itPending)->Events.bComplete = false;
							}
							else
							{
								// success - now mark the PartEntry as complete
								for (list<shared_ptr<CECSConnection::S3_UPLOAD_PART_ENTRY>>::iterator itList = S3PartList.begin(); itList != S3PartList.end(); ++itList)
								{
									if (itList->get() == (*itPending)->pUploadPartEntry.get())
									{
										pPartEntry->bComplete = true;
										break;
									}
								}
							}
							bDeleteEntry = true;
							bDeletedAtLeastOneEntry = true;
						}
						if (!bDeleteEntry)
						{
							lock.Unlock();
							LARGE_INTEGER liOffset;
							liOffset.QuadPart = pPartEntry->ullBaseOffset + pPartEntry->ullCursor;
							dwError = pStream->Seek(liOffset, STREAM_SEEK_SET, nullptr);
							if (dwError != S_OK)
								throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
							// check if we could add more data to the stream queues
							while ((pPartEntry->StreamQueue.StreamData.GetCount() < dwMaxQueueSize)
								&& (pPartEntry->ullPartSize > pPartEntry->ullCursor))
							{
								DWORD dwNumRead, dwReadBufSize;
								ULONGLONG ullRemainingPart;
								dwReadBufSize = Buf.GetBufSize();
								ullRemainingPart = pPartEntry->ullPartSize - pPartEntry->ullCursor;
								if ((ULONGLONG)dwReadBufSize > ullRemainingPart)
									dwReadBufSize = (DWORD)ullRemainingPart;
								dwError = pStream->Read(Buf.GetData(), dwReadBufSize, &dwNumRead);
								if ((dwError != S_OK) && (dwError != S_FALSE))
									throw CErrorInfo(_T(__FILE__), __LINE__, dwError);
								CECSConnection::STREAM_DATA_ENTRY StreamMsg;
								StreamMsg.Data.Load(Buf.GetData(), dwNumRead);
								StreamMsg.bLast = pPartEntry->ullPartSize <= (pPartEntry->ullCursor + dwNumRead);
								pPartEntry->StreamQueue.StreamData.push_back(StreamMsg, 0, TestAbortStatic, &Conn);
								pPartEntry->ullCursor += dwNumRead;
							}
							lock.Lock();
						}
						if (bDeleteEntry)
							itPending = MPUPool.Pending.PendingList.erase(itPending);
						else
							++itPending;
					}
				}
				if (bDeletedAtLeastOneEntry)
					break;								// get more from the part list
			}
		}
		// done!
		// complete the upload. tell the server to reassemble all the parts
		CECSConnection::S3_MPU_COMPLETE_INFO MPUCompleteInfo;
		Error = Conn.S3MultiPartComplete(*MultiPartInfo, S3PartList, MPUCompleteInfo);
		if (Error.IfError())
			throw CECSConnection::CS3ErrorInfo(_T(__FILE__), __LINE__, Error);
	}
	catch (const CECSConnection::CS3ErrorInfo& E)
	{
		if (bStartedMultipartUpload)
		{
			Conn.CheckShutdown(false);
			Error = Conn.S3MultiPartAbort(*MultiPartInfo);
			Conn.CheckShutdown(true);
		}
		Error = E.Error;
		return false;
	}
	catch (const CErrorInfo& E)
	{
		if (bStartedMultipartUpload)
		{
			Conn.CheckShutdown(false);
			Error = Conn.S3MultiPartAbort(*MultiPartInfo);
			Conn.CheckShutdown(true);
		}
		Error = E.dwError;
		return false;
	}
	return true;
}

bool CMPUPool::DoProcess(const CSimpleWorkerThread * pThread, const shared_ptr<CMPUPoolMsg>& Msg)
{
	{
		CSingleLock lock(&Pending.csPendingList, true);
		// check if this request has already been aborted
		if ((Msg->Events.pevMsg == nullptr) || (Msg->Events.pMsgList == nullptr))
			return true;
	}
	{
		CTestShutdown Shutdown(pThread, &Msg->Conn);
		// S3 multipart upload
		Msg->Error = Msg->Conn.S3MultiPartUpload(*Msg->MultiPartInfo, *Msg->pUploadPartEntry, Msg->pStreamQueue, Msg->ullTotalLen, nullptr, 0ULL, nullptr);
	}
	{
		CSingleLock lock(&Pending.csPendingList, true);
		Msg->lwWriteComplete += Msg->Buf.GetBufSize();
		Msg->Buf.Empty();							// don't need the data anymore. free it up now
	}
	{
		CSingleLock lock(&Pending.csPendingList, true);
		Msg->Events.bComplete = true;
		if ((Msg->Events.pevMsg != nullptr) && (Msg->Events.pMsgList != nullptr))
		{
			(void)Msg->Events.pevMsg->SetEvent();
		}
	}
	return true;
}

bool CMPUPool::SearchEntry(const shared_ptr<CMPUPoolMsg>& Msg1, const shared_ptr<CMPUPoolMsg>& Msg2) const
{
	(void)Msg1;
	(void)Msg2;
	return false;
}
