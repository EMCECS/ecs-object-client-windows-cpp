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
#include "exportdef.h"

namespace ecs_sdk
{


	enum class E_URI_ENCODE : BYTE
	{
		StdSAFE,				// used for paths/object keys
		AllSAFE,				// encode almost everything except a-z, 0-9
		V4Auth,					// encode according to v4auth rules
		V4AuthSlash				// encode according to v4auth rules plus encode slash "/"
	};

	ECSUTIL_EXT_API extern CString UriEncode(LPCTSTR pszInput, E_URI_ENCODE Type = E_URI_ENCODE::StdSAFE);
	ECSUTIL_EXT_API extern CString UriDecode(LPCTSTR pszInput);
	ECSUTIL_EXT_API extern CString EncodeSpecialChars(const CString& sSource);

} // end namespace ecs_sdk
