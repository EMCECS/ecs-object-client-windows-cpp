// S3Read.cpp : File read
//

#include "stdafx.h"
#include <afxsock.h>
#include <list>
#include <deque>
#include "S3Test.h"
#include "ECSUtil.h"
#include "ECSConnection.h"
#include "NTERRTXT.H"
#include "SimpleWorkerThread.h"

bool TestShutdown(void *pContext)
{
	// TODO: determine if system or process is shutting down. return true if the operation needs to be cancelled
	// assume pContext is a pointer to a CSimpleWorkerThread
	// test if thread is exiting
	if (pContext != nullptr)
	{
		CSimpleWorkerThread *pThread = (CSimpleWorkerThread *)pContext;
		if (pThread->GetExitFlag())
			return true;								// thread is exiting, abort the operation
	}
	return false;
}

struct CS3ReadThread : public CSimpleWorkerThread
{
	// define here any thread-specific variables, if any
	CECSConnection::STREAM_CONTEXT ReadContext;
	CString sECSPath;					// path to ECS object to read
	CECSConnection *pConn;				// ECS connection object
	CECSConnection::S3_ERROR Error;		// returned status

	CS3ReadThread()
		: pConn(nullptr)
	{}
	~CS3ReadThread()
	{
		KillThreadWait();
	}
	void DoWork();
};

// S3Read
// Set up a worker thread that will read the data from ECS and fill a memory queue
// the original thread will read the data off of the queue and write it to disk
CECSConnection::S3_ERROR S3Read(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszFilePath,							// path to write file
	LPCTSTR pszECSPath)								// path to object in format: /bucket/dir1/dir2/object
{
	CS3ReadThread ReadThread;						// thread object
	CSharedQueueEvent MsgEvent;						// event that new data was pushed on the read queue
	DWORD dwError;

	ReadThread.pConn = &Conn;
	ReadThread.sECSPath = pszECSPath;
	CHandle hFile(CreateFile(pszFilePath, FILE_GENERIC_READ | FILE_GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (hFile.m_h == INVALID_HANDLE_VALUE)
	{
		_tprintf(_T("Open error for %s: %s\n"), pszFilePath, (LPCTSTR)GetNTLastErrorText());
		return 1;
	}
	
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
		dwError = WaitForSingleObject(MsgEvent.Event.evQueue.m_hObject, SECONDS(10));
		// check for thread exit (but not if we are waiting for the handle to close)
		if (TestShutdown(nullptr))
			return ERROR_OPERATION_ABORTED;
		// check if the background thread has ended with an error
		if (!ReadThread.IfActive() && ReadThread.Error.IfError())
			break;
		if (dwError == WAIT_FAILED)
			return GetLastError();
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
					if (!WriteFile(hFile, StreamData.Data.GetData(), StreamData.Data.GetBufSize(), &dwNumWritten, nullptr))
						return GetLastError();
					_tprintf(_T("Offset: %-20I64d\r"), StreamData.ullOffset + StreamData.Data.GetBufSize());
				}
				if (StreamData.bLast)
				{
					bDone = true;
					break;								// done!
				}
			}
		}
	}
	_tprintf(_T("\n"));		// leave offset line on the screen
	// now wait for the worker thread to terminate to get its error code
	ReadThread.KillThreadWait();
	return ReadThread.Error;
}

void CS3ReadThread::DoWork()
{
	CBuffer RetData;
	pConn->RegisterShutdownCB(TestShutdown, this);
	Error = pConn->Read(sECSPath, 0ULL, 0ULL, RetData, 0UL, &ReadContext);
	pConn->UnregisterShutdownCB(TestShutdown);
	KillThread();
}
