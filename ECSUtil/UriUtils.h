/*
 * Copyright (c) 1994 - 2017, EMC Corporation. All Rights Reserved.
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
#include "exportdef.h"


ECSUTIL_EXT_API extern CString UriEncode(LPCTSTR pszInput, bool bEncodeAll = false);
ECSUTIL_EXT_API extern CString UriDecode(LPCTSTR pszInput);
ECSUTIL_EXT_API extern CString EncodeSpecialChars(const CString& sSource);
ECSUTIL_EXT_API extern CString UriEncodeS3(const CString& sSource, bool bEncodeSlash);
