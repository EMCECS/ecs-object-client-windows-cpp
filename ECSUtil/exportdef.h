//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//
//

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
