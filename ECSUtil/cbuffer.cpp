//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//



#include "stdafx.h"


#include "cbuffer.h"
#include "widestring.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


HANDLE CBuffer::hBufferHeap = NULL;

//
// CreateBuffer
// allocate and initialize a buffer
//
BYTE *CBuffer::CreateBuffer(
	DWORD nNewSize)			// new size
{
#ifdef SEPARATE_HEAPS
	if (hBufferHeap == NULL)
		hBufferHeap = HeapCreate(0, 0, 0);
#endif
	if (hBufferHeap == NULL)
		hBufferHeap = GetProcessHeap();
	BYTE *pNewData = (BYTE *)HeapAlloc(hBufferHeap, HEAP_ZERO_MEMORY, nNewSize + sizeof(CBufferData));
	if (pNewData == nullptr)
		AfxThrowMemoryException();
	CBufferData *pNewInfo = (CBufferData *)pNewData;
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
	CBufferData *pInfo = GetInternalData();
	// if there is another reference to the buffer, allocate its own and copy the data into it
	if (pInfo != NULL)
	{
		if (pInfo->m_nRefs > 1)
		{
			BYTE *pNewData;
			if (!bEmpty)
			{
				pNewData = CreateBuffer(nNewSize);
				memcpy(pNewData, m_pData, __min(pInfo->m_nSize, nNewSize));
			}
			else
				pNewData = NULL;
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
		if (m_pData != NULL)
		{
			VERIFY(HeapFree(hBufferHeap, 0, pInfo));
			m_pData = NULL;
		}
		return;
	}

	ASSERT((pInfo == NULL) || (!bForce || (nNewSize >= pInfo->m_nSize)));
	if (m_pData == NULL)
	{
		if (nNewSize == 0)
			return;
		m_pData = CreateBuffer(nNewSize);
		return;
	}
	ASSERT(pInfo != NULL);
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
		if (HeapValidate(hBufferHeap, 0, NULL) == 0)
			DebugBreak();
#endif
		SIZE_T AllocLen = nNewSize + sizeof(CBufferData) + ALLOC_INCR_DEFAULT;
		pInfo = (CBufferData *)HeapReAlloc(hBufferHeap, HEAP_ZERO_MEMORY, pInfo, AllocLen);
		if (pInfo == NULL)
			AfxThrowMemoryException();
		pInfo->m_nSize = nNewSize;
		pInfo->m_nAllocSize = nNewSize + ALLOC_INCR_DEFAULT;
		m_pData = (BYTE *)(pInfo + 1);
	}
}

// Lock
// make sure that the buffer is never shared with any other variable
// assignments will always create a new copy
// cannot lock an empty buffer
void CBuffer::Lock(void)
{
	if (m_pData != NULL)
	{
		Grow(GetInternalData()->m_nSize);		// make sure the reference count is 1
		ASSERT(GetInternalData()->m_nRefs == 1);
		GetInternalData()->m_nRefs = -1;		// lock it
	}
}

void CBuffer::Unlock(void)
{
	if (m_pData != NULL)
	{
		if (GetInternalData()->m_nRefs == -1)
			GetInternalData()->m_nRefs = 1;
	}
}

// Attributes
DWORD CBuffer::GetAllocSize() const
{
	CBufferData *pInfo = GetInternalData();
	if (pInfo == NULL)
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
	CBufferData *pInfo = GetInternalData();
	DWORD nSize = pInfo == NULL ? 0 : pInfo->m_nSize;
	Grow(nIndex >= nSize ? nIndex + 1 : nSize);
	ASSERT(m_pData != NULL);
	m_pData[nIndex] = newElement;
}

//
// assignment operator
// copy the contents of one buffer to another
//
CBuffer &CBuffer::operator =(
	const CBuffer &b)            // duplicate a CBuffer object
{
	if (&b == this)
		return(*this);
	if ((b.m_pData == NULL) || b.IsEmpty())
	{
		Empty();
		return(*this);
	}
	CBufferData *pSrcInfo = b.GetInternalData();
	CBufferData *pInfo = GetInternalData();
	if ((pSrcInfo->m_nRefs == -1) || ((pInfo != NULL) && (pInfo->m_nRefs == -1)))	// check if locked
	{
		Grow(pSrcInfo->m_nSize);		// copy the data
		memcpy(m_pData, b.m_pData, pSrcInfo->m_nSize);
		return *this;
	}
	if (m_pData != b.m_pData)
	{
		Empty();							// deallocate anything that might be allocated
		(void)InterlockedIncrement(&pSrcInfo->m_nRefs);
		m_pData = b.m_pData;				//lint !e1555: (Warning -- Direct pointer copy of member 'CBuffer::m_pData' within copy assignment operator: 'CBuffer::operator=(const CBuffer &)')
	}
	return *this;
};

//
// append operator
// append the contents of a buffer to the current buffer
//
CBuffer &CBuffer::operator +=(
	const CBuffer &b)            // duplicate a CBuffer object
{
	if (!b.IsEmpty())
	{
		DWORD OldSize = GetBufSize();
		Grow(GetBufSize() + b.GetBufSize());
		ASSERT(m_pData != NULL);
		memcpy(&m_pData[OldSize], b.m_pData, b.GetBufSize());
	}
	return(*this);
};

//
// copy constructor
// copy the contents of one buffer to a new object
//
CBuffer::CBuffer(
	const CBuffer &b)            // duplicate a CBuffer object
{
	m_pData = NULL;
	if ((b.m_pData == NULL) || b.IsEmpty())
		return;
	CBufferData *pSrcInfo = b.GetInternalData();
	if (pSrcInfo->m_nRefs == -1)	// check if locked
	{
		Grow(pSrcInfo->m_nSize);		// copy the data
		memcpy(m_pData, b.m_pData, pSrcInfo->m_nSize);
		return;
	}
	(void)InterlockedIncrement(&pSrcInfo->m_nRefs);
	m_pData = b.m_pData;					//lint !e1554: (Warning -- Direct pointer copy of member 'CBuffer::m_pData' within copy constructor: 'CBuffer::CBuffer(const CBuffer &)')
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
	const void *pSrc,				// pointer to data to copy into the buffer
	DWORD nLen)					// number of bytes to copy
{
	Grow(nLen);
	if (nLen > 0)
	{
		ASSERT(m_pData != NULL);
		memcpy(m_pData, pSrc, nLen);
	}
}

//
// Append
// copy the specified buffer to the end of the buffer, allocating as necessary
//
void CBuffer::Append(
	const void *pSrc,				// pointer to data to copy into the buffer
	DWORD nLen)					// number of bytes to copy
{
	DWORD nOrigSize = GetBufSize();
	Grow(nLen + nOrigSize);
	if (GetBufSize() > 0)
	{
		ASSERT(m_pData != NULL);
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
	register DWORD nSize = GetBufSize();
	int iDiff = nSize - Buf.GetBufSize();
	if ((iDiff == 0) && (nSize != 0))
	{
		ASSERT(m_pData != NULL);
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
	VERIFY(Base64Decode((char *)AnsiData.GetData(), AnsiData.GetBufSize(), GetData(), &iDataLen));
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
	UnicodeHash.Set((char *)OutBuf.GetData(), OutBuf.GetBufSize());
	return (LPCTSTR)UnicodeHash;
#else
	return CString((char *)OutBuf.GetData());
#endif
}
