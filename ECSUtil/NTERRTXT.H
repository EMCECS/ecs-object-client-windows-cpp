//
// Copyright (C) 1994 - 2017 EMC Corporation
// All rights reserved.
//

#pragma once

#include "exportdef.h"

struct ERROR_MESSAGE_INFO_ENTRY
{
	CString sDLLPath;
	CString sMsg;
};

ECSUTIL_EXT_API extern CString GetNTErrorText(DWORD ec);
ECSUTIL_EXT_API extern CString GetNTLastErrorText(void);

ECSUTIL_EXT_API extern CString GetUnixErrorText(errno_t err);
