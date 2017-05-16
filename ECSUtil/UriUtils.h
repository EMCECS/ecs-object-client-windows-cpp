//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//

#pragma once
#include "exportdef.h"


ECSUTIL_EXT_API extern CString UriEncode(LPCTSTR pszInput, bool bEncodeAll = false);
ECSUTIL_EXT_API extern CString UriDecode(LPCTSTR pszInput);
ECSUTIL_EXT_API extern CString EncodeSpecialChars(const CString& sSource);
ECSUTIL_EXT_API extern CString UriEncodeS3(const CString& sSource, bool bEncodeSlash);
