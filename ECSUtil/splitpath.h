//////////////////////////////////////////////////////////////////////////////////////////////
// Copyright © 1994 - 2022, Dell Technologies, Inc. All Rights Reserved.
//
// This software contains the intellectual property of Dell Inc. or is licensed to Dell Inc.
// from third parties. Use of this software and the intellectual property contained therein
// is expressly limited to the terms and conditions of the License Agreement under which it
// is provided by or on behalf of Dell Inc. or its subsidiaries.
//////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "exportdef.h"
#include "stdafx.h"

namespace ecs_sdk
{
	ECSUTIL_EXT_API wchar_t MapCaseInvariant(_In_ wchar_t input, _In_ DWORD dwFlags);
	ECSUTIL_EXT_API CString MapCaseInvariant(_In_ LPCTSTR pszInput, _In_ DWORD dwFlags);
	ECSUTIL_EXT_API void MapCaseInvariant(_Inout_ CString& sInputOutput, _In_ DWORD dwFlags);

	inline wchar_t ToUpperInvariant(_In_ wchar_t input)				{ return MapCaseInvariant(input, LCMAP_UPPERCASE);	}
	inline CString ToUpperInvariant(_In_ LPCTSTR pszInput)		{ return MapCaseInvariant(pszInput, LCMAP_UPPERCASE); }
	inline void ToUpperInvariant(_Inout_ CString& sInputOutput)	{ MapCaseInvariant(sInputOutput, LCMAP_UPPERCASE);	}
	inline wchar_t ToLowerInvariant(_In_ wchar_t input)				{ return MapCaseInvariant(input, LCMAP_LOWERCASE);	}
	inline CString ToLowerInvariant(_In_ LPCTSTR pszInput)		{ return MapCaseInvariant(pszInput, LCMAP_LOWERCASE);	}
	inline void ToLowerInvariant(_Inout_ CString& sInputOutput)	{ return MapCaseInvariant(sInputOutput, LCMAP_LOWERCASE);	}


} // end namespace ecs_sdk
