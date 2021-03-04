/*
 * Copyright (c) 2017 - 2021, Dell Technologies, Inc. All Rights Reserved.
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

// S3Util.h : main header file for the S3Util DLL
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols

namespace ecs_sdk
{


	// CS3UtilApp
	// See S3Util.cpp for the implementation of this class
	//

	class CS3UtilApp : public CWinApp
	{
	public:
		CS3UtilApp();

		// Overrides
	public:
		virtual BOOL InitInstance();

		DECLARE_MESSAGE_MAP()
	};

} // end namespace ecs_sdk
