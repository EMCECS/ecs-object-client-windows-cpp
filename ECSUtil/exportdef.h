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

// ECSUTILDLL should be defined if implicitly linking to the ECSUtil.dll (undefine if statically linking)
// ECSUTILDLL_FORCE_IMPORT should be defined if the current project is an MFC extension DLL

#ifdef ECSUTILDLL

#ifndef ECSUTILDLL_FORCE_IMPORT

#define ECSUTIL_EXT_CLASS AFX_EXT_CLASS
#define ECSUTIL_EXT_API AFX_EXT_API

#else

#define ECSUTIL_EXT_CLASS __declspec( dllimport )
#define ECSUTIL_EXT_API __declspec( dllimport )

#endif

#else

#define ECSUTIL_EXT_CLASS
#define ECSUTIL_EXT_API

#endif
