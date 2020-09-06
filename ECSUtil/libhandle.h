/*
 * Copyright (c) 2017 - 2020, Dell Technologies, Inc. All Rights Reserved.
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

#include <afxwin.h>

namespace ecs_sdk
{



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
			hModule = nullptr;
			bAfx = bAfxFlag;
		}

		~CLibraryHandle()
		{
			Close();
			hModule = nullptr;
		}

		operator HMODULE() const
		{
			return(hModule);
		}

		CLibraryHandle& operator =(HMODULE hParam)
		{
			hModule = hParam;
			return *this;
		}

		bool IfOpen() const
		{
			return hModule != nullptr;
		}

		void Close()
		{
			if (hModule != nullptr)
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
			hModule = nullptr;
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
				hModule = ::LoadLibraryEx(lpLibFileName, nullptr, DONT_RESOLVE_DLL_REFERENCES);
		}
	};

} // end namespace ecs_sdk
