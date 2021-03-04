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

// S3Util.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"
#include "ECSUtil.h"

namespace ecs_sdk
{

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//
//TODO: If this DLL is dynamically linked against the MFC DLLs,
//		any functions exported from this DLL which call into
//		MFC must have the AFX_MANAGE_STATE macro added at the
//		very beginning of the function.
//
//		For example:
//
//		extern "C" BOOL PASCAL EXPORT ExportedFunction()
//		{
//			AFX_MANAGE_STATE(AfxGetStaticModuleState());
//			// normal function body here
//		}
//
//		It is very important that this macro appear in each
//		function, prior to any calls into MFC.  This means that
//		it must appear as the first statement within the 
//		function, even before any object variable declarations
//		as their constructors may generate calls into the MFC
//		DLL.
//
//		Please see MFC Technical Notes 33 and 58 for additional
//		details.
//

// CS3UtilApp

BEGIN_MESSAGE_MAP(CS3UtilApp, CWinApp)
END_MESSAGE_MAP()


// CS3UtilApp construction

CS3UtilApp::CS3UtilApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}


// The one and only CS3UtilApp object

CS3UtilApp theApp;


// CS3UtilApp initialization

BOOL CS3UtilApp::InitInstance()
{
	CWinApp::InitInstance();

	return TRUE;
}

} // end namespace ecs_sdk
