/*
 * Copyright (c) 2017 - 2022, Dell Technologies, Inc. All Rights Reserved.
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


	struct ERROR_MESSAGE_INFO_ENTRY
	{
		CString sDLLPath;
		CString sMsg;
	};

	ECSUTIL_EXT_API extern CString GetNTErrorText(DWORD ec);
	ECSUTIL_EXT_API extern CString GetNTLastErrorText(void);

	ECSUTIL_EXT_API extern CString GetUnixErrorText(errno_t err);

} // end namespace ecs_sdk
