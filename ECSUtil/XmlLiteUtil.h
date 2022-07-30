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

#pragma once
#include "stdafx.h"
#include "exportdef.h"
#include <afx.h>
#include <Objidl.h>
#include <xmllite.h>
#include "cbuffer.h"
#include "widestring.h"


namespace ecs_sdk
{
	// defines for Process
	enum class E_XML_FIELD_TYPE
	{
		Invalid,
		String,
		Time,
		U32,
		U64,
		S32,
		S64,
		Bool,
	};

	struct XML_FIELD_ENTRY
	{
		E_XML_FIELD_TYPE Type = E_XML_FIELD_TYPE::Invalid;
		UINT uOffset = 0;
	};

	// use FIELD_ENTRY_INIT if C++ struct field name is the same as the XML field name
#define FIELD_ENTRY_INIT(field_type, field_name) {L#field_name, {E_XML_FIELD_TYPE::field_type, UFIELD_OFFSET(CECSConnection::ECS_BUCKET_INFO, field_name)}}
	// use FIELD_ENTRY_INIT_XML if C++ struct field name is NOT the same as the XML field name
#define FIELD_ENTRY_INIT_XML(field_type, field_name, xml_name) {L#xml_name, {E_XML_FIELD_TYPE::field_type, UFIELD_OFFSET(CECSConnection::ECS_BUCKET_INFO, field_name)}}


	struct XML_LITE_ATTRIB
	{
		CStringW sAttrName;
		CStringW sValue;
	};

	typedef HRESULT(*XMLLITE_READER_CB)(const CStringW& sXmlPath, void* pContext, IXmlReader* pReader, XmlNodeType NodeType, const std::list<XML_LITE_ATTRIB>* pAttrList, const CStringW* psValue);

	class ECSUTIL_EXT_CLASS CBufferStream : public IStream
	{
	private:
		void Register()
		{
#ifdef DEBUG_DUMP_QUEUES
			if (pcsGlobalCBufferStreamSet == nullptr)
			{
				CCriticalSection* pcsGlobalThreadTemp = new CCriticalSection;			//lint !e1732 !e1733 (Info -- new in constructor for class 'CSimpleWorkerThread' which has no assignment operator)
				if (InterlockedCompareExchangePointer((void**)&pcsGlobalCBufferStreamSet, pcsGlobalThreadTemp, nullptr) != nullptr)
					delete pcsGlobalThreadTemp;
			}
			ASSERT(pcsGlobalCBufferStreamSet != nullptr);
			CSingleLock lockGlobalList(pcsGlobalCBufferStreamSet, true);
			if (pGlobalCBufferStreamSet == nullptr)
				pGlobalCBufferStreamSet = new std::set<CBufferStream*>;
			(void)pGlobalCBufferStreamSet->insert(this);
#endif
		}

	public:
		CBufferStream(CBuffer* pBuf = nullptr)
		{
#ifdef DEBUG_DUMP_QUEUES
			Register();
#endif
			_refcount = 0;
			_pBuf = pBuf;
			if (_pBuf == nullptr)
				_pBuf = &_InternalBuf;
			_liPosition.QuadPart = 0;
			bReadOnly = false;
		}

		CBufferStream(const CBuffer* pBuf)
		{
#ifdef DEBUG_DUMP_QUEUES
			Register();
#endif
			_refcount = 0;
			_pBuf = const_cast<CBuffer*> (pBuf);
			if (_pBuf == nullptr)
				_pBuf = &_InternalBuf;
			_liPosition.QuadPart = 0;
			bReadOnly = true;
		}

		virtual ~CBufferStream()
		{
			ASSERT(_refcount == 0);
			_pBuf = nullptr;
#ifdef DEBUG_DUMP_QUEUES
			if (pcsGlobalCBufferStreamSet && pGlobalCBufferStreamSet)
			{
				CSingleLock lockGlobalList(pcsGlobalCBufferStreamSet, true);
				(void)pGlobalCBufferStreamSet->erase(this);
			}
#endif
		}

	public:
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject)
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
			ULONG res = (ULONG)InterlockedDecrement(&_refcount);
			if (res == 0)
				delete this;
			return res;
		}

		// ISequentialStream Interface

	public:
		virtual HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead)
		{
			ASSERT(_pBuf != nullptr);
			ULONG uBytesRead = 0;
			if (pcbRead == nullptr)
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
			ASSERT(_pBuf != nullptr);
			if (((DWORD)_liPosition.QuadPart + cb) > _pBuf->GetBufSize())
			{
				_pBuf->SetBufSize((DWORD)_liPosition.QuadPart + cb + 8196);
				_pBuf->SetBufSize((DWORD)_liPosition.QuadPart + cb);
			}
			memcpy(_pBuf->GetData() + (DWORD)_liPosition.QuadPart, pv, cb);
			_liPosition.LowPart += cb;
			_liPosition.HighPart = 0;
			if (pcbWritten != nullptr)
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

		virtual HRESULT STDMETHODCALLTYPE Clone(IStream**)
		{
			return E_NOTIMPL;
		}

		virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER liDistanceToMove, DWORD dwOrigin,
			ULARGE_INTEGER* lpNewFilePointer)
		{
			switch (dwOrigin)
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
			if (lpNewFilePointer != nullptr)
				*lpNewFilePointer = _liPosition;

			return S_OK;
		}

		virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG* pStatstg, DWORD grfStatFlag)
		{
			(void)grfStatFlag;
			ASSERT(_pBuf != nullptr);
			pStatstg->cbSize.QuadPart = _pBuf->GetBufSize();
			return S_OK;
		}

		CString GetXml()
		{
			return FROM_ANSI((LPCSTR)_pBuf->GetData());
		}

		CString Format()
		{
			CString sMsg;
			sMsg.Format(_T("CBufferStream: Position: %I64u, Ref: %d, Readonly: %d"), _liPosition.QuadPart, _refcount, (int)bReadOnly);
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
			for (std::set<CBufferStream*>::iterator itSet = pGlobalCBufferStreamSet->begin(); itSet != pGlobalCBufferStreamSet->end(); ++itSet)
			{
				sHandleMsg += (*itSet)->Format();
			}
#else
			sHandleMsg.Empty();
#endif
		}

	private:
#ifdef DEBUG_DUMP_QUEUES
		static CCriticalSection* pcsGlobalCBufferStreamSet;
		static std::set<CBufferStream*>* pGlobalCBufferStreamSet;
#endif
		CBuffer* _pBuf, _InternalBuf;
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
			_hFile = nullptr;
		}

	public:
		static HRESULT OpenFile(LPCTSTR pName, IStream** ppStream, bool fWrite)
		{
			HANDLE hFile = ::CreateFile(pName, fWrite ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ,
				nullptr, fWrite ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

			if (hFile == INVALID_HANDLE_VALUE)
				return HRESULT_FROM_WIN32(GetLastError());

			*ppStream = new FileStream(hFile);

			if (*ppStream == nullptr)
				CloseHandle(hFile);

			return S_OK;
		}

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject)
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
			BOOL rc = ReadFile(_hFile, pv, cb, pcbRead, nullptr);
			return (rc) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
		}

		virtual HRESULT STDMETHODCALLTYPE Write(void const* pv, ULONG cb, ULONG* pcbWritten)
		{
			BOOL rc = WriteFile(_hFile, pv, cb, pcbWritten, nullptr);
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

		virtual HRESULT STDMETHODCALLTYPE Clone(IStream**)
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
			(void)grfStatFlag;
			if (GetFileSizeEx(_hFile, (PLARGE_INTEGER)&pStatstg->cbSize) == 0)
				return HRESULT_FROM_WIN32(GetLastError());
			return S_OK;
		}

	private:
		HANDLE _hFile;
		LONG _refcount;
	};

	HRESULT ECSUTIL_EXT_API ScanXml(
		const CBuffer* pXml,
		void* pContext,
		XMLLITE_READER_CB ReaderCB);

	HRESULT ECSUTIL_EXT_API ScanXmlStream(
		IStream* pStream,
		void* pContext,
		XMLLITE_READER_CB ReaderCB);

	HRESULT ProcessXmlTextField(
		const std::map<CString, XML_FIELD_ENTRY>& FieldMap,
		const CStringW& sPathRoot,							// the XML path without the last field name, such as //bucket_info/
		const CStringW& sXmlPath,
		void* pContext,
		const CStringW* psValue);

} // end namespace ecs_sdk
