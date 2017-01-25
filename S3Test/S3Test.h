#pragma once

#include "resource.h"
#include "stdafx.h"
#include <afxsock.h>
#include <list>
#include <deque>
#include "S3Test.h"
#include "ECSUtil.h"
#include "ECSConnection.h"


extern CECSConnection::S3_ERROR S3Read(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszFilePath,							// path to write file
	LPCTSTR pszECSPath);							// path to object in format: /bucket/dir1/dir2/object
