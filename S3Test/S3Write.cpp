// S3Write.cpp : File read
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

struct CS3WriteThread : public CSimpleWorkerThread
{
	// define here any thread-specific variables, if any
	CECSConnection::STREAM_CONTEXT WriteContext;
	CString sECSPath;					// path to ECS object to read
	CECSConnection *pConn;				// ECS connection object
	CECSConnection::S3_ERROR Error;		// returned status
	LARGE_INTEGER FileSize;

	CS3WriteThread()
		: pConn(nullptr)
	{
		FileSize.QuadPart = 0;
	}
	~CS3WriteThread()
	{
		KillThreadWait();
	}
	void DoWork();
};

// S3Write
// Set up a worker thread that will read the data from ECS and fill a memory queue
// the original thread will read the data off of the queue and write it to disk
CECSConnection::S3_ERROR S3Write(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszFilePath,							// path to write file
	LPCTSTR pszECSPath)								// path to object in format: /bucket/dir1/dir2/object
{
	CS3WriteThread WriteThread;						// thread object
	CSharedQueueEvent MsgEvent;						// event that new data was pushed on the read queue
	CECSConnection::STREAM_DATA_ENTRY WriteRec;
	CBuffer WriteBuf;
	DWORD dwBytesRead;

	WriteBuf.SetBufSize(KILOBYTES(10));				// how much data to write at a time
	WriteThread.pConn = &Conn;
	WriteThread.sECSPath = pszECSPath;
	// TODO: set WriteThread.WriteContext.UpdateProgressCB,pContext if you want callbacks on sending progress
	CHandle hFile(CreateFile(pszFilePath, FILE_GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (hFile.m_h == INVALID_HANDLE_VALUE)
	{
		_tprintf(_T("Open error for %s: %s\n"), pszFilePath, (LPCTSTR)GetNTLastErrorText());
		return 1;
	}
	if (!GetFileSizeEx(hFile, &WriteThread.FileSize))
	{
		_tprintf(_T("Error getting file size for %s: %s\n"), pszFilePath, (LPCTSTR)GetNTLastErrorText());
		return 1;
	}
	// file is open and ready, now start up the worker thread so it starts writing to ECS
	WriteThread.CreateThread();				// create the thread
	WriteThread.StartWork();					// kick it off

	bool bDone = false;
	ULONGLONG ullOffset = 0ULL;
	while (!bDone)
	{
		if (!ReadFile(hFile, WriteBuf.GetData(), WriteBuf.GetBufSize(), &dwBytesRead, nullptr))
		{
			_tprintf(_T("Read error from %s: %s\n"), pszFilePath, (LPCTSTR)GetNTLastErrorText());
			return 1;
		}
		WriteRec.Data.Load(WriteBuf.GetData(), dwBytesRead);
		bDone = WriteRec.bLast = dwBytesRead == 0;
		WriteRec.ullOffset = ullOffset;
		ullOffset += dwBytesRead;
		WriteThread.WriteContext.StreamData.push_back(WriteRec, 2, TestShutdown, &WriteThread);
		_tprintf(_T("Offset: %-20I64d\r"), ullOffset);
	}
	_tprintf(_T("\n"));		// leave offset line on the screen
	// now wait for the worker thread to terminate to get its error code
	WriteThread.KillThreadWait(true);			// don't kill it - just wait for it to die
	return WriteThread.Error;
}

void CS3WriteThread::DoWork()
{
	CBuffer RetData;
	pConn->RegisterShutdownCB(TestShutdown, this);
	Error = pConn->Create(sECSPath, nullptr, 0UL, nullptr, nullptr, nullptr, &WriteContext, FileSize.QuadPart);
	pConn->UnregisterShutdownCB(TestShutdown);
	KillThread();
}
