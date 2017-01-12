// S3Util.h : main header file for the S3Util DLL
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols


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
