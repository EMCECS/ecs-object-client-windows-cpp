/*
 * Copyright (c) 1994 - 2017, EMC Corporation.
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * + Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * + The name of EMC Corporation may not be used to endorse or promote
 *   products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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
	LPCTSTR pszFilePath,							// path to write file
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
