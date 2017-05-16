//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//
//
/****************************************************************************
*
*
*    PROGRAM: cbuffer.h
*
*    PURPOSE: definition for CBuffer class
*
*
****************************************************************************/


#pragma once

#include "exportdef.h"


struct CBufferData
{
	DWORD m_nSize;			// # of elements (upperBound - 1)
	DWORD m_nAllocSize;	// number of bytes allocated
	long m_nRefs;			// number of current references to this buffer
	DWORD m_dummy;			// for 64bit alignment

	CBufferData()			// this constructor is more for illustration, it will never have a chance to run
	{
		m_nSize = m_nAllocSize = 0;
		m_nRefs = 1;
		m_dummy = 0;
	}
};

class ECSUTIL_EXT_CLASS CBuffer
{
// Implementation
private:
	enum {ALLOC_INCR_DEFAULT = 100};

	BYTE* m_pData;			// the actual array of data
public:
	static HANDLE hBufferHeap;

private:
	//
	// GetInternalData
	// get the CBufferData struct associated with the buffer
	// returns NULL if none was allocated
	//
	CBufferData* GetInternalData() const
	{
		if (m_pData == NULL)
			return NULL;
		return ((CBufferData*)m_pData)-1;
	}

	//
	// CreateBuffer
	// allocate and initialize a buffer
	//
	BYTE *CreateBuffer(
		DWORD nNewSize);			// new size

	//
	// Grow
	// make sure the allocated array has AT LEAST nNewSize bytes
	// if bForce is true, then it will allocate EXACTLY the specified amount
	// if bForce is true, nNewSize must not be less than m_nSize
	//
	void Grow(
		DWORD nNewSize,				// new size
		bool bForce = false,		// allocate EXACTLY the specified amount
		bool bEmpty = false);		// if true, deallocate everything

public:

// Construction
	CBuffer()
	{
		m_pData = NULL;
	}
	CBuffer(
		const void *pSrc,			// pointer to data to copy into the buffer
		DWORD nLen)					// number of bytes to copy
	{
		m_pData = NULL;
		Load(pSrc, nLen);
	}

	virtual ~CBuffer()
	{
		Empty();
	}				//lint !e1740 // m_pData not directly freed

	// Lock
	// make sure that the buffer is never shared with any other variable
	// assignments will always create a new copy
	// cannot lock an empty buffer
	void Lock(void);
	void Unlock(void);
// Attributes
	DWORD GetBufSize() const
	{
		CBufferData *pInfo = GetInternalData();
		if (pInfo == NULL)
			return 0;
		return(pInfo->m_nSize);
	}
	DWORD GetAllocSize() const;

	bool IsEmpty() const
	{
		CBufferData *pInfo = GetInternalData();
		if (pInfo == NULL)
			return true;
		return pInfo->m_nSize == 0;
	}
	void SetBufSize(DWORD nNewSize);
	void Empty();

	// Accessing elements
	BYTE GetAt(DWORD nIndex) const
	{
		ASSERT(nIndex < GetBufSize());
		ASSERT(m_pData != NULL);
		return(m_pData[nIndex]);
	}
	void SetAt(DWORD nIndex, BYTE newElement);

	// Direct Access to the element data (may return NULL)
	const BYTE* GetData() const
	{
		return (const BYTE*)m_pData;
	}

	BYTE* GetData()
	{
		if (m_pData == NULL)
			return NULL;
		Grow(GetInternalData()->m_nSize);
		return m_pData;
	}

	// overloaded operator helpers
	BYTE operator[](int nIndex) const
	{
		return(GetAt(nIndex));
	}
	CBuffer &operator =(
		const CBuffer &b);            // duplicate a CBuffer object
	CBuffer &operator +=(
		const CBuffer &b);            // duplicate a CBuffer object
	CBuffer(
		const CBuffer &b);            // duplicate a CBuffer object
	void FreeExtra(void);
	void Load(
		const void *pSrc,				// pointer to data to copy into the buffer
		DWORD nLen);					// number of bytes to copy
	void Append(
		const void *pSrc,				// pointer to data to copy into the buffer
		DWORD nLen);					// number of bytes to copy
	int Compare(const CBuffer& Buf) const;

	bool inline operator==(const CBuffer& Buf) const
		{return Compare(Buf) == 0; }

	bool inline operator!=(const CBuffer& Buf) const
		{return Compare(Buf) != 0; }

	bool inline operator<(const CBuffer& Buf) const
		{return Compare(Buf) < 0; }

	bool inline operator>(const CBuffer& Buf) const
		{return Compare(Buf) > 0; }

	bool inline operator<=(const CBuffer& Buf) const
		{return Compare(Buf) <= 0; }

	bool inline operator>=(const CBuffer& Buf) const
		{return Compare(Buf) >= 0; }
	static void DumpBuffers(CString *pDumpMsg = NULL);
	void LoadBase64(LPCTSTR pszBase64Input);
	CString EncodeBase64(void) const;
};
