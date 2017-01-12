//
// Copyright (C) 1994 - 2011 EMC Corporation
// All rights reserved.
//

#pragma once

#include <afxwin.h>



// CLibraryHandle
// holder for library instance
// it will automatically be closed by the destructor
class CLibraryHandle
{
private:
	HMODULE hModule;
	bool bAfx;

public:
	CLibraryHandle(bool bAfxFlag = false)
	{
		hModule = NULL;
		bAfx = bAfxFlag;
	}

	~CLibraryHandle()
	{
		Close();
		hModule = NULL;
	}

	operator HMODULE() const
	{
		return(hModule);
	}

	CLibraryHandle &operator =(HMODULE hParam)
	{
		hModule = hParam;
		return *this;
	}

	bool IfOpen() const
	{
		return hModule != NULL;
	}

	void Close()
	{
		if (hModule != NULL)
		{
			if (!bAfx)
			{
				VERIFY(FreeLibrary(hModule));
			}
			else
			{
#ifdef _AFXDLL
				VERIFY(AfxFreeLibrary(hModule));
#else
				VERIFY(FreeLibrary(hModule));
#endif
			}
		}
		hModule = NULL;
	}

	void SetAfx(bool bAfxFlag = true)
	{
		bAfx = bAfxFlag;
	}

	void LoadLibrary(LPCTSTR lpLibFileName, bool bDataOnlyDll = false)
	{
		if (!bDataOnlyDll)
		{
#ifndef _AFXDLL
			hModule = ::LoadLibrary(lpLibFileName);
#else
			SetAfx();
			hModule = AfxLoadLibrary(lpLibFileName);
#endif
		}
		else
			hModule = ::LoadLibraryEx(lpLibFileName, NULL, DONT_RESOLVE_DLL_REFERENCES);
	}
};

