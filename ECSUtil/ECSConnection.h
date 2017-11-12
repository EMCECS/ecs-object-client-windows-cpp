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
#include <Winhttp.h>
#include "generic_defs.h"
#include "S3Error.h"
#include "ThreadPool.h"
#include "SimpleWorkerThread.h"
#include "CRWLock.h"
#include "Logging.h"
#include "fmtnum.h"

using namespace std;

const UINT GDReadWriteChunkMax = 0x100000;			// maxiumum read/write request size
const UINT MaxRetryCount = 5;
const UINT MaxWriteRequest = 0;
const UINT MaxWriteRequestThrottle = 8192;
const UINT MaxS3DeleteObjects = 1000;				// maximum number of objects to delete in one command (S3)
const UINT MaxS3ListingSize = 1000;					// max number of objects to get in one list command (S3 - Max is 1000)
const UINT MaxStreamQueueSize = 30;					// maximum size of stream queue. if there is a mismatch between the queue feed and consumer, you don't want it to grow too big

class CInternetHandle
{
private:
	HINTERNET hInternet;

public:
	CInternetHandle(HINTERNET hInit = nullptr)
	{
		hInternet = hInit;
	}

	~CInternetHandle()
	{
		(void)CloseHandle();
		hInternet = nullptr;
	}

	CInternetHandle &operator =(HINTERNET hParam)
	{
		if (hInternet != nullptr)
			(void)CloseHandle();
		hInternet = hParam;
		return *this;
	}

	const CInternetHandle& operator =(const CInternetHandle& src)
	{
		if (&src == this)
			return(*this);
		hInternet = nullptr;				// for an assignment, don't bother assigning the handle, it really shouldn't be duplicated
		return *this;
	};

	CInternetHandle(const CInternetHandle &src)
	{
		(void)src;
		hInternet = nullptr;				// for an assignment, don't bother assigning the handle, it really shouldn't be duplicated
	}

	operator HINTERNET() const
	{
		return hInternet;					//lint !e1535 !e1536 !e1537 : member function 'CInternetHandle::operator void *' exposes lower access pointer member 'CInternetHandle::hInternet'
	}

	DWORD CloseHandle()
	{
		DWORD dwError = ERROR_SUCCESS;
		if (IfOpen())
		{
			if (!WinHttpCloseHandle(hInternet))
			{
				dwError = GetLastError();
				if (dwError != ERROR_INVALID_HANDLE)
					LogMessage(TEXT(__FILE__), __LINE__, _T("WinHttpCloseHandle error"), dwError);
			}
			hInternet = nullptr;
		}
		return dwError;
	}

	bool IfOpen() const
	{
		return hInternet != nullptr;
	}
};

class ECSUTIL_EXT_CLASS CECSConnection
{
public:
	// DT Query support
	struct DT_QUERY_RESPONSE
	{
		bool bStatus;
		ULONGLONG ullTotalDataSize;
		ULONGLONG ullShippedDataSize;
		UINT uShippedDataPercentage;

		DT_QUERY_RESPONSE()
			: bStatus(false)
			, ullTotalDataSize(0ULL)
			, ullShippedDataSize(0ULL)
			, uShippedDataPercentage(0)
		{}
	};

	// metadata search
	enum class E_MD_SEARCH_TYPE : BYTE
	{
		Unknown,
		Integer,
		Datetime,
		Decimal,
		String
	};
	static E_MD_SEARCH_TYPE TranslateSearchFieldType(LPCTSTR pszType);
	enum class E_MD_SEARCH_FIELD
	{
		Unknown,
		SYSMD,
		USERMD,
	};
	struct S3_METADATA_SEARCH_ENTRY
	{
		CString sName;
		E_MD_SEARCH_TYPE Type;
		S3_METADATA_SEARCH_ENTRY()
			: Type(E_MD_SEARCH_TYPE::Unknown)
		{}
		int Compare(const S3_METADATA_SEARCH_ENTRY& rec) const
		{
			return sName.Compare(rec.sName);
		}
		bool operator == (const S3_METADATA_SEARCH_ENTRY& rec) const
		{
			return Compare(rec) == 0;
		};
		bool operator != (const S3_METADATA_SEARCH_ENTRY& rec) const
		{
			return Compare(rec) != 0;
		};
		bool operator < (const S3_METADATA_SEARCH_ENTRY& rec) const
		{
			return Compare(rec) < 0;
		};
		bool operator <= (const S3_METADATA_SEARCH_ENTRY& rec) const
		{
			return Compare(rec) <= 0;
		};
		bool operator > (const S3_METADATA_SEARCH_ENTRY& rec) const
		{
			return Compare(rec) > 0;
		};
		bool operator >= (const S3_METADATA_SEARCH_ENTRY& rec) const
		{
			return Compare(rec) >= 0;
		};
	};

	struct S3_METADATA_SEARCH_FIELDS
	{
		list<S3_METADATA_SEARCH_ENTRY> KeyList;
		list<S3_METADATA_SEARCH_ENTRY> AttributeList;
	};

	struct S3_METADATA_SEARCH_FIELDS_BUCKET
	{
		bool bSearchEnabled;
		list<S3_METADATA_SEARCH_ENTRY> KeyList;
		S3_METADATA_SEARCH_FIELDS_BUCKET()
			: bSearchEnabled(false)
		{}
	};

	struct ECSUTIL_EXT_CLASS S3_METADATA_SEARCH_PARAMS
	{
		CString sBucket;
		CString sExpression;
		CString sAttributes;
		CString sSorted;
		bool bOlderVersions;
		S3_METADATA_SEARCH_PARAMS()
			: bOlderVersions(false)
		{}
		int Compare(const S3_METADATA_SEARCH_PARAMS& Rec) const;
		bool operator == (const S3_METADATA_SEARCH_PARAMS& Rec) const;
		bool operator != (const S3_METADATA_SEARCH_PARAMS& Rec) const;
		CString Format(void) const;
	};

	struct S3_METADATA_SEARCH_RESULT_MD_MAP
	{
		CString sKey;
		CString sValue;
	};

	struct S3_METADATA_SEARCH_RESULT_QUERY_MD
	{
		E_MD_SEARCH_FIELD FieldType;
		list<S3_METADATA_SEARCH_RESULT_MD_MAP> MDMapList;
		S3_METADATA_SEARCH_RESULT_QUERY_MD()
			: FieldType(E_MD_SEARCH_FIELD::Unknown)
		{}
	};

	struct S3_METADATA_SEARCH_RESULT_OBJECT_MATCH
	{
		CString sObjectName;
		CString sObjectId;
		CString sVersionId;
		list<S3_METADATA_SEARCH_RESULT_QUERY_MD> QueryMDList;
	};

	struct S3_METADATA_SEARCH_RESULT
	{
		CString sBucket;
		deque<S3_METADATA_SEARCH_RESULT_OBJECT_MATCH> ObjectMatchList;
	};

	struct ECSUTIL_EXT_CLASS S3_MPU_COMPLETE_INFO
	{
		CString sLocation;
		CString sBucket;
		CString sKey;
		CString sETag;
	};

	struct ECSUTIL_EXT_CLASS S3_ENDPOINT_INFO
	{
		list<CString> EndpointList;
		CString sVersion;
	};

	struct ECSUTIL_EXT_CLASS S3_LIFECYCLE_RULE
	{
		CString sRuleID;									// user assigned rule ID
		CString sPath;										// optional subpath to apply rule (relative to bucket)
		bool bEnabled;										// enabled or disabled
		DWORD dwDays;										// number of days until expiration (current objects)
		DWORD dwNoncurrentDays;								// number of days (for non-current objects) until expiration
		FILETIME ftDate;									// date of expiration (current objects)

		S3_LIFECYCLE_RULE()
			: bEnabled(false)
			, dwDays(0)
			, dwNoncurrentDays(0)
		{
			ZeroFT(ftDate);
		}
		void Empty(void)
		{
			sRuleID.Empty();
			sPath.Empty();
			bEnabled = false;
			dwDays = 0;
			dwNoncurrentDays = 0;
			ZeroFT(ftDate);
		}
		bool IsEmpty(void)
		{
			return sRuleID.IsEmpty() || ((dwDays == 0) && (dwNoncurrentDays == 0) && IfFTZero(ftDate));
		}
		bool operator < (const S3_LIFECYCLE_RULE& rec) const
		{
			return sRuleID.CompareNoCase(rec.sRuleID) < 0;
		}
	};

	struct ECSUTIL_EXT_CLASS S3_LIFECYCLE_INFO
	{
		list<S3_LIFECYCLE_RULE> LifecycleRules;			// list of lifecycle rules

		S3_LIFECYCLE_INFO()
		{}
	};

	struct ECSUTIL_EXT_CLASS S3_ADMIN_USER_INFO
	{
		CString sUser;
		CString sNamespace;
		CString sSecret;
		CString sTags;
		CString sLink;
		FILETIME ftKeyCreate;
		FILETIME ftKeyExpiry;
		S3_ADMIN_USER_INFO()
		{
			ZeroFT(ftKeyCreate);
			ZeroFT(ftKeyExpiry);
		}
		bool IsEmpty(void)
		{
			return sUser.IsEmpty();
		}
		void EmptyRec(void)
		{
			sUser.Empty();
			sNamespace.Empty();
			sSecret.Empty();
			sTags.Empty();
			sLink.Empty();
			ZeroFT(ftKeyCreate);
			ZeroFT(ftKeyExpiry);
		}
	};

	struct ECSUTIL_EXT_CLASS S3_ADMIN_USER_KEY_INFO
	{
		CString sSecret1;
		CString sSecret2;
		CString sLink;
		FILETIME ftKeyCreate1;
		FILETIME ftKeyExpiry1;
		FILETIME ftKeyCreate2;
		FILETIME ftKeyExpiry2;

		S3_ADMIN_USER_KEY_INFO()
		{
			ZeroFT(ftKeyCreate1);
			ZeroFT(ftKeyExpiry1);
			ZeroFT(ftKeyCreate2);
			ZeroFT(ftKeyExpiry2);
		}
	};

	enum class E_S3_VERSIONING : BYTE
	{
		Off,
		On,
		Suspended
	};

	// stream support
	typedef void(*UPDATE_PROGRESS_CB)(int iProgress, void *pContext);

	struct ECSUTIL_EXT_CLASS STREAM_DATA_ENTRY
	{
		bool bLast;
		CBuffer Data;
		ULONGLONG ullOffset;
		STREAM_DATA_ENTRY()
			: bLast(false)
			, ullOffset(0)
		{}
		STREAM_DATA_ENTRY(bool bLastParam, const CBuffer& DataParam, ULONGLONG ullOffsetParam)
			: bLast(bLastParam)
			, Data(DataParam)
			, ullOffset(ullOffsetParam)
		{}
	};

	struct ECSUTIL_EXT_CLASS STREAM_CONTEXT
	{
		CSharedQueue<STREAM_DATA_ENTRY> StreamData;
		UPDATE_PROGRESS_CB UpdateProgressCB;
		void *pContext;
		int iAccProgress;						// how much data has been read so far
		bool bMultiPart;
		ULONGLONG ullTotalSize;					// on receive, keep track of the total size of the transfer
		STREAM_CONTEXT()
			: UpdateProgressCB(nullptr)
			, pContext(nullptr)
			, iAccProgress(0)
			, bMultiPart(false)
			, ullTotalSize(0ULL)
		{}
	};

	// S3 structs
	struct ECSUTIL_EXT_CLASS S3_DELETE_ENTRY
	{
		CString sKey;
		CString sVersionId;
		S3_DELETE_ENTRY(LPCTSTR pszKey = nullptr, LPCTSTR pszVersionId = nullptr)
			: sKey(pszKey)
			, sVersionId(pszVersionId)
		{}
		int Compare(const S3_DELETE_ENTRY& rec) const
		{
			return sKey.Compare(rec.sKey);
		}
		bool operator == (const S3_DELETE_ENTRY& rec) const
		{
			return Compare(rec) == 0;
		};
		bool operator != (const S3_DELETE_ENTRY& rec) const
		{
			return Compare(rec) != 0;
		};
		bool operator < (const S3_DELETE_ENTRY& rec) const
		{
			return Compare(rec) < 0;
		};
		bool operator <= (const S3_DELETE_ENTRY& rec) const
		{
			return Compare(rec) <= 0;
		};
		bool operator > (const S3_DELETE_ENTRY& rec) const
		{
			return Compare(rec) > 0;
		};
		bool operator >= (const S3_DELETE_ENTRY& rec) const
		{
			return Compare(rec) >= 0;
		};
	};

	struct ECSUTIL_EXT_CLASS S3_BUCKET_INFO
	{
		CString sName;
		FILETIME ftCreationDate;
		S3_BUCKET_INFO()
		{
			ZeroFT(ftCreationDate);
		}
		void Empty()
		{
			sName.Empty();
			ZeroFT(ftCreationDate);
		}
	};

	struct ECSUTIL_EXT_CLASS S3_SERVICE_INFO
	{
		CString sOwnerID;
		CString sOwnerDisplayName;
		list<S3_BUCKET_INFO> BucketList;
	};

	struct ECSUTIL_EXT_CLASS S3_UPLOAD_PART_ENTRY_EVENT
	{
		CSharedQueueEvent QueueEvent;				// event linked to StreamQueue

		S3_UPLOAD_PART_ENTRY_EVENT()
		{}

		// copy constructor
		S3_UPLOAD_PART_ENTRY_EVENT(const S3_UPLOAD_PART_ENTRY_EVENT& src)
		{
			(void)src;
		};

		const S3_UPLOAD_PART_ENTRY_EVENT& operator =(const S3_UPLOAD_PART_ENTRY_EVENT& src)
		{
			if (&src == this)
				return *this;
			return *this;
		};		//lint !e1539	// members not assigned by assignment operator
	};

	struct ECSUTIL_EXT_CLASS S3_UPLOAD_PART_ENTRY
	{
		UINT uPartNum;						// part number
		CString sETag;						// ETag for part
		CBuffer Checksum;					// if MD5, checksum for the current part
		ULONGLONG ullPartSize;				// size of this part, in bytes. if not the last part, it can't be < 5MB
		ULONGLONG ullBaseOffset;			// base offset of this part in the file
		STREAM_CONTEXT StreamQueue;			// queue used to send data to the HTTP upload thread
		S3_UPLOAD_PART_ENTRY_EVENT Event;	// holds all fields that can't be assigned
		bool bInProcess;					// set if it was pushed onto the thread pool queue
		bool bComplete;						// if set, this entry is done
		DWORD dwRetryNum;					// keep track of how many retries were needed for this part
		ULONGLONG ullCursor;				// current point in transfer

		S3_UPLOAD_PART_ENTRY()
			: uPartNum(0)
			, ullPartSize(0ULL)
			, ullBaseOffset(0ULL)
			, bInProcess(false)
			, bComplete(false)
			, dwRetryNum(0)
			, ullCursor(0ULL)
		{}
	};

	struct ECSUTIL_EXT_CLASS S3_UPLOAD_PART_INFO
	{
		CString sResource;					// HTTP resource used
		CString sBucket;					// bucket name
		CString sKey;						// object key
		CString sUploadId;					// multipart upload ID
	};

	struct ECSUTIL_EXT_CLASS S3_LIST_MULTIPART_UPLOADS_ENTRY
	{
		CString sKey;
		CString sUploadId;
		CString sInitiatorId;
		CString sInitiatorDisplayName;
		CString sOwnerId;
		CString sOwnerDisplayName;
		CString sStorageClass;
		FILETIME ftInitiated;

		S3_LIST_MULTIPART_UPLOADS_ENTRY()
		{
			ZeroFT(ftInitiated);
		}

		void EmptyRec()
		{
			sKey.Empty();
			sUploadId.Empty();
			sInitiatorId.Empty();
			sInitiatorDisplayName.Empty();
			sOwnerId.Empty();
			sOwnerDisplayName.Empty();
			sStorageClass.Empty();
			ZeroFT(ftInitiated);
		}
	};

	struct ECSUTIL_EXT_CLASS S3_LIST_MULTIPART_UPLOADS
	{
		CString sBucket;
		UINT uMaxUploads;
		list<S3_LIST_MULTIPART_UPLOADS_ENTRY> ObjectList;

		S3_LIST_MULTIPART_UPLOADS()
			: uMaxUploads(0)
		{}
	};

	enum E_S3_ACL_VALUES : BYTE
	{
		AAV_INVALID,			// not a valid ACL entry
		AAV_NONE,				// no access
		AAV_READ,				// read-only access
		AAV_WRITE,				// write-only access
		AAV_FULL_CONTROL,		// read/write access
		AAV_READ_ACP,			// read permissions
		AAV_WRITE_ACP,			// write permissions
	};

	struct ECSUTIL_EXT_CLASS ACL_ENTRY
	{
		CString sID;			// user or group name
		CString sDisplayName;
		E_S3_ACL_VALUES Acl;	// access for user or group
		bool bGroup;			// if set, group ACL Entry

		ACL_ENTRY()
			: Acl(AAV_INVALID)
			, bGroup(false)
		{}

		ACL_ENTRY(LPCTSTR pszUID, E_S3_ACL_VALUES AclParam, bool bGroupParam)
			: sID(pszUID)
			, Acl(AclParam)
			, bGroup(bGroupParam)
		{}

		bool operator < (const ACL_ENTRY& rec) const
		{
			return sID < rec.sID;
		};

		CString GetAclString() const
		{
			switch (Acl)
			{
			case AAV_NONE: return _T("NONE");
			case AAV_READ: return _T("READ");
			case AAV_WRITE: return _T("WRITE");
			case AAV_FULL_CONTROL: return _T("FULL_CONTROL");
			case AAV_READ_ACP: return _T("READ_ACP");
			case AAV_WRITE_ACP: return _T("WRITE_ACP");
			default:
				break;
			}
			return _T("INVALID");
		}

		void Clear(void)
		{
			Acl = AAV_INVALID;
			bGroup = false;
			sID.Empty();
			sDisplayName.Empty();
		}
	};

	static CString GetAclHeaderString(E_S3_ACL_VALUES Acl)
	{
		switch (Acl)
		{
		case AAV_NONE: return _T("");
		case AAV_READ: return _T("x-amz-grant-read");
		case AAV_WRITE: return _T("x-amz-grant-write");
		case AAV_FULL_CONTROL: return _T("x-amz-grant-full-control");
		case AAV_READ_ACP: return _T("x-amz-grant-read-acp");
		case AAV_WRITE_ACP: return _T("x-amz-grant-write-acp");
		default:
			break;
		}
		return _T("");
	}

	typedef list<ACL_ENTRY> UIDList_t;

	struct ECSUTIL_EXT_CLASS S3_ERROR
	{
		DWORD dwError;					// WIN32 error
		DWORD dwHttpError;				// HTTP error
		E_S3_ERROR_TYPE S3Error;		// S3 error
		CString sS3Code;				// S3: error code
		CString sS3Resource;			// S3: resource associated with the error
		CString sS3RequestID;			// S3: request ID associated with the error
		CString sDetails;				// ECS Admin: additional details

		S3_ERROR(DWORD dwErrorParam = ERROR_SUCCESS)
			: dwError(dwErrorParam)
			, dwHttpError(0)
			, S3Error(S3_ERROR_SUCCESS)
		{
		}

		bool IfError() const;
		CString Format(bool bOneLine = false) const;
		bool operator ==(const S3_ERROR &Error) const
		{
			return (dwError == Error.dwError)
				&& (dwHttpError == Error.dwHttpError)
				&& (S3Error == Error.S3Error)
				&& (sS3Code == Error.sS3Code);
		}
		bool operator !=(const S3_ERROR &Error) const
		{
			return !operator ==(Error);
		}
		bool IfNotFound(void) const
		{
			return ((dwError == ERROR_SUCCESS)
				&& ((S3Error == S3_ERROR_NoSuchKey)
					|| (dwHttpError == HTTP_STATUS_NOT_FOUND)));
		}
		S3_ERROR(const S3_ERROR& src)
		{
			Assign(src);
		}
		const S3_ERROR& operator =(const S3_ERROR& src)
		{
			if (&src == this)
				return *this;
			Assign(src);
			return *this;
		};
	private:
		void Assign(const S3_ERROR& src)
		{
			dwError = src.dwError;
			dwHttpError = src.dwHttpError;
			S3Error = src.S3Error;
			sS3Code = src.sS3Code;
			sS3Resource = src.sS3Resource;
			sS3RequestID = src.sS3RequestID;
			sDetails = src.sDetails;
			SetError();						// if http error, set dwError code to reflect it
		}
		void SetError(void);
	};

	class CS3ErrorInfo : public CErrorInfo
	{
	public:
		CECSConnection::S3_ERROR Error;

		CS3ErrorInfo()
		{
		}

		CS3ErrorInfo(const CECSConnection::S3_ERROR& ErrorParam)
		{
			Error = ErrorParam;
		}

		CS3ErrorInfo(LPCTSTR pszFile, DWORD dwLineParam, const CECSConnection::S3_ERROR& ErrorParam)
		{
			sFile = pszFile;
			dwLine = dwLineParam;
			Error = ErrorParam;
		}

		CS3ErrorInfo(LPCTSTR pszFile, DWORD dwLineParam, NTSTATUS Status, const CECSConnection::S3_ERROR& ErrorParam, LPCTSTR pszAdditionalInfo = nullptr)
		{
			sFile = pszFile;
			dwLine = dwLineParam;
			dwError = Status;
			Error = ErrorParam;
			if (pszAdditionalInfo != nullptr)
				sAdditionalInfo = pszAdditionalInfo;
		}

		CS3ErrorInfo(const CErrorInfo& ErrorInfo)
			: CErrorInfo(ErrorInfo)
		{
		}

		bool IfError() const
		{
			return Error.IfError() || (dwError != ERROR_SUCCESS);
		}
		CString Format() const
		{
			S3_ERROR TmpAtError = Error;
			if ((TmpAtError.dwError == ERROR_SUCCESS) && (dwError != ERROR_SUCCESS))
				TmpAtError.dwError = dwError;
			CString sMsg(TmpAtError.Format());
			if (!sAdditionalInfo.IsEmpty())
				sMsg += _T("\r\n") + sAdditionalInfo;
			return sMsg;
		}
	};

	struct ECSUTIL_EXT_CLASS ECS_CERT_INFO
	{
		CString sCertName;								// if certificate error, these fields contain info from the server cert
		CString sCertSubject;
		CBuffer SerializedCert;							// if self-signed cert, this contains the certificate from the server
	};

	struct ECSUTIL_EXT_CLASS S3_SYSTEM_METADATA
	{
		FILETIME ftLastMod;		// Last user-data modification time
		ULONGLONG llSize;		// Object size in bytes (size)
		bool bIsLatest;			// only used for S3 versions: indicates it is the latest version
		bool bDeleted;			// object has been deleted
		CString sVersionId;		// only used for S3 versions: version ID of object
		CString sETag;			// S3: unique key used to determine if the file has changed
		CString sOwnerDisplayName;	// S3: owner display name
		CString sOwnerID;		// S3: owner ID

		S3_SYSTEM_METADATA()
		{
			ZeroFT(ftLastMod);
			llSize = 0;
			bIsLatest = false;
			bDeleted = false;
		}

		void Empty()
		{
			ZeroFT(ftLastMod);
			llSize = 0;
			sETag.Empty();
			sOwnerDisplayName.Empty();
			bIsLatest = false;
			bDeleted = false;
			sVersionId.Empty();
		}

		bool operator==(const S3_SYSTEM_METADATA& rec) const
		{
			return (ftLastMod == rec.ftLastMod)
				&& (llSize == rec.llSize)
				&& (sETag == rec.sETag)
				&& (sOwnerDisplayName == rec.sOwnerDisplayName)
				&& (sOwnerID == rec.sOwnerID)
				&& (bIsLatest == rec.bIsLatest)
				&& (bDeleted == rec.bDeleted)
				&& (sVersionId == rec.sVersionId);
		}

		bool operator!=(const S3_SYSTEM_METADATA& rec) const
		{
			return !operator==(rec);
		}
	};

	struct ECSUTIL_EXT_CLASS DIR_ENTRY
	{
		CString sName;
		bool bDir;				// if set, subdir
		S3_SYSTEM_METADATA Properties;

		DIR_ENTRY()
			: bDir(false)
		{
		}

		bool operator < (const DIR_ENTRY& rec) const
		{
			int iComp = sName.Compare(rec.sName);
			return iComp < 0;
		};
	};
	typedef list<DIR_ENTRY> DirEntryList_t;

	// function to check if the current request needs to be aborted (thread exit or application termination)
	// returns true if current request needs to be canceled immediately
	typedef bool (*TEST_SHUTDOWN_CB)(void *pContext);
	typedef void (*ECS_DISCONNECT_CB)(CECSConnection *pHost, const CS3ErrorInfo *pError, bool *pbDisconnected);
	typedef CString (*GET_HTTP_ERROR_TEXT_CB)(DWORD dwHttpError);
	typedef void (*GET_HOST_PERF_COUNTERS_CB)(LPCTSTR pszHost, ULONGLONG **ppullPerfBytesSentToHost_Host, ULONGLONG **ppullPerfBytesRcvFromHost_Host, int *piHostCounters);
	typedef void (*DIR_LISTING_CB)(size_t Size, void *pContext);

public:
	struct XML_DIR_LISTING_CONTEXT;
	struct LISTING_CONTEXT_MONITOR
	{
		CEvent evUpdate;
		DWORD dwNumProcessed;
		list<XML_DIR_LISTING_CONTEXT *>::iterator itDirList;

		LISTING_CONTEXT_MONITOR()
			: dwNumProcessed(0)
		{
		}
	};

	struct XML_DIR_LISTING_CONTEXT
	{
		// parameters from listing
		CString sPathIn;

		bool bNeedMetadata;
		bool bDontTranslateLongname;

		bool bGotSysMetadata;
		bool bGotType;
		bool bValid;					// don't latch onto this record until this is true
		bool bGotRootElement;			// sanity check. we need to see the root element to verify that the XML looks good
		bool bSingle;					// just get some entries. not all
		bool bS3Versions;				// S3 version listing
		bool bGotKey;					// set if it saw any key. this is used to determine if the folder exists at all
		DirEntryList_t *pDirList;
		CCriticalSection csDirList;
		CECSConnection::DIR_ENTRY Rec;
		CString sLastMetaName;
		CString sLastMetaValue;
		LPCTSTR pszSearchName;
		CString *psRetSearchName;

		list<LISTING_CONTEXT_MONITOR *> DirMonitorList;

		bool bDone;
		S3_ERROR Error;

		// S3 fields
		bool bIsTruncated;
		CString sPrefix;					// the prefix received from the command results
		CString sS3NextMarker;				// passed to marker in next request
		CString sS3NextKeyMarker;			// passed to key-marker in next request
		CString sS3NextVersionIdMarker;		// passed to version-id-marker in next request

		XML_DIR_LISTING_CONTEXT()
			: bNeedMetadata(false)
			, bDontTranslateLongname(false)
			, bGotSysMetadata(false)
			, bGotType(false)
			, bValid(false)
			, bGotRootElement(false)
			, bSingle(false)
			, bS3Versions(false)
			, bGotKey(false)
			, pDirList(nullptr)
			, pszSearchName(nullptr)
			, psRetSearchName(nullptr)
			, bDone(false)
			, bIsTruncated(false)
		{
		}

		void EmptyRec()
		{
			bGotType = false;
			Rec.sName.Empty();
			Rec.Properties.Empty();
		}
	};

	struct HEADER_REQ
	{
		CString sLabel;
		list<CString> ContentList;

		HEADER_REQ(LPCTSTR pszLabel = nullptr)
		{
			if (pszLabel != nullptr)
				sLabel = pszLabel;
		}
	};

	struct HEADER_STRUCT
	{
		CString sLabel;
		CString sContents;

		HEADER_STRUCT(LPCTSTR pszLabel = nullptr, LPCTSTR pszContents = nullptr)
		{
			if (pszLabel != nullptr)
				sLabel = pszLabel;
			if (pszContents != nullptr)
				sContents = pszContents;
		}
	};

private:
	struct HTTP_CALLBACK_EVENT
	{
		CEvent evCmd;								// event is fired when async callback is received

		HTTP_CALLBACK_EVENT()
			: evCmd(false, true)
		{
		}

		// copy constructor
		HTTP_CALLBACK_EVENT(const HTTP_CALLBACK_EVENT& src)
		{
			(void)src;
		};

		const HTTP_CALLBACK_EVENT& operator =(const HTTP_CALLBACK_EVENT& src)
		{
			if (&src == this)
				return *this;
			return *this;
		};		//lint !e1539	// members not assigned by assignment operator
	};

	// http async context
	struct HTTP_CALLBACK_CONTEXT
	{
		CCriticalSection csContext;
		HTTP_CALLBACK_EVENT Event;					// event is fired when async callback is received
		WINHTTP_ASYNC_RESULT Result;				// if error, this contains the error code
		vector<DWORD> CallbacksReceived;			// the last callbacks received
		DWORD dwReadLength;							// WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE, WINHTTP_CALLBACK_STATUS_READ_COMPLETE
		DWORD dwBytesWritten;						// WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE
		DWORD dwSecureError;						// explanation for SSL errors (WINHTTP_CALLBACK_STATUS_FLAG_...)
		bool bHeadersAvail;							// WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE
		bool bSendRequestComplete;					// WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE
		bool bFailed;								// WINHTTP_CALLBACK_STATUS_REQUEST_ERROR
		bool bComplete;								// set if the command has completed
		bool bHandleCreated;						// WINHTTP_CALLBACK_STATUS_HANDLE_CREATED
		bool bHandleClosing;						// WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING
		bool *pbCallbackRegistered;					// pointer to flag indicating the callback has been registered
		bool bDisableSecureLog;						// if set, don't log security errors in the callback
		BYTE *pReadData;							// WINHTTP_CALLBACK_STATUS_READ_COMPLETE
		CECSConnection *pHost;							// allow access to the host record

		HTTP_CALLBACK_CONTEXT()
		{
			Reset();
		}

		void Reset()
		{
			CSingleLock lock(&csContext, true);
			ZeroMemory(&Result, sizeof(Result));
			dwReadLength = 0;
			dwBytesWritten = 0;
			bHeadersAvail = false;
			bSendRequestComplete = false;
			bFailed = false;
			bComplete = false;
			pReadData = nullptr;
			bHandleCreated = false;
			bHandleClosing = false;
			pbCallbackRegistered = nullptr;
			CallbacksReceived.clear();
			bDisableSecureLog = false;
			dwSecureError = 0;
			pHost = nullptr;
		}
	};

	struct ABORT_ENTRY
	{
		const bool *pbAbort;			// pointer to abort bool
		bool bAbortIfTrue;				// determines sense of pbAbort
		TEST_SHUTDOWN_CB ShutdownCB;
		void *pShutdownContext;			// context to pass back to shutdown callback

		ABORT_ENTRY()
			: pbAbort(nullptr)
			, bAbortIfTrue(true)
			, ShutdownCB(nullptr)
			, pShutdownContext(nullptr)
		{
		}
	};

	struct SESSION_MAP_KEY
	{
		CString sHostEntry;					// host entry
		CString sIP;						// IP/hostname
		long lKey;							// random number to make the key unique
		SESSION_MAP_KEY()
			: lKey(0)
		{}
		int CompareEntry(const SESSION_MAP_KEY& Src) const
		{
			int iDiff = sHostEntry.Compare(Src.sHostEntry);
			if (iDiff == 0)
				iDiff = sIP.Compare(Src.sIP);
			if (iDiff == 0)
				iDiff = COMPARE(lKey, Src.lKey);
			return iDiff;
		}
		bool operator <(const SESSION_MAP_KEY& Src) const
		{
			return CompareEntry(Src) < 0;
		}
		CString Format(void) const
		{
			return _T("Session: ") + sHostEntry + _T(", ") + sIP + _T(", ") + FmtNum(lKey);
		}
	};

	struct SESSION_MAP_VALUE
	{
		bool bInUse;								// if true, entry is in use
		bool bKillWhenDone;							// the host entry has changed. when releasing this entry, close and delete it so it gets recreated with the new info
		CInternetHandle hSession;
		CInternetHandle hConnect;
		FILETIME ftIdleTime;						// set when entry is freed
		SESSION_MAP_VALUE()
			: bInUse(false)
			, bKillWhenDone(false)
			, hSession(nullptr)
			, hConnect(nullptr)
		{
			GetSystemTimeAsFileTime(&ftIdleTime);
		}
		void CloseAll(void)
		{
			if (hConnect.IfOpen())
				(void)hConnect.CloseHandle();
			if (hSession.IfOpen())
				(void)hSession.CloseHandle();
		}
		~SESSION_MAP_VALUE()
		{
			CloseAll();
		}
		CString Format(void) const
		{
			return CString(bInUse ? _T("InUse, ") : _T("free, "))
				+ (bKillWhenDone ? _T("Kill, ") : _T("NoKill, "))
				+ _T("hSession:") + FmtNum((HINTERNET)hSession, 0, false, true) + _T(", ")
				+ _T("hConnect:") + FmtNum((HINTERNET)hConnect, 0, false, true);
		}
	};

	class CECSConnectionSession
	{
	private:
		SESSION_MAP_KEY Key;
	public:
		SESSION_MAP_VALUE *pValue;
		CECSConnectionSession();
		~CECSConnectionSession();
		void AllocSession(LPCTSTR pszHost, LPCTSTR pszIP);
		void ReleaseSession(void) throw();
		CString Format(void) const;
	};
	static CCriticalSection csSessionMap;
	static map<SESSION_MAP_KEY, SESSION_MAP_VALUE> SessionMap;			// protected by csSessionMap
	static long lSessionKeyValue;											// used to make session key unique

	// all state fields. These are not copied during assignment or copy constructor
	struct CECSConnectionState
	{
		CECSConnection *pECSConnection;			// pointer to parent record
		CECSConnectionSession Session;			// current allocated session (cached hSession and hConnect)
		CInternetHandle hRequest;				// request handle
		bool bCallbackRegistered;
		bool bS3Admin;							// admin API
		bool bCaseSensitive;					// disable case insensitive support
		bool bOverrideLowerCase;				// if set, use the paths exactly as presented, despite the current lower case settings
		bool bECSExtension;						// if set, ECS extension and don't worry about getting x-amz-request-id back
		map<CString,HEADER_STRUCT> Headers;
		UINT iIPList;							// index into IPList showing currently used
		deque<CString> IPListLocal;				// local copy of IPListHost to be used only for this request
		CString sEmcToken;						// holds last x-emc-token received
		CEvent evThrottle;						// triggered if throttling and throttle interval was refreshed
		HTTP_CALLBACK_CONTEXT CallbackContext;
		DWORD dwCurrentThread;
		DWORD dwProxyAuthScheme;				// if non-zero, proxy requires authorization (WINHTTP_AUTH_SCHEME_...)
		DWORD dwAuthScheme;						
		CCriticalSection csAbortList;			// critical section used for AbortList
		DWORD dwSecureError;					// explanation for SSL errors (WINHTTP_CALLBACK_STATUS_FLAG_...)
		bool bDisableSecureLog;					// if set, don't log security errors in the callback
		bool bDisableUserSupport;				// don't redirect to a different user based on path
		bool bUseFolderObject;					// if set (default), and S3, and if folder is referenced, add the folder object to the path (S3_FOLDER_FILE_NAME)
		ECS_CERT_INFO CertInfo;				// holds certificate info if it is from an unknown CA
		CString sLastNormalizedPath;			// saved from last normalized path operation
		list<S3_DELETE_ENTRY> S3DeletePathList;	// accumulated list of objects to delete. used during DeleteS3
		ULONGLONG ullReadBytes;					// keep track of number of received bytes in a stream receive
		DWORD dwSecurityFlagsAdd;				// flags to add to security options from default
		DWORD dwSecurityFlagsSub;				// flags to subtract from security options from default
		CString sHTTPUser;						// for HTTP authentication
		CString sHTTPPassword;					// for HTTP authentication
		CString sX_SDS_AUTH_TOKEN;				// for HTTP authentication: after login this contains the auth token used on subsequent calls

		CECSConnectionState()
			: pECSConnection(nullptr)
			, hRequest(nullptr)
			, bCallbackRegistered(false)
			, bS3Admin(false)
			, bCaseSensitive(false)
			, bOverrideLowerCase(false)
			, bECSExtension(false)
			, iIPList(0)
			, dwCurrentThread(0)
			, dwProxyAuthScheme(0)
			, dwAuthScheme(0)
			, dwSecureError(0)
			, bDisableSecureLog(false)
			, bDisableUserSupport(false)
			, bUseFolderObject(true)
			, ullReadBytes(0ULL)
			, dwSecurityFlagsAdd(0)
			, dwSecurityFlagsSub(0)
		{}
		~CECSConnectionState()
		{
			// Close any open handles.
			CloseRequest();
			pECSConnection = nullptr;
		}

		// copy constructor
		CECSConnectionState(const CECSConnectionState& src)
			: pECSConnection(nullptr)
			, hRequest(nullptr)
			, bCallbackRegistered(false)
			, bS3Admin(false)
			, bCaseSensitive(false)
			, bOverrideLowerCase(false)
			, bECSExtension(false)
			, iIPList(0)
			, dwCurrentThread(0)
			, dwProxyAuthScheme(0)
			, dwAuthScheme(0)
			, dwSecureError(0)
			, bDisableSecureLog(false)
			, bDisableUserSupport(false)
			, bUseFolderObject(true)
			, ullReadBytes(0ULL)
			, dwSecurityFlagsAdd(0)
			, dwSecurityFlagsSub(0)
		{
			(void)src;
		};

		const CECSConnectionState& operator =(const CECSConnectionState& src)
		{
			if (&src == this)
				return *this;
			return *this;
		};		//lint !e1539	// members not assigned by assignment operator
		void CloseRequest(bool bSaveCert = false) throw();
	};

	struct CECSConnectionStateCS
	{
		mutable CCriticalSection csStateMap;
		mutable map<DWORD, shared_ptr<CECSConnectionState>> StateMap;

		CECSConnectionStateCS()
		{
		}

		// copy constructor
		CECSConnectionStateCS(const CECSConnectionStateCS& src)
		{
			(void)src;
		};

		const CECSConnectionStateCS& operator =(const CECSConnectionStateCS& src)
		{
			if (&src == this)
				return *this;
			return *this;
		};		//lint !e1539	// members not assigned by assignment operator
	};

	CString sSecret;
	CString sUser;
	CString sHost;
	CString sUserAgent;
	deque<CString> IPListHost;				// list of IP addresses/DNS host names to access this server. protected by rwlIPListHost
	mutable CSimpleRWLock rwlIPListHost;	// lock for IPListHost
	INTERNET_PORT Port;
	bool bSSL;								// use HTTPS if set
	bool bUseDefaultProxy;					// if set and sProxy is empty, use default proxy (see netsh winhttp)
	bool bTestConnection;					// if test connection, timeout quickly and don't do anything unnecessary for the test
	CString sProxy;							// optional proxy server
	DWORD dwProxyPort;						// if sProxy not emtpy, contains the port for the proxy
	CString sProxyUser;						// proxy server authentication
	CString sProxyPassword;					// proxy server password
	DWORD dwHttpsProtocol;					// bit field of acceptable protocols
	bool bCheckShutdown;					// if set, then abort request if during service shutdown (default)
	ECS_DISCONNECT_CB DisconnectCB;		// disconnect callback
	list<ABORT_ENTRY> AbortList;			// list of abort entries

	// S3 settings
	CString sS3KeyID;						// S3 Key ID
	CString sS3Region;						// S3 region (ie us-east-1)

	CECSConnectionStateCS Events;				// a place to put fields that can't be assigned

	// timeout values
	DWORD dwWinHttpOptionConnectRetries;
	DWORD dwWinHttpOptionConnectTimeout;	// default 60 sec
	DWORD dwWinHttpOptionReceiveResponseTimeout;	// default 90 sec
	DWORD dwWinHttpOptionReceiveTimeout;	// default 30 sec
	DWORD dwWinHttpOptionSendTimeout;		// default 30 sec
	DWORD dwLongestTimeout;					// use this to determine the longest time to wait for any one command to finish
	DWORD dwBadIPAddrAge;					// how long a bad IP entry in the host entry will stay bad before being put back into service (seconds)

	DWORD dwMaxRetryCount;
	DWORD dwMaxWriteRequest;				// if non-zero, this specifies the maximum write request. larger requests should be broken into smaller ones

	DWORD dwHttpSecurityFlags;				// global default for security flags (see WinHttpSetOption, WINHTTP_OPTION_SECURITY_FLAGS)

	// throttle info

	// CThrottleTimerThread
	// controls the throttle thread
	struct CThrottleTimerThread : public CSimpleWorkerThread
	{
		FILETIME ftLast;					// last activation - used to determine the exact time between calls
		CThrottleTimerThread();
		~CThrottleTimerThread();
	protected:
		void DoWork();
	public:
		bool InitInstance();
	};

	struct THROTTLE_INFO
	{
		int iBytesSec;						// bytes/sec throttle rate for this URL
		int iBytesCurInterval;				// how many bytes can be sent in the current interval

		THROTTLE_INFO()
		{
			iBytesSec = 0;
			iBytesCurInterval = 0;
		}
	};
	struct THROTTLE_REC
	{
		THROTTLE_INFO Upload;
		THROTTLE_INFO Download;
	};
	struct RECENT_PATH
	{
		CString sPath;
		FILETIME ftTime;					// time the path was added
		RECENT_PATH(LPCTSTR pszPath = nullptr)
			: sPath(pszPath)
		{
			GetSystemTimeAsFileTime(&ftTime);
		}
	};
	static bool bInitialized;							// starts out false. If false, timeouts are very short. must call SetInitialized to get regular timeouts
	static CThrottleTimerThread TimerThread;				// throttle timer thread
	static map<CString,THROTTLE_REC> ThrottleMap;	// global map used by all CECSConnection objects, key is host name
	static CCriticalSection csThrottleMap;				// critical section protecting ThrottleMap
	static list<CECSConnection *> ECSConnectionList;		// list of all CECSConnection objects (protected by csThrottleMap)
	static list<XML_DIR_LISTING_CONTEXT *> DirListList;	// listing of current dir listing operations
	static CCriticalSection csDirListList;				// critical section protecting DirListList
	static DWORD dwGlobalHttpsProtocol;					// bit field of acceptable protocols

	static CString sAmzMetaPrefix;						// just a place to hold "x-amz-meta-"

private:

	// BadIPMap
	// holds any IP address that seems to be bad
	// if an IP fails, it will be put into this map for a bit
	// when it is in the map, it will not be used in any connection attempts
	// when it times out it will be put into rotation. if it fails again - back on the map
	struct BAD_IP_KEY
	{
		CString sHostName;				// host entry name
		CString sIP;					// IP address or FQDN

		BAD_IP_KEY()
		{
		}
		BAD_IP_KEY(const CString& sHostParam, const CString& sIPParam)
		{
			sHostName = sHostParam;
			sIP = sIPParam;
		}
		int Compare(const BAD_IP_KEY& Key) const
		{
			int iDiff = sHostName.Compare(Key.sHostName);
			if (iDiff != 0)
				return iDiff;
			return sIP.Compare(Key.sIP);
		}
		bool operator < (const BAD_IP_KEY& Key) const
		{
			return Compare(Key) < 0;
		}
		bool operator > (const BAD_IP_KEY& Key) const
		{
			return Compare(Key) > 0;
		}
		bool operator <= (const BAD_IP_KEY& Key) const
		{
			return Compare(Key) <= 0;
		}
		bool operator >= (const BAD_IP_KEY& Key) const
		{
			return Compare(Key) >= 0;
		}
		bool operator == (const BAD_IP_KEY& Key) const
		{
			return Compare(Key) == 0;
		}
		bool operator != (const BAD_IP_KEY& Key) const
		{
			return Compare(Key) != 0;
		}
	};
	struct BAD_IP_ENTRY
	{
		FILETIME ftError;				// time that error occurred
		CS3ErrorInfo ErrorInfo;		// last error received
		BAD_IP_ENTRY()
		{
			ZeroFT(ftError);
		}
		BAD_IP_ENTRY(const CS3ErrorInfo& ErrorInfoParam)
		{
			ErrorInfo = ErrorInfoParam;
			GetSystemTimeAsFileTime(&ftError);
		}
	};
	static CCriticalSection csBadIPMap;
	static map<BAD_IP_KEY,BAD_IP_ENTRY> BadIPMap;
	static map<CString,UINT> LoadBalMap;					// global IP selector for all entries

	CECSConnectionState& GetStateBuf(DWORD dwThreadID = 0);
	BOOL WinHttpQueryHeadersBuffer(__in HINTERNET hRequest, __in DWORD dwInfoLevel, __in_opt LPCTSTR pwszName, __inout CBuffer& RetBuf, __inout LPDWORD lpdwIndex);
	CString GetCanonicalTime() const;
	static FILETIME ParseCanonicalTime(LPCTSTR pszCanonTime);
	void sign(const CString& secretStr, const CString& hashStr, CString& encodedStr);
	CString signRequestS3v2(const CString& secretStr, const CString& method, const CString& resource, const map<CString, HEADER_STRUCT>& headers, LPCTSTR pszExpires = nullptr);

	static void CALLBACK HttpStatusCallback(__in  HINTERNET hInternet,__in  DWORD_PTR dwContext,__in  DWORD dwInternetStatus,__in  LPVOID lpvStatusInformation,__in  DWORD dwStatusInformationLength);
	DWORD InitSession();
	void CloseAll() throw();
	void InitHeader(void);
	void AddHeader(LPCTSTR pszHeaderLabel, LPCTSTR pszHeaderText, bool bOverride = true);
	void SetTimeouts(const CInternetHandle& hRequest);
	CS3ErrorInfo SendRequestInternal(LPCTSTR pszMethod, LPCTSTR pszResource, const void *pData, DWORD dwDataLen, CBuffer& RetData, list<HEADER_REQ> *pHeaderReq, DWORD dwReceivedDataHint, DWORD dwBufOffset, bool *pbGotServerResponse, STREAM_CONTEXT *pStreamSend, STREAM_CONTEXT *pStreamReceive, ULONGLONG ullTotalLen);
	LPCTSTR GetCurrentServerIP(void);
	void TestAllIPBad(void);
	bool GetNextECSIP(map<CString, BAD_IP_ENTRY>& IPUsed);
	void LogBadIPAddr(const map<CString,BAD_IP_ENTRY>& IPUsed);
	void PrepareCmd(void);
	void CleanupCmd(void);
	bool WaitComplete(DWORD dwCallbackExpected);
	bool WaitStreamSend(STREAM_CONTEXT *pStreamSend, const CSharedQueueEvent& MsgEvent);
	void SetNewFailoverIP(void);
	DWORD ChooseAuthScheme(DWORD dwSupportedSchemes);
	CString FormatAuthScheme(void);
	// internal version of DirListing allowing it to search for a single file/dir and not return the whole list
	S3_ERROR DirListingInternal(LPCTSTR pszPathIn, DirEntryList_t& DirList, LPCTSTR pszSearchName, CString& sRetSearchName, bool bS3Versions, bool bSingle, DWORD *pdwGetECSRetention);
	CString signS3ShareableURL(const CString& sResource, const CString& sExpire);
	void KillHostSessions(void);
	void DeleteS3Send(void);
	void DeleteS3Internal(const list<S3_DELETE_ENTRY>& PathList);
	S3_ERROR CopyS3(LPCTSTR pszSrcPath, LPCTSTR pszTargetPath, LPCTSTR pszVersionId, bool bCopyMD, ULONGLONG *pullObjSize, const list<HEADER_STRUCT> *pMDList);

public:
	CECSConnection();
	~CECSConnection();
	CECSConnection(const CECSConnection& Rec);
	static void Init(void);
	static S3_ERROR CECSConnection::ParseS3Timestamp(const CString& sS3Time, FILETIME& ftTime);
	static void SetGlobalHttpsProtocol(DWORD dwGlobalHttpsProtocolParam);

	static void SetInitialized(void);				// global initialized flag. must be called to set regular timeouts
	void SetSecret(LPCTSTR pszSecret);		// set shared secret string in base64
	void SetUser(LPCTSTR pszUser);			// set user ID
	void SetHost(LPCTSTR pszHost);			// set Host
	void SetIPList(const deque<CString>& IPListParam);
	void GetIPList(deque<CString>& IPListParam);
	void SetSSL(bool bSSLParam);
	void SetRegion(LPCTSTR pszS3Region);
	void SetS3KeyID(LPCTSTR pszS3KeyID);
	CString GetS3KeyID(void);
	static DWORD ParseISO8601Date(LPCTSTR pszDate, FILETIME& ftTime, bool bLocal = false);
	static CString FormatISO8601Date(const FILETIME& ftDate, bool bLocal, bool bMilliSec = true);

	void SetUserAgent(LPCTSTR pszUserAgent);					// typically app name/version. put in 'user agent' field in HTTP protocol
	void SetPort(INTERNET_PORT PortParam);
	void SetProxy(bool bUseDefaultProxyParam, LPCTSTR pszProxy, DWORD dwPort, LPCTSTR pszProxyUser, LPCTSTR pszProxyPassword);
	void SetTest(bool bTestParam);
	void SetHttpsProtocol(DWORD dwHttpsProtocolParam);
	static void SetThrottle(LPCTSTR pszHost, int iUploadThrottleRate, int iDownloadThrottleRate);
	static void TerminateThrottle(void);
	void IfThrottle(bool *pDownloadThrottle, bool *pUploadThrottle);
	void RegisterShutdownCB(TEST_SHUTDOWN_CB ShutdownParamCB, void *pContext);
	void UnregisterShutdownCB(TEST_SHUTDOWN_CB ShutdownParamCB, void *pContext);
	void RegisterAbortPtr(const bool *pbAbort, bool bAbortTrue = true);
	void UnregisterAbortPtr(const bool *pbAbort);
	void CheckShutdown(bool bCheckShutdownParam=true);		// set this to false if you need to do ops during a shutdown
	void RegisterDisconnectCB(ECS_DISCONNECT_CB DisconnectParamCB);
	void SetTimeouts(
		DWORD dwWinHttpOptionConnectRetriesParam,
		DWORD dwWinHttpOptionConnectTimeoutParam,
		DWORD dwWinHttpOptionReceiveResponseTimeoutParam,
		DWORD dwWinHttpOptionReceiveTimeoutParam,
		DWORD dwWinHttpOptionSendTimeoutParam,
		DWORD dwBadIPAddrAge);
	CString GetHost(void) const;
	void SetRetries(DWORD dwMaxRetryCountParam);
	void SetMaxWriteRequest(DWORD dwMaxWriteRequestParam);
	static void SetMaxWriteRequestAll(DWORD dwMaxWriteRequestParam);
	void SetDisableSecureLog(bool bDisable = true);
	void SetDisableUserSupport(bool bDisable = true);
	void GetCertInfo(ECS_CERT_INFO& Rec);
	DWORD GetSecureError(void);
	static void RemoveACLDups(deque<CECSConnection::ACL_ENTRY>& UserAcls);
	bool IfValidMetadataTag(LPCTSTR pszMDString);
	static void GarbageCollect(void);
	bool TestAbort(void);
	static bool ValidateS3BucketName(LPCTSTR pszBucketName);
	void SetHTTPSecurityFlags(DWORD dwHTTPSecurityFlagsParam);

	S3_ERROR SendRequest(LPCTSTR pszMethod, LPCTSTR pszResource, const void *pData, DWORD dwDataLen, CBuffer& RetData, list<HEADER_REQ> *pHeaderReq = nullptr, DWORD dwReceivedDataHint = 0, DWORD dwBufOffset = 0, STREAM_CONTEXT *pStreamSend = nullptr, STREAM_CONTEXT *pStreamReceive = nullptr, ULONGLONG ullTotalLen = 0ULL);

	S3_ERROR Create(LPCTSTR pszPath, const void *pData = nullptr, DWORD dwLen = 0, const list<HEADER_STRUCT> *pMDList = nullptr, const CBuffer *pChecksum = nullptr, STREAM_CONTEXT *pStreamSend = nullptr, ULONGLONG ullTotalLen = 0ULL, LPCTSTR pIfNoneMatch = nullptr, list <HEADER_REQ> *pReq = nullptr);
	S3_ERROR DeleteS3(LPCTSTR pszPath);
	S3_ERROR DeleteS3(const list<S3_DELETE_ENTRY>& PathList);
	S3_ERROR Read(LPCTSTR pszPath, ULONGLONG lwLen, ULONGLONG lwOffset, CBuffer& RetData, DWORD dwBufOffset = 0, STREAM_CONTEXT *pStreamReceive = nullptr, list<HEADER_REQ> *pRcvHeaders = nullptr, ULONGLONG *pullReturnedLength = nullptr);
	S3_ERROR DirListing(LPCTSTR pszPath, DirEntryList_t& DirList, bool bSingle = false, DWORD *pdwGetECSRetention = nullptr);
	S3_ERROR DirListingS3Versions(LPCTSTR pszPath, DirEntryList_t& DirList);
	S3_ERROR S3ServiceInformation(S3_SERVICE_INFO& ServiceInfo);
	void WriteMetadataEntry(list<HEADER_STRUCT>& MDList, LPCTSTR pszTag, const CBuffer& Data);
	void WriteMetadataEntry(list<HEADER_STRUCT>& MDList, LPCTSTR pszTag, const CString& sStr);
	S3_ERROR UpdateMetadata(LPCTSTR pszPath, const list<HEADER_STRUCT>& MDList, const list<CString> *pDeleteTagParam = nullptr);
	S3_ERROR ReadProperties(LPCTSTR pszPath, S3_SYSTEM_METADATA& Properties, LPCTSTR pszVersionId = nullptr, list<HEADER_STRUCT> *pMDList = nullptr, list<HEADER_REQ> *pReq = nullptr);
	S3_ERROR ReadACL(LPCTSTR pszPath, deque<ACL_ENTRY>& Acls, LPCTSTR pszVersion = nullptr);
	S3_ERROR WriteACL(LPCTSTR pszPath, const deque<ACL_ENTRY>& Acls, LPCTSTR pszVersion = nullptr);
	CString GenerateShareableURL(LPCTSTR pszPath, SYSTEMTIME *pstExpire);
	S3_ERROR CreateS3Bucket(LPCTSTR pszBucketName);
	S3_ERROR DeleteS3Bucket(LPCTSTR pszBucketName);
	S3_ERROR S3GetBucketVersioning(LPCTSTR pszBucket, E_S3_VERSIONING& Versioning);
	S3_ERROR S3PutBucketVersioning(LPCTSTR pszBucket, E_S3_VERSIONING Versioning);
	S3_ERROR RenameS3(LPCTSTR pszOldPath, LPCTSTR pszNewPath, LPCTSTR pszVersionId, bool bCopy, const list<CECSConnection::HEADER_STRUCT> *pMDList, const list<CString> *pDeleteTagParam = nullptr);
	S3_ERROR DataNodeEndpointS3(S3_ENDPOINT_INFO& Endpoint);
	S3_ERROR S3GetLifecycle(LPCTSTR pszBucket, S3_LIFECYCLE_INFO& Lifecycle);
	S3_ERROR S3PutLifecycle(LPCTSTR pszBucket, const S3_LIFECYCLE_INFO& Lifecycle);
	S3_ERROR S3DeleteLifecycle(LPCTSTR pszBucket);

	// S3 multipart upload support
	S3_ERROR S3MultiPartInitiate(LPCTSTR pszPath, S3_UPLOAD_PART_INFO& MultiPartInfo, const list<HEADER_STRUCT> *pMDList);
	S3_ERROR S3MultiPartUpload(const S3_UPLOAD_PART_INFO& MultiPartInfo, S3_UPLOAD_PART_ENTRY& PartEntry, STREAM_CONTEXT *pStreamSend, ULONGLONG ullTotalLen, LPCTSTR pszCopySource, ULONGLONG ullStartRange, LPCTSTR pszVersionId);
	S3_ERROR S3MultiPartComplete(const S3_UPLOAD_PART_INFO& MultiPartInfo, const list<shared_ptr<S3_UPLOAD_PART_ENTRY>>& PartList, S3_MPU_COMPLETE_INFO& MPUCompleteInfo);
	S3_ERROR S3MultiPartAbort(const S3_UPLOAD_PART_INFO& MultiPartInfo);
	S3_ERROR S3MultiPartList(LPCTSTR pszBucketName, S3_LIST_MULTIPART_UPLOADS& MultiPartList);

	// ECS Admin functions
	S3_ERROR ECSAdminLogin(LPCTSTR pszUser, LPCTSTR pszPassword);
	S3_ERROR ECSAdminLogout(void);
	S3_ERROR ECSAdminGetUserList(list<S3_ADMIN_USER_INFO>& UserList);
	S3_ERROR ECSAdminCreateUser(S3_ADMIN_USER_INFO& User);
	S3_ERROR ECSAdminGetKeysForUser(LPCTSTR pszUser, LPCTSTR pszNamespace, S3_ADMIN_USER_KEY_INFO& Keys);
	S3_ERROR ECSAdminCreateKeyForUser(S3_ADMIN_USER_INFO& User);

	// ECS metadata search functions
	S3_ERROR S3GetMDSearchFields(S3_METADATA_SEARCH_FIELDS& MDFields);
	S3_ERROR S3GetMDSearchFields(LPCTSTR pszBucket, S3_METADATA_SEARCH_FIELDS_BUCKET& MDFieldBucket);
	S3_ERROR S3SearchMD(const S3_METADATA_SEARCH_PARAMS& Params, S3_METADATA_SEARCH_RESULT& MDSearchResult);

	// DT Query
	S3_ERROR ECSDTQuery(LPCTSTR pszNamespace, LPCTSTR pszBucket, LPCTSTR pszObject, DT_QUERY_RESPONSE& Response);

	static DWORD MaxMetadataBinarySize(void);
};

ECSUTIL_EXT_API extern std::wostream& operator<<(std::wostream& os, const CECSConnection::S3_SYSTEM_METADATA& Rec);

class CECSConnectionAbortBase
{
private:
	const bool *pbAbort;			// pointer to abort bool
	bool bAbortIfTrue;				// determines sense of pbAbort
	CECSConnection *pHost;
	static bool IfShutdownCommon(void *pContext);

protected:
	virtual bool IfShutdown(void) = 0;

public:
	CECSConnectionAbortBase(CECSConnection *pHostParam = nullptr, const bool *pbAbortParam = nullptr, bool bAbortIfTrueParam = true)
	{
		pbAbort = pbAbortParam;
		bAbortIfTrue = bAbortIfTrueParam;
		pHost = pHostParam;
		if (pHost != nullptr)
		{
			if (pbAbort != nullptr)
				pHost->RegisterAbortPtr(pbAbort, bAbortIfTrue);
			pHost->RegisterShutdownCB(&CECSConnectionAbortBase::IfShutdownCommon, this);
		}
	}
	virtual ~CECSConnectionAbortBase()
	{
		if (pHost != nullptr)
		{
			if (pbAbort != nullptr)
				pHost->UnregisterAbortPtr(pbAbort);
			pHost->UnregisterShutdownCB(&CECSConnectionAbortBase::IfShutdownCommon, this);
		}
		pbAbort = nullptr;
		pHost = nullptr;
	}
};

