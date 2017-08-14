/*
 * Copyright (c) 1994 - 2017, EMC Corporation.
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * + Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * + The name of EMC Corporation may not be used to endorse or promote
 *   products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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

