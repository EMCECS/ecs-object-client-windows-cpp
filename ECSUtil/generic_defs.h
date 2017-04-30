#include "exportdef.h"

#pragma once

#define NUL (_T('\0'))

#define COMPARE(rec1, rec2)  (((rec1) > (rec2)) ? 1 : ((rec1) < (rec2)) ? -1 : 0)

#define SECONDS(x) ((x) * 1000)
#define MINUTES(x) ((x) * SECONDS(60))
#define HOURS(x)   ((x) * MINUTES(60))
#define DAYS(x)    ((x) * HOURS(24))

#define GIGABYTES(x) ((x) * 0x40000000)
#define MEGABYTES(x) ((x) * 0x100000)
#define KILOBYTES(x) ((x) * 0x400)

#define FT_SECOND		((__int64)10000000)
#define FT_SECONDS(x)	((x) * FT_SECOND)
#define FT_MINUTES(x)	((x) * FT_SECONDS(60))
#define FT_HOURS(x)		((x) * FT_MINUTES(60))
#define FT_DAYS(x)		((x) * FT_HOURS(24))

#define SET_BIT(x,b) ((x) |=  (b))
#define CLR_BIT(x,b) ((x) &= ~(b))
#define TST_BIT(x,b) (((x) &  (b)) != 0)
#define TOG_BIT(x,b) ((x) ^=  (b))
#define SET_CLR_BIT(x,b,f) { if ((f)) SET_BIT(x,b); else CLR_BIT(x,b); }


inline bool IfFTZero(const FILETIME& ftTime)
{
	return (ftTime.dwLowDateTime == 0) && (ftTime.dwHighDateTime == 0);
}

inline void ZeroFT(FILETIME& ftTime)
{
	ftTime.dwLowDateTime = 0;
	ftTime.dwHighDateTime = 0;
}

inline LARGE_INTEGER FTtoLarge(const FILETIME& ftTime)
{
	LARGE_INTEGER ulFileTime;
	ulFileTime.LowPart = ftTime.dwLowDateTime;
	ulFileTime.HighPart = ftTime.dwHighDateTime;
	return ulFileTime;
}

inline FILETIME LargetoFT(const LARGE_INTEGER& ulFileTime)
{
	FILETIME ftTime;
	ftTime.dwLowDateTime = ulFileTime.LowPart;
	ftTime.dwHighDateTime = ulFileTime.HighPart;
	return ftTime;
}

inline ULARGE_INTEGER FTtoULarge(const FILETIME& ftTime)
{
	ULARGE_INTEGER ulFileTime;
	ulFileTime.LowPart = ftTime.dwLowDateTime;
	ulFileTime.HighPart = ftTime.dwHighDateTime;
	return ulFileTime;
}

inline FILETIME ULargetoFT(const ULARGE_INTEGER& ulFileTime)
{
	FILETIME ftTime;
	ftTime.dwLowDateTime = ulFileTime.LowPart;
	ftTime.dwHighDateTime = ulFileTime.HighPart;
	return ftTime;
}

// add an int to a FILETIME
inline FILETIME operator +(const FILETIME& ftTime, const LONGLONG& llAddend)
{
	LARGE_INTEGER liTime = FTtoLarge(ftTime);
	liTime.QuadPart += llAddend;
	return LargetoFT(liTime);
}

// subtract an int to a FILETIME
inline FILETIME operator -(const FILETIME& ftTime, const LONGLONG& llSubtrahend)
{
	LARGE_INTEGER liTime = FTtoLarge(ftTime);
	liTime.QuadPart -= llSubtrahend;
	return LargetoFT(liTime);
}

// compare two FILETIME's
inline bool operator ==(const FILETIME& ftTime1, const FILETIME& ftTime2)
{
	return CompareFileTime(&ftTime1, &ftTime2) == 0;
}

// compare two FILETIME's
inline bool operator !=(const FILETIME& ftTime1, const FILETIME& ftTime2)
{
	return CompareFileTime(&ftTime1, &ftTime2) != 0;
}

// compare two FILETIME's
inline bool operator >(const FILETIME& ftTime1, const FILETIME& ftTime2)
{
	return CompareFileTime(&ftTime1, &ftTime2) > 0;
}

// compare two FILETIME's
inline bool operator <(const FILETIME& ftTime1, const FILETIME& ftTime2)
{
	return CompareFileTime(&ftTime1, &ftTime2) < 0;
}

// compare two FILETIME's
inline bool operator >=(const FILETIME& ftTime1, const FILETIME& ftTime2)
{
	return CompareFileTime(&ftTime1, &ftTime2) >= 0;
}

// compare two FILETIME's
inline bool operator <=(const FILETIME& ftTime1, const FILETIME& ftTime2)
{
	return CompareFileTime(&ftTime1, &ftTime2) <= 0;
}

// exception error structure
class CErrorInfo
{
public:
	DWORD dwLine;
	DWORD dwError;
	CString sFile;
	CString sAdditionalInfo;

public:
	CErrorInfo()
	{
		dwLine = 0;
		dwError = ERROR_SUCCESS;
	}

	CErrorInfo(LPCTSTR pszFile, DWORD dwLineParam, DWORD dwErrorParam = ERROR_SUCCESS, LPCTSTR pszAdditionalInfo = NULL)
	{
		sFile = pszFile;
		dwLine = dwLineParam;
		dwError = dwErrorParam;
		sAdditionalInfo = pszAdditionalInfo;
	}

	CErrorInfo(LPCTSTR pszFile, DWORD dwLineParam, NTSTATUS dwErrorParam = ERROR_SUCCESS, LPCTSTR pszAdditionalInfo = NULL)
	{
		sFile = pszFile;
		dwLine = dwLineParam;
		dwError = (DWORD)dwErrorParam;
		sAdditionalInfo = pszAdditionalInfo;
	}
};

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0)

class CStringSortNoCase
{
private:
	CString sStr;
public:
	CStringSortNoCase(LPCTSTR pszInit = NULL)
		: sStr(pszInit)
	{}
	CStringSortNoCase(const CString& sInit)
		: sStr(sInit)
	{}
	CStringSortNoCase& operator =(const CStringSortNoCase& Src)
	{
		if (&Src == this)
			return *this;
		sStr = Src.sStr;
		return *this;
	}
	bool operator < (const CStringSortNoCase& sOperand) const
	{
		return sStr.CompareNoCase(sOperand.sStr) < 0;
	}
	const CString& GetStr(void) const
	{
		return sStr;
	}
	operator CString() const
	{
		return sStr;
	}
};

// these routines allow arrays of strings to be loaded and stored
// string arrays have a single NUL between each item and two NUL's at the end
// LIST must be a STL container class of CString (list<CString>, deque, vector, etc.)
template<class LIST>
inline void LoadNullTermStringArray(LPCTSTR pNullStringArray, LIST& List)
{
	List.clear();
	while (*pNullStringArray != NUL)
	{
		List.push_back(CString(pNullStringArray));
		pNullStringArray += lstrlen(pNullStringArray) + 1;
	}
}

// CBoolSet - make sure the specified bool is cleared when variable goes out of scope
class CBoolSet
{
private:
	bool *pbFlag;

public:
	// if boolptr is supplied, automatically set the referenced flag
	CBoolSet(bool *pBoolPtr = NULL)
	{
		pbFlag = NULL;
		Set(true, pBoolPtr);
	}

	~CBoolSet()
	{
		if (pbFlag != NULL)
			*pbFlag = false;
		pbFlag = NULL;
	}

	void Set(bool bState, bool *pBoolPtr = NULL)
	{
		if (pBoolPtr != NULL)
		{
			if (pbFlag != NULL)
				*pbFlag = false;
			pbFlag = pBoolPtr;
		}
		if (pbFlag != NULL)
			*pbFlag = bState;
	}

	void Reset()
	{
		if (pbFlag != NULL)
			*pbFlag = false;
		pbFlag = NULL;
	}
};


//
// GetUniqueInt64
// return a unique number (as long as it doesn't get called more than
// a gazillion times)
// it will never return 0
//
inline ULONGLONG GetUniqueInt64()
{
	static LONGLONG next_id = 0;
	LONGLONG new_id = InterlockedIncrement64(&next_id);
	while (new_id == 0)
		new_id = InterlockedIncrement64(&next_id);
	return new_id;
}
