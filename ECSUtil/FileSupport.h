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

#include "stdafx.h"
#include "exportdef.h"
#include "ECSConnection.h"

namespace ecs_sdk
{


	extern ECSUTIL_EXT_API CECSConnection::S3_ERROR S3Read(
		CECSConnection& Conn,							// established connection to ECS
		LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
		IStream* pStream,								// open stream to file
		ULONGLONG lwLen,								// if lwOffset == 0 and dwLen == 0, read entire file
		ULONGLONG lwOffset,								// if dwLen != 0, read 'dwLen' bytes starting from lwOffset
														// if lwOffset != 0 and dwLen == 0, read from lwOffset to the end of the file
		list<CECSConnection::HEADER_REQ>* pRcvHeaders,			// optional return all headers
		CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
		void* pContext,											// context for UpdateProgressCB
		ULONGLONG* pullReturnedLength);					// optional output returned size

	extern ECSUTIL_EXT_API CECSConnection::S3_ERROR S3Write(
		CECSConnection& Conn,							// established connection to ECS
		LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
		IStream* pStream,								// open stream to file
		const DWORD dwBufSize,							// size of buffer to use
		bool bChecksum,									// if set, include content-MD5 header
		DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
		const list<CECSConnection::HEADER_STRUCT>* pMDList,	// optional metadata to send to object
		CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
		void* pContext);											// context for UpdateProgressCB

	extern ECSUTIL_EXT_API bool DoS3MultiPartUpload(
		CECSConnection& Conn,							// established connection to ECS
		LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
		IStream* pStream,								// open handle to file
		const DWORD dwBufSize,							// size of buffer to use
		const DWORD dwPartSize,							// part size (in MB)
		const DWORD dwMaxThreads,						// maxiumum number of threads to spawn
		bool bChecksum,									// if set, include content-MD5 header
		const list<CECSConnection::HEADER_STRUCT>* pMDList,	// optional metadata to send to object
		DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
		DWORD dwMaxRetries,									// how many times to retry a part before giving up
		CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
		void* pContext,											// context for UpdateProgressCB
		CECSConnection::S3_ERROR& Error);						// returned error

	extern ECSUTIL_EXT_API CECSConnection::S3_ERROR S3Read(
		LPCWSTR pszFile,								// path to file
		DWORD grfMode,									// examples: for read: STGM_READ | STGM_SHARE_DENY_WRITE, for write: STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_WRITE
		DWORD dwAttributes,								// attribute of file if created
		bool bCreate,									// see SHCreateStreamOnFileEx on how to use
		CECSConnection& Conn,							// established connection to ECS
		LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
		ULONGLONG lwLen,								// if lwOffset == 0 and dwLen == 0, read entire file
		ULONGLONG lwOffset,								// if dwLen != 0, read 'dwLen' bytes starting from lwOffset
														// if lwOffset != 0 and dwLen == 0, read from lwOffset to the end of the file
		list<CECSConnection::HEADER_REQ>* pRcvHeaders,			// optional return all headers
		CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
		void* pContext,											// context for UpdateProgressCB
		ULONGLONG* pullReturnedLength);					// optional output returned size

	extern ECSUTIL_EXT_API CECSConnection::S3_ERROR S3Write(
		LPCWSTR pszFile,								// path to file
		DWORD grfMode,									// examples: for read: STGM_READ | STGM_SHARE_DENY_WRITE, for write: STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_WRITE
		DWORD dwAttributes,								// attribute of file if created
		CECSConnection& Conn,							// established connection to ECS
		LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
		const DWORD dwBufSize,							// size of buffer to use
		bool bChecksum,									// if set, include content-MD5 header
		DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
		const list<CECSConnection::HEADER_STRUCT>* pMDList,	// optional metadata to send to object
		CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
		void* pContext);										// context for UpdateProgressCB

	extern ECSUTIL_EXT_API bool DoS3MultiPartUpload(
		LPCWSTR pszFile,								// path to file
		DWORD grfMode,									// examples: for read: STGM_READ | STGM_SHARE_DENY_WRITE, for write: STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_WRITE
		DWORD dwAttributes,								// attribute of file if created
		CECSConnection& Conn,							// established connection to ECS
		LPCTSTR pszECSPath,								// path to object in format: /bucket/dir1/dir2/object
		const DWORD dwBufSize,							// size of buffer to use
		const DWORD dwPartSize,							// part size (in MB)
		const DWORD dwMaxThreads,						// maxiumum number of threads to spawn
		bool bChecksum,									// if set, include content-MD5 header
		const list<CECSConnection::HEADER_STRUCT>* pMDList,	// optional metadata to send to object
		DWORD dwMaxQueueSize,								// how big the queue can grow that feeds the upload thread
		DWORD dwMaxRetries,									// how many times to retry a part before giving up
		CECSConnection::UPDATE_PROGRESS_CB UpdateProgressCB,	// optional progress callback
		void* pContext,											// context for UpdateProgressCB
		CECSConnection::S3_ERROR& Error);						// returned error

}
