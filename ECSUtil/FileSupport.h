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

#include "stdafx.h"
#include "exportdef.h"
#include "ECSConnection.h"

extern ECSUTIL_EXT_API CECSConnection::S3_ERROR S3Read(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	const CHandle& hDataHandle,						// open handle to file
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext);										// context for UpdateProgressCB

extern ECSUTIL_EXT_API CECSConnection::S3_ERROR S3Write(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	const CHandle& hDataHandle,						// open handle to file
	const DWORD dwBufSize,							// size of buffer to use
	bool bChecksum,									// if set, include content-MD5 header
	DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext);											// context for UpdateProgressCB

extern ECSUTIL_EXT_API bool DoS3MultiPartUpload(
	CECSConnection& Conn,							// established connection to ECS
	LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
	const CHandle& hDataHandle,						// open handle to file
	const ULONGLONG ullTotalLen,					// size of the file
	const DWORD dwBufSize,							// size of buffer to use
	const DWORD dwPartSize,							// part size (in MB)
	const DWORD dwMaxThreads,						// maxiumum number of threads to spawn
	bool bChecksum,									// if set, include content-MD5 header
	list<CECSConnection::S3_METADATA_ENTRY> *pMDList,	// optional metadata to send to object
	DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
	DWORD dwMaxRetries,									// how many times to retry a part before giving up
	CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
	void *pContext,											// context for UpdateProgressCB
	CECSConnection::S3_ERROR& Error);						// returned error
