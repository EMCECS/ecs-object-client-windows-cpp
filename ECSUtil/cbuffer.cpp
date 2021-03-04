/*
 * Copyright (c) 2017 - 2021, Dell Technologies, Inc. All Rights Reserved.
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


#include "cbuffer.h"
#include "widestring.h"

namespace ecs_sdk
{

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
	static char THIS_FILE[] = __FILE__;
#endif


	HANDLE CBuffer::hBufferHeap = nullptr;

	//
	// CreateBuffer
	// allocate and initialize a buffer
	//
	BYTE* CBuffer::CreateBuffer(
		DWORD nNewSize)			// new size
	{
#ifdef SEPARATE_HEAPS
		if (hBufferHeap == nullptr)
			hBufferHeap = HeapCreate(0, 0, 0);
#endif
		if (hBufferHeap == nullptr)
			hBufferHeap = GetProcessHeap();
		BYTE* pNewData = (BYTE*)HeapAlloc(hBufferHeap, HEAP_ZERO_MEMORY, nNewSize + sizeof(CBufferData));
		if (pNewData == nullptr)
			AfxThrowMemoryException();
		CBufferData* pNewInfo = (CBufferData*)pNewData;
		pNewData += sizeof(CBufferData);
		pNewInfo->m_nSize = pNewInfo->m_nAllocSize = nNewSize;
		pNewInfo->m_nRefs = 1;
		return pNewData;
	}

	//
	// Grow
	// make sure the allocated array has AT LEAST nNewSize bytes
	// if bForce is true, then it will allocate EXACTLY the specified amount
	// if bForce is true, nNewSize must not be less than m_nSize
	//
	void CBuffer::Grow(
		DWORD nNewSize,				// new size
		bool bForce,		// allocate EXACTLY the specified amount
		bool bEmpty)		// if true, deallocate everything
	{
		// verify that the new size is valid
		// if adding the header causes overflow, FORGET IT
		// this could be a security breach - forcing a buffer overrun by allocating a much smaller buffer than we think
		// for instance, if nNewSize = 0xfffffff0, adding sizeof(CBufferData) makes the actual allocation 0
		if ((nNewSize + sizeof(CBufferData)) <= nNewSize)
			AfxThrowInvalidArgException();
		CBufferData* pInfo = GetInternalData();
		// if there is another reference to the buffer, allocate its own and copy the data into it
		if (pInfo != nullptr)
		{
			if (pInfo->m_nRefs > 1)
			{
				BYTE* pNewData;
				if (!bEmpty)
				{
					pNewData = CreateBuffer(nNewSize);
					memcpy(pNewData, m_pData, __min(pInfo->m_nSize, nNewSize));
				}
				else
					pNewData = nullptr;
				if (InterlockedDecrement(&pInfo->m_nRefs) <= 0)
				{
					// count has gone to zero, deallocate the buffer
					VERIFY(HeapFree(hBufferHeap, 0, pInfo));
				}
				m_pData = pNewData;
				return;
			}
		}
		// if it gets here, the reference count must be 0
		if (bEmpty)
		{
			if (m_pData != nullptr)
			{
				VERIFY(HeapFree(hBufferHeap, 0, pInfo));
				m_pData = nullptr;
			}
			return;
		}

		ASSERT((pInfo == nullptr) || (!bForce || (nNewSize >= pInfo->m_nSize)));
		if (m_pData == nullptr)
		{
			if (nNewSize == 0)
				return;
			m_pData = CreateBuffer(nNewSize);
			return;
		}
		ASSERT(pInfo != nullptr);
		if (pInfo == nullptr)
			AfxThrowMemoryException();
		// if the buffer is too big, don't bother to reallocate
		if (!bForce && (nNewSize <= pInfo->m_nAllocSize))
		{
			pInfo->m_nSize = nNewSize;
			return;
		}
		// the buffer must grow, allocate it and copy over any existing data
		if (nNewSize == 0)
			Empty();			// it can only get here if bForce is set
		else
		{
#if defined(DEBUG) && !defined(BETA_BUILD)
			if (HeapValidate(hBufferHeap, 0, nullptr) == 0)
				DebugBreak();
#endif
			SIZE_T AllocLen = nNewSize + sizeof(CBufferData) + ALLOC_INCR_DEFAULT;
			void* pTmp = HeapReAlloc(hBufferHeap, HEAP_ZERO_MEMORY, pInfo, AllocLen);
			if (pTmp == nullptr)
				AfxThrowMemoryException();
			pInfo = (CBufferData*)pTmp;
			pInfo->m_nSize = nNewSize;
			pInfo->m_nAllocSize = nNewSize + ALLOC_INCR_DEFAULT;
			m_pData = (BYTE*)(pInfo + 1);
		}
	}

	// Lock
	// make sure that the buffer is never shared with any other variable
	// assignments will always create a new copy
	// cannot lock an empty buffer
	void CBuffer::Lock(void)
	{
		if (m_pData != nullptr)
		{
			Grow(GetInternalData()->m_nSize);		// make sure the reference count is 1
			ASSERT(GetInternalData()->m_nRefs == 1);
			GetInternalData()->m_nRefs = -1;		// lock it
		}
	}

	void CBuffer::Unlock(void)
	{
		if (m_pData != nullptr)
		{
			if (GetInternalData()->m_nRefs == -1)
				GetInternalData()->m_nRefs = 1;
		}
	}

	// Attributes
	DWORD CBuffer::GetAllocSize() const
	{
		CBufferData* pInfo = GetInternalData();
		if (pInfo == nullptr)
			return 0;
		return(pInfo->m_nAllocSize);
	}

	void CBuffer::SetBufSize(DWORD nNewSize)
	{
		Grow(nNewSize);
	}

	// Operations
	// Clean up
	void CBuffer::Empty()
	{
		Grow(0, false, true);
	}

	void CBuffer::SetAt(DWORD nIndex, BYTE newElement)
	{
		CBufferData* pInfo = GetInternalData();
		DWORD nSize = pInfo == nullptr ? 0 : pInfo->m_nSize;
		Grow(nIndex >= nSize ? nIndex + 1 : nSize);
		ASSERT(m_pData != nullptr);
		if (m_pData == nullptr)
			AfxThrowMemoryException();
		m_pData[nIndex] = newElement;
	}

	//
	// assignment operator
	// copy the contents of one buffer to another
	//
	CBuffer& CBuffer::operator =(
		const CBuffer& b)            // duplicate a CBuffer object
	{
		if (&b == this)
			return(*this);
		if ((b.m_pData == nullptr) || b.IsEmpty())
		{
			Empty();
			return(*this);
		}
		CBufferData* pSrcInfo = b.GetInternalData();
		CBufferData* pInfo = GetInternalData();
		if ((pSrcInfo->m_nRefs == -1) || ((pInfo != nullptr) && (pInfo->m_nRefs == -1)))	// check if locked
		{
			Grow(pSrcInfo->m_nSize);		// copy the data
			memcpy(m_pData, b.m_pData, pSrcInfo->m_nSize);
			return *this;
		}
		if (m_pData != b.m_pData)
		{
			Empty();							// deallocate anything that might be allocated
			(void)InterlockedIncrement(&pSrcInfo->m_nRefs);
			m_pData = b.m_pData;
		}
		return *this;
	};

	//
	// append operator
	// append the contents of a buffer to the current buffer
	//
	CBuffer& CBuffer::operator +=(
		const CBuffer& b)            // duplicate a CBuffer object
	{
		if (!b.IsEmpty())
		{
			DWORD OldSize = GetBufSize();
			Grow(GetBufSize() + b.GetBufSize());
			ASSERT(m_pData != nullptr);
			if (m_pData == nullptr)
				AfxThrowMemoryException();
			memcpy(&m_pData[OldSize], b.m_pData, b.GetBufSize());
		}
		return(*this);
	};

	//
	// copy constructor
	// copy the contents of one buffer to a new object
	//
	CBuffer::CBuffer(
		const CBuffer& b)            // duplicate a CBuffer object
	{
		m_pData = nullptr;
		if ((b.m_pData == nullptr) || b.IsEmpty())
			return;
		CBufferData* pSrcInfo = b.GetInternalData();
		if (pSrcInfo->m_nRefs == -1)	// check if locked
		{
			Grow(pSrcInfo->m_nSize);		// copy the data
			if (m_pData == nullptr)
				return;
			memcpy(m_pData, b.m_pData, pSrcInfo->m_nSize);
			return;
		}
		(void)InterlockedIncrement(&pSrcInfo->m_nRefs);
		m_pData = b.m_pData;
	}

	//
	// FreeExtra
	// free any memory allocated above the upper bound
	//
	void CBuffer::FreeExtra(void)
	{
		Grow(GetBufSize(), true);
	}

	//
	// Load
	// copy the specified buffer into the buffer, allocating as necessary
	//
	void CBuffer::Load(
		const void* pSrc,				// pointer to data to copy into the buffer
		DWORD nLen)					// number of bytes to copy
	{
		Grow(nLen);
		if (nLen > 0)
		{
			ASSERT(m_pData != nullptr);
			if (m_pData != nullptr)
				memcpy(m_pData, pSrc, nLen);
		}
	}

	//
	// Append
	// copy the specified buffer to the end of the buffer, allocating as necessary
	//
	void CBuffer::Append(
		const void* pSrc,				// pointer to data to copy into the buffer
		DWORD nLen)					// number of bytes to copy
	{
		DWORD nOrigSize = GetBufSize();
		Grow(nLen + nOrigSize);
		if (GetBufSize() > 0)
		{
			ASSERT(m_pData != nullptr);
			if (m_pData != nullptr)
				memcpy(m_pData + nOrigSize, pSrc, nLen);
		}
	}

	//
	// Compare
	// check two CBuffer's for equality
	// return 0 if equal
	// returns negative or positive value depending on whether one is "more" or "less"
	// than the other
	//
	int CBuffer::Compare(const CBuffer& Buf) const
	{
		DWORD nSize = GetBufSize();
		int iDiff = nSize - Buf.GetBufSize();
		if ((iDiff == 0) && (nSize != 0))
		{
			ASSERT(m_pData != nullptr);
			if (m_pData != nullptr)
				iDiff = memcmp(m_pData, Buf.m_pData, nSize);
		}
		return iDiff;
	}

	void CBuffer::LoadBase64(LPCTSTR pszBase64Input)
	{
		CAnsiString AnsiData;
		int iDataLen = Base64DecodeGetRequiredLength(lstrlen(pszBase64Input));
		SetBufSize(iDataLen);
#ifdef _UNICODE
		AnsiData.Set(pszBase64Input);
#else
		AnsiData.Load(pszBase64Input, lstrlen(pszBase64Input) + 1);
#endif
		VERIFY(Base64Decode((char*)AnsiData.GetData(), AnsiData.GetBufSize(), GetData(), &iDataLen));
		SetBufSize(iDataLen);
	}

	CString CBuffer::EncodeBase64() const
	{
		CBuffer OutBuf;
		OutBuf.SetBufSize(Base64EncodeGetRequiredLength(GetBufSize(), ATL_BASE64_FLAG_NOCRLF));
		int iOutputLen = OutBuf.GetBufSize();
		VERIFY(Base64Encode(GetData(), GetBufSize(), (LPSTR)OutBuf.GetData(), &iOutputLen, ATL_BASE64_FLAG_NOCRLF));
#ifdef _UNICODE
		CWideString UnicodeHash;
		UnicodeHash.Set((char*)OutBuf.GetData(), OutBuf.GetBufSize());
		return (LPCTSTR)UnicodeHash;
#else
		return CString((char*)OutBuf.GetData());
#endif
	}

} // end namespace ecs_sdk
