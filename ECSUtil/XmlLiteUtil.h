//
// Copyright (C) 1994 - 2011 EMC Corporation
// All rights reserved.
//

#pragma once
#include "stdafx.h"
#include "exportdef.h"
#include <afx.h>
#include <Objidl.h>
#include <xmllite.h>
#include "cbuffer.h"
#include "widestring.h"

using namespace std;

struct XML_LITE_ATTRIB
{
	CString sAttrName;
	CString sValue;
};

typedef HRESULT (*XMLLITE_READER_CB)(const CString& sXmlPath, void *pContext, IXmlReader *pReader, XmlNodeType NodeType, const list<XML_LITE_ATTRIB> *pAttrList, const CString *psValue);

class ECSUTIL_EXT_CLASS CBufferStream : public IStream
{
private:
	void Register()
	{
#ifdef DEBUG_DUMP_QUEUES
		if (pcsGlobalCBufferStreamSet == NULL)
		{
			CCriticalSection *pcsGlobalThreadTemp = new CCriticalSection;			//lint !e1732 !e1733 (Info -- new in constructor for class 'CSimpleWorkerThread' which has no assignment operator)
			if (InterlockedCompareExchangePointer((void **)&pcsGlobalCBufferStreamSet, pcsGlobalThreadTemp, NULL) != NULL)
				delete pcsGlobalThreadTemp;
		}
		ASSERT(pcsGlobalCBufferStreamSet != NULL);
		CSingleLock lockGlobalList(pcsGlobalCBufferStreamSet, true);
		if (pGlobalCBufferStreamSet == NULL)
			pGlobalCBufferStreamSet = new std::set<CBufferStream *>;
		(void)pGlobalCBufferStreamSet->insert(this);
#endif
	}

public:
	CBufferStream(CBuffer *pBuf = NULL)
	{
#ifdef DEBUG_DUMP_QUEUES
		Register();
#endif
		_refcount = 0;
		_pBuf = pBuf;
		if (_pBuf == NULL)
			_pBuf = &_InternalBuf;
		_liPosition.QuadPart = 0;
		bReadOnly = false;
	}

	CBufferStream(const CBuffer *pBuf)
	{
#ifdef DEBUG_DUMP_QUEUES
		Register();
#endif
		_refcount = 0;
		_pBuf = const_cast<CBuffer *> (pBuf);
		if (_pBuf == NULL)
			_pBuf = &_InternalBuf;
		_liPosition.QuadPart = 0;
		bReadOnly = true;
	}

	virtual ~CBufferStream()
	{
		ASSERT(_refcount == 0);
#ifdef DEBUG_DUMP_QUEUES
		if (pcsGlobalCBufferStreamSet && pGlobalCBufferStreamSet)
		{
			CSingleLock lockGlobalList(pcsGlobalCBufferStreamSet, true);
			(void)pGlobalCBufferStreamSet->erase(this);
		}
#endif
	}

public:
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void ** ppvObject)
	{
		if (iid == __uuidof(IUnknown)
			|| iid == __uuidof(IStream)
			|| iid == __uuidof(ISequentialStream))
		{
			*ppvObject = static_cast<IStream*>(this);
			(void)AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef(void)
	{
		return (ULONG)InterlockedIncrement(&_refcount);
	}

	virtual ULONG STDMETHODCALLTYPE Release(void)
	{
		ULONG res = (ULONG) InterlockedDecrement(&_refcount);
		if (res == 0)
			delete this;
		return res;
	}

	// ISequentialStream Interface

public:
	virtual HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead)
	{
		ASSERT(_pBuf != NULL);
		ULONG uBytesRead = 0;
		if (pcbRead == NULL)
			pcbRead = &uBytesRead;
		if ((DWORD)_liPosition.QuadPart < _pBuf->GetBufSize())
		{
			DWORD dwRemainingBytes = _pBuf->GetBufSize() - (DWORD)_liPosition.QuadPart;
			if (cb > dwRemainingBytes)
				*pcbRead = dwRemainingBytes;
			else
				*pcbRead = cb;
			memcpy(pv, _pBuf->GetData() + (DWORD)_liPosition.QuadPart, *pcbRead);
			_liPosition.LowPart += *pcbRead;
			_liPosition.HighPart = 0;
			return S_OK;
		}
		*pcbRead = 0;
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE Write(void const* pv, ULONG cb, ULONG* pcbWritten)
	{
		if (bReadOnly)
			return E_ACCESSDENIED;
		ASSERT(_pBuf != NULL);
		if (((DWORD)_liPosition.QuadPart + cb) > _pBuf->GetBufSize())
		{
			_pBuf->SetBufSize((DWORD)_liPosition.QuadPart + cb + 8196);
			_pBuf->SetBufSize((DWORD)_liPosition.QuadPart + cb);
		}
		memcpy(_pBuf->GetData() + (DWORD)_liPosition.QuadPart, pv, cb);
		_liPosition.LowPart += cb;
		_liPosition.HighPart = 0;
		if (pcbWritten != NULL)
			*pcbWritten = cb;
		return S_OK;
	}

	// IStream Interface

public:
	virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*,
	ULARGE_INTEGER*)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE Commit(DWORD)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE Revert(void)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE Clone(IStream **)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER liDistanceToMove, DWORD dwOrigin,
		ULARGE_INTEGER* lpNewFilePointer)
	{
		switch(dwOrigin)
		{
		case STREAM_SEEK_SET:
			_liPosition.LowPart = liDistanceToMove.LowPart;
			_liPosition.HighPart = liDistanceToMove.HighPart;
			break;
		case STREAM_SEEK_CUR:
			_liPosition.LowPart += liDistanceToMove.LowPart;
			break;
		case STREAM_SEEK_END:
			_liPosition.LowPart = _pBuf->GetBufSize() + liDistanceToMove.LowPart;
			break;
		default:
			return STG_E_INVALIDFUNCTION;
		break;
	}
	_liPosition.HighPart = 0;
	if (lpNewFilePointer != NULL)
		*lpNewFilePointer = _liPosition;

	return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG* pStatstg, DWORD grfStatFlag)
	{
		(void)grfStatFlag;
		ASSERT(_pBuf != NULL);
		pStatstg->cbSize.QuadPart = _pBuf->GetBufSize();
		return S_OK;
	}

	CString GetXml()
	{
		CWideString WideBuf;
		WideBuf.Set((LPCSTR)_pBuf->GetData(), -1, CP_UTF8);
		return (LPCTSTR)WideBuf;
	}

	CString Format()
	{
		CString sMsg;
		sMsg.Format(L"CBufferStream: Position: %I64u, Ref: %d, Readonly: %d", _liPosition.QuadPart, _refcount, (int)bReadOnly);
		return sMsg;
	}

	static void DumpHandles(CString& sHandleMsg)
	{
#ifdef DEBUG_DUMP_QUEUES
		if (!pcsGlobalCBufferStreamSet)
			return;
		CSingleLock lockGlobalList(pcsGlobalCBufferStreamSet, true);
		if (!pGlobalCBufferStreamSet)
			return;
		for (std::set<CBufferStream *>::iterator itSet=pGlobalCBufferStreamSet->begin() ; itSet != pGlobalCBufferStreamSet->end() ; ++itSet)
		{
			sHandleMsg += (*itSet)->Format();
		}
#else
		(void)sHandleMsg;
#endif
	}

private:
#ifdef DEBUG_DUMP_QUEUES
	static CCriticalSection *pcsGlobalCBufferStreamSet;
	static std::set<CBufferStream *> *pGlobalCBufferStreamSet;
#endif
	CBuffer *_pBuf, _InternalBuf;
	ULARGE_INTEGER _liPosition;
	LONG _refcount;
	bool bReadOnly;
};

//implement filestream that derives from IStream
class FileStream : public IStream
{
	FileStream(HANDLE hFile)
	{
		_refcount = 1;
		_hFile = hFile;
	}

	virtual ~FileStream()
	{
		if (_hFile != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(_hFile);
		}
	}

public:
	static HRESULT OpenFile(LPCWSTR pName, IStream ** ppStream, bool fWrite)
	{
		HANDLE hFile = ::CreateFileW(pName, fWrite ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ,
			NULL, fWrite ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hFile == INVALID_HANDLE_VALUE)
			return HRESULT_FROM_WIN32(GetLastError());

		*ppStream = new FileStream(hFile);

		if (*ppStream == NULL)
			CloseHandle(hFile);

		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void ** ppvObject)
	{
		if (iid == __uuidof(IUnknown)
			|| iid == __uuidof(IStream)
			|| iid == __uuidof(ISequentialStream))
		{
			*ppvObject = static_cast<IStream*>(this);
			AddRef();
			return S_OK;
		}
		else
			return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef(void)
	{
		return (ULONG)InterlockedIncrement(&_refcount);
	}

	virtual ULONG STDMETHODCALLTYPE Release(void)
	{
		ULONG res = (ULONG)InterlockedDecrement(&_refcount);
		if (res == 0)
			delete this;
		return res;
	}

	// ISequentialStream Interface
public:
	virtual HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead)
	{
		BOOL rc = ReadFile(_hFile, pv, cb, pcbRead, NULL);
		return (rc) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	}

	virtual HRESULT STDMETHODCALLTYPE Write(void const* pv, ULONG cb, ULONG* pcbWritten)
	{
		BOOL rc = WriteFile(_hFile, pv, cb, pcbWritten, NULL);
		return rc ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	}

	// IStream Interface
public:
	virtual HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*,
		ULARGE_INTEGER*)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE Commit(DWORD)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE Revert(void)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE Clone(IStream **)
	{
		return E_NOTIMPL;
	}

	virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER liDistanceToMove, DWORD dwOrigin,
		ULARGE_INTEGER* lpNewFilePointer)
	{
		DWORD dwMoveMethod;

		switch (dwOrigin)
		{
		case STREAM_SEEK_SET:
			dwMoveMethod = FILE_BEGIN;
			break;
		case STREAM_SEEK_CUR:
			dwMoveMethod = FILE_CURRENT;
			break;
		case STREAM_SEEK_END:
			dwMoveMethod = FILE_END;
			break;
		default:
			return STG_E_INVALIDFUNCTION;
			break;
		}

		if (SetFilePointerEx(_hFile, liDistanceToMove, (PLARGE_INTEGER)lpNewFilePointer,
			dwMoveMethod) == 0)
			return HRESULT_FROM_WIN32(GetLastError());
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG* pStatstg, DWORD grfStatFlag)
	{
		if (GetFileSizeEx(_hFile, (PLARGE_INTEGER)&pStatstg->cbSize) == 0)
			return HRESULT_FROM_WIN32(GetLastError());
		return S_OK;
	}

private:
	HANDLE _hFile;
	LONG _refcount;
};

HRESULT ECSUTIL_EXT_API ScanXml(
	const CBuffer *pXml,
	void *pContext,
	XMLLITE_READER_CB ReaderCB);

HRESULT ECSUTIL_EXT_API ScanXmlStream(
	IStream *pStream,
	void *pContext,
	XMLLITE_READER_CB ReaderCB);
