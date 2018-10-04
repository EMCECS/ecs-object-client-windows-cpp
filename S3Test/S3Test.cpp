/*
 * Copyright (c) 2017, EMC Corporation. All Rights Reserved.
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

// S3Test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <afxsock.h>
#include <list>
#include <deque>
#include "S3Test.h"
#include "ECSGlobal.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// The one and only application object

CWinApp theApp;


const TCHAR * const USAGE =
_T("Usage:\n\n")
_T("   /http                               Don't use SSL\n")
_T("   /https                              Use SSL (default)\n")
_T("   /endpoint <IP or hostname>          S3/ECS Server\n")
_T("   /port <port number>                 Rest API Port\n")
_T("   /user <user ID>                     ECS object user\n")
_T("   /secret <secret>                    ECS object secret\n")
_T("   /list <path, starting with bucket>  Object listing\n")
_T("   /create <localfile> <ECSpath>       Create ECS object and initialize with file\n")
_T("   /delete <ECSpath>                   Delete ECS object\n")
_T("   /read <localfile> <ECSpath>         Read ECS object into file\n")
_T("   /write <localfile> <ECSpath>        Write ECS object from file\n")
_T("   /readmeta <ECSpath>                 Read all metadata from object\n")
_T("   /cert                               Display certificate even if connect successful\n")
_T("   /setcert                            Prompt user to install certificate\n")
_T("   /dtquery <namespace> <bucket> <object> DT Query for object\n")
_T("   /createbucket <bucket>              Create ECS bucket\n")
_T("   /retention <seconds>                Used with /createbucket to set bucket-level retention\n");


const TCHAR * const CMD_OPTION_ENDPOINT = _T("/endpoint");
const TCHAR * const CMD_OPTION_ENDPOINT2 = _T("/endpoint2");
const TCHAR * const CMD_OPTION_ENDPOINT3 = _T("/endpoint3");
const TCHAR * const CMD_OPTION_ENDPOINT4 = _T("/endpoint4");
const TCHAR * const CMD_OPTION_PORT = _T("/port");
const TCHAR * const CMD_OPTION_USER = _T("/user");
const TCHAR * const CMD_OPTION_SECRET = _T("/secret");
const TCHAR * const CMD_OPTION_HTTPS = _T("/https");
const TCHAR * const CMD_OPTION_HTTP = _T("/http");
const TCHAR * const CMD_OPTION_LIST = _T("/list");
const TCHAR * const CMD_OPTION_CREATE = _T("/create");
const TCHAR * const CMD_OPTION_DELETE = _T("/delete");
const TCHAR * const CMD_OPTION_READ = _T("/read");
const TCHAR * const CMD_OPTION_WRITE = _T("/write");
const TCHAR * const CMD_OPTION_READMETA = _T("/readmeta");
const TCHAR * const CMD_OPTION_CERT = _T("/cert");
const TCHAR * const CMD_OPTION_SETCERT = _T("/setcert");
const TCHAR * const CMD_OPTION_DTQUERY = _T("/dtquery");
const TCHAR * const CMD_OPTION_CREATE_BUCKET = _T("/createbucket");
const TCHAR * const CMD_OPTION_RETENTION = _T("/retention");
const TCHAR * const CMD_OPTION_HELP1 = _T("--help");
const TCHAR * const CMD_OPTION_HELP2 = _T("-h");
const TCHAR * const CMD_OPTION_HELP3 = _T("/?");

WSADATA WsaData;

CString sEndPoint, sEndPoint2, sEndPoint3, sEndPoint4;
CString sUser;
CString sSecret;
CString sDirPath;
CString sCreateLocalPath;
CString sCreateECSPath;
CString sReadLocalPath;
CString sReadECSPath;
CString sWriteLocalPath;
CString sWriteECSPath;
CString sReadMetaECSPath;
CString sDeleteECSPath;
CString sDTQueryNamespace;
CString sDTQueryBucket;
CString sDTQueryObject;
CString sCreateBucket;

bool bHttps = true;
bool bCert = false;
bool bSetCert = false;
INTERNET_PORT wPort = 9021;
DWORD dwRetention = 0;					// retention in seconds

bool bShuttingDown = false;

using namespace std;

static int DoTest(CString& sOutMessage);

//
// ConsoleShutdownHandler
// intercepts shutdown and logoff events
// allows the process to cleanly terminate
// used ONLY for win95 and if "not_service" is specified in NT
//
BOOL WINAPI	ConsoleShutdownHandler(
	DWORD dwCtrlType)			// control signal type
{
	switch (dwCtrlType)
	{
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		// cause the process to terminate
		bShuttingDown = true;
		break;
	default:
		break;
	}
	return TRUE;
}

struct PROGRESS
{
	FILETIME ftTime;
	int iProgress;
	PROGRESS()
	{
		ZeroFT(ftTime);
		iProgress = 0;
	}
};
list<PROGRESS> ProgressList;

void ProgressCallback(int iProgress, void *pContext)
{
	static CCriticalSection csProgress;
	static LONGLONG llOffset = 0LL;
	list<PROGRESS> *pList = (list<PROGRESS> *)pContext;
	PROGRESS Rec;
	CSingleLock lock(&csProgress, true);
	GetSystemTimeAsFileTime(&Rec.ftTime);
	Rec.iProgress = iProgress;
	ProgressList.push_back(Rec);
	llOffset += iProgress;
	_tprintf(_T("MPU Offset: %-20I64d\r"), llOffset);
}

static bool ParseArguments(const list<CString>& CmdArgs, CString& sOutMessage)
{
	list<CString>::const_iterator itParam = CmdArgs.begin();
	++itParam;
	for (; itParam != CmdArgs.end(); ++itParam)
	{
		if (itParam->CompareNoCase(CMD_OPTION_ENDPOINT) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sEndPoint = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_ENDPOINT2) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sEndPoint2 = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_ENDPOINT3) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sEndPoint3 = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_ENDPOINT4) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sEndPoint4 = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_USER) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sUser = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_PORT) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end()) 
			{
				sOutMessage = USAGE;
				return false;
			}

			wPort = (INTERNET_PORT)_tcstoul(*itParam, nullptr, 10);
		}
		else if (itParam->CompareNoCase(CMD_OPTION_SECRET) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sSecret = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_HTTPS) == 0)
		{
			bHttps = true;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_HTTP) == 0)
		{
			bHttps = false;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_LIST) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sDirPath = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_CREATE) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sCreateLocalPath = *itParam;
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sCreateECSPath = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_READ) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sReadLocalPath = *itParam;
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sReadECSPath = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_WRITE) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sWriteLocalPath = *itParam;
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sWriteECSPath = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_READMETA) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sReadMetaECSPath = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_CERT) == 0)
		{
			bCert = true;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_SETCERT) == 0)
		{
			bSetCert = true;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_DTQUERY) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sDTQueryNamespace = *itParam;
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sDTQueryBucket = *itParam;
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sDTQueryObject = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_DELETE) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sDeleteECSPath = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_CREATE_BUCKET) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			sCreateBucket = *itParam;
		}
		else if (itParam->CompareNoCase(CMD_OPTION_RETENTION) == 0)
		{
			++itParam;
			if (itParam == CmdArgs.end())
			{
				sOutMessage = USAGE;
				return false;
			}
			dwRetention = _wtol(*itParam);
		}
		else if (itParam->CompareNoCase(CMD_OPTION_HELP1) == 0 ||
			itParam->CompareNoCase(CMD_OPTION_HELP2) == 0 ||
			itParam->CompareNoCase(CMD_OPTION_HELP3) == 0 ||
			itParam->CompareNoCase(_T("/")) == 0)
		{
			sOutMessage = USAGE;
			return false;
		}
		else
		{
			sOutMessage = USAGE;
			return false;
		}
	}

	return true;
}

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	(void)envp;
	int nRetCode = 0;

    HMODULE hModule = ::GetModuleHandle(nullptr);

    if (hModule != nullptr)
    {
        // initialize MFC and print and error on failure
        if (!AfxWinInit(hModule, nullptr, ::GetCommandLine(), 0))
        {
            _tprintf(_T("Fatal Error: MFC initialization failed\n"));
            nRetCode = 1;
        }
        else
        {
			VERIFY(AfxSocketInit(&WsaData));

			ECSInitLib();

			CString sOutMessage;
			list<CString> CmdArgs;
			for (int i = 0; i<argc; i++)
				CmdArgs.push_back(CString(argv[i]));

			if (!ParseArguments(CmdArgs, sOutMessage))
				_tprintf(_T("%s\n"), (LPCTSTR)sOutMessage);
			nRetCode = DoTest(sOutMessage);

			_tprintf(_T("Press ENTER to continue..."));
			cin.get();
			ECSTermLib();
		}
    }
    else
    {
        _tprintf(_T("Fatal Error: GetModuleHandle failed\n"));
        nRetCode = 1;
    }

    return nRetCode;
}

struct PROGRESS_CONTEXT
{
	CString sTitle;
	ULONGLONG ullOffset;
	PROGRESS_CONTEXT()
		: ullOffset(0ULL)
	{}
};

static void ProgressCallBack(int iProgress, void *pContext)
{
	PROGRESS_CONTEXT *pProg = (PROGRESS_CONTEXT *)pContext;
	pProg->ullOffset += iProgress;
	_tprintf(L"%s: %-20I64d\r", (LPCTSTR)pProg->sTitle, pProg->ullOffset);
}

static int DoTest(CString& sOutMessage)
{
	(void)SetConsoleCtrlHandler(ConsoleShutdownHandler, TRUE);

	CECSConnection Conn;
	CECSConnection::ECS_CERT_INFO CertInfo;
	// register an "abort pointer" if bShuttingDown gets set to true, the current request will be aborted
	Conn.RegisterAbortPtr(&bShuttingDown);
	CECSConnection::S3_ERROR Error;
	if (sEndPoint.IsEmpty())
	{
		sOutMessage = _T("Endpoint not defined");
		return 1;
	}
	if (sUser.IsEmpty())
	{
		sOutMessage = _T("User not defined");
		return 1;
	}
	if (sSecret.IsEmpty())
	{
		sOutMessage = _T("Secret not defined");
		return 1;
	}
	deque<CString> IPList;
	IPList.push_back(sEndPoint);
	if (!sEndPoint2.IsEmpty())
		IPList.push_back(sEndPoint2);
	if (!sEndPoint3.IsEmpty())
		IPList.push_back(sEndPoint3);
	if (!sEndPoint4.IsEmpty())
		IPList.push_back(sEndPoint4);
	Conn.SetIPList(IPList);
	Conn.SetS3KeyID(sUser);
	Conn.SetSecret(sSecret);
	Conn.SetSSL(bHttps);
	Conn.SetPort(wPort);
	Conn.SetHost(_T("ECS Test Drive"));
	Conn.SetUserAgent(_T("TestApp/1.0"));
	Conn.SetRetries(10, SECONDS(2), SECONDS(4));
	Conn.SetHttpsProtocol(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2);
//	Conn.SetHTTPSecurityFlags(SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_UNKNOWN_CA);
//	Conn.SetProxy(false, L"127.0.0.1", 8888, nullptr, nullptr);
	Conn.SetTimeouts(10, SECONDS(180), SECONDS(180), SECONDS(180), SECONDS(180), 10);

	// get the list of buckets
	if (bCert || bSetCert)
	{
		Conn.SetSaveCertInfo(true);
	}
	CECSConnection::S3_SERVICE_INFO ServiceInfo;
	Error = Conn.S3ServiceInformation(ServiceInfo);
	if (Error.IfError())
	{
		_tprintf(_T("S3ServiceInformation error: %s\n"), (LPCTSTR)Error.Format());
		if (Error.dwError == ERROR_WINHTTP_SECURE_FAILURE)
		{
			DWORD dwSecureError = Conn.GetSecureError();
			Conn.GetCertInfo(CertInfo);
			_tprintf(_T("Cert Name: %s\n\nCert Subject:\n%s\n\nCert Subject Alternate Names:\n%s\n"),
				(LPCTSTR)CertInfo.sCertName, (LPCTSTR)CertInfo.sCertSubject, (LPCTSTR)CertInfo.sCertSubjectAltName);
			if ((dwSecureError & WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA) != 0)
			{
				wchar_t InArray[10];
				size_t SizeRead = 0;
				_tprintf(L"Install Certificate?\n");
				errno_t err = _cgetws_s(InArray, &SizeRead);
				CString sInArray(InArray);
				sInArray.MakeLower();
				if (sInArray.Left(1) == L"y")
				{
					DWORD dwErr = CECSConnection::SetRootCertificate(CertInfo);
					if (dwErr == ERROR_SUCCESS)
						_tprintf(L"Certificate installed\n");
					else
						_tprintf(L"Error installing certificate: %d\n", dwErr);
				}
			}
		}
		return 1;
	}
	// check if we should dump the certificate info
	if (bCert || bSetCert)
	{
		Conn.SetSaveCertInfo(false);
		Conn.GetCertInfo(CertInfo);
		_tprintf(_T("Cert Name:\n%s\n\nCert Subject:\n%s\n\nCert Subject Alternate Names:\n%s\n"),
			(LPCTSTR)CertInfo.sCertName, (LPCTSTR)CertInfo.sCertSubject, (LPCTSTR)CertInfo.sCertSubjectAltName);
		if (bSetCert)
		{
			wchar_t InArray[10];
			size_t SizeRead = 0;
			_tprintf(L"Install Certificate?\n");
			errno_t err = _cgetws_s(InArray, &SizeRead);
			CString sInArray(InArray);
			sInArray.MakeLower();
			if (sInArray.Left(1) == L"y")
			{
				DWORD dwErr = CECSConnection::SetRootCertificate(CertInfo);
				if (dwErr == ERROR_SUCCESS)
					_tprintf(L"Certificate installed\n");
				else
					_tprintf(L"Error installing certificate: %s\n", (LPCTSTR)GetNTErrorText(dwErr));
			}
		}
	}
	// dump service info
	_tprintf(_T("OwnerID: %s, Name: %s\n"), (LPCTSTR)ServiceInfo.sOwnerID, (LPCTSTR)ServiceInfo.sOwnerDisplayName);
	for (list<CECSConnection::S3_BUCKET_INFO>::const_iterator itList = ServiceInfo.BucketList.begin();
		itList != ServiceInfo.BucketList.end(); ++itList)
	{
		_tprintf(_T("  Bucket: %s: %s\n"), (LPCTSTR)itList->sName, (LPCTSTR)DateTimeStr(&itList->ftCreationDate, true, true, true, false, true));
	}
	// get the endpoint list
	CECSConnection::S3_ENDPOINT_INFO Endpoint;
	Error = Conn.DataNodeEndpointS3(Endpoint);
	if (Error.IfError())
	{
		_tprintf(_T("DataNodeEndpointS3 error: %s\n"), (LPCTSTR)Error.Format());
		return 1;
	}
	// dump endpoint info
	_tprintf(_T("Version: %s\n"), (LPCTSTR)Endpoint.sVersion);
	for (list<CString>::const_iterator itList = Endpoint.EndpointList.begin();
		itList != Endpoint.EndpointList.end(); ++itList)
	{
		_tprintf(_T("  Endpoint: %s\n"), (LPCTSTR)*itList);
	}

	// create a bucket?
	if (!sCreateBucket.IsEmpty())
	{
		CECSConnection::S3_BUCKET_OPTIONS BucketOptions;
		if (dwRetention != 0)
			BucketOptions.dwRetention = dwRetention;
		Error = Conn.CreateS3Bucket(sCreateBucket, &BucketOptions);
		if (Error.IfError())
		{
			_tprintf(_T("CreateS3Bucket error: %s\n"), (LPCTSTR)Error.Format());
			return 1;
		}
		_tprintf(_T("CreateS3Bucket: %s created, retention: %u seconds\n"), (LPCTSTR)sCreateBucket, dwRetention);
	}
	if (!sCreateECSPath.IsEmpty() && !sCreateLocalPath.IsEmpty())
	{
		CHandle hFile(CreateFile(sCreateLocalPath, FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (hFile.m_h == INVALID_HANDLE_VALUE)
		{
			_tprintf(_T("Open error for %s: %s\n"), (LPCTSTR)sCreateLocalPath, (LPCTSTR)GetNTLastErrorText());
			return 1;
		}
		LARGE_INTEGER FileSize;
		if (!GetFileSizeEx(hFile, &FileSize))
		{
			_tprintf(_T("Get size error for %s: %s\n"), (LPCTSTR)sCreateLocalPath, (LPCTSTR)GetNTLastErrorText());
			return 1;
		}
		CBuffer Buf;
		DWORD dwNumRead;
		Buf.SetBufSize((DWORD)FileSize.QuadPart);
		if (!ReadFile(hFile, Buf.GetData(), Buf.GetBufSize(), &dwNumRead, nullptr))
		{
			_tprintf(_T("read error for %s: %s\n"), (LPCTSTR)sCreateLocalPath, (LPCTSTR)GetNTLastErrorText());
			return 1;
		}
		CECSConnection::S3_ERROR Error = Conn.Create(sCreateECSPath, Buf.GetData(), Buf.GetBufSize());
		if (Error.IfError())
		{
			_tprintf(_T("Create error: %s\n"), (LPCTSTR)Error.Format());
			return 1;
		}
	}
	if (!sReadECSPath.IsEmpty() && !sReadLocalPath.IsEmpty())
	{
		PROGRESS_CONTEXT Context;
		Context.sTitle = L"Read";
		CECSConnection::S3_ERROR Error = S3Read(sReadLocalPath, STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, true, Conn, sReadECSPath, 0ULL, 0ULL, nullptr, ProgressCallBack, &Context, nullptr);
		if (Error.IfError())
		{
			_tprintf(_T("Error from S3Read: %s\n"), (LPCTSTR)Error.Format());
		}
		_tprintf(L"\r\n");
	}
	if (!sWriteECSPath.IsEmpty() && !sWriteLocalPath.IsEmpty())
	{
		PROGRESS_CONTEXT Context;
		Context.sTitle = L"Write";
		list<CECSConnection::HEADER_STRUCT> MDList;
		CECSConnection::HEADER_STRUCT MD_Rec;
#ifndef unused
		MD_Rec.sHeader = _T("x-amz-meta-NewTag");
		MD_Rec.sContents = _T("NewTagValue");
		MDList.push_back(MD_Rec);
		CECSConnection::S3_ERROR Error = S3Write(sWriteLocalPath, STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL, Conn, sWriteECSPath, MEGABYTES(1), true, 20, &MDList, ProgressCallBack, &Context);
		if (Error.IfError())
		{
			_tprintf(_T("Error from S3Write: %s\n"), (LPCTSTR)Error.Format());
		}
		_tprintf(L"\r\n");
#else
		MD_Rec.sHeader = _T("x-amz-meta-NewTag");
		MD_Rec.sContents = _T("NewTagValueMPU");
		MDList.push_back(MD_Rec);
		CECSConnection::S3_ERROR Error;
		_tprintf(L"\nMPU Upload:\n");
		bool bMPUUpload = DoS3MultiPartUpload(
			sWriteLocalPath,
			STGM_READ | STGM_SHARE_DENY_WRITE,
			FILE_ATTRIBUTE_NORMAL,
			Conn,						// established connection to ECS
			sWriteECSPath,				// path to object in format: /bucket/dir1/dir2/object
			MEGABYTES(1),				// size of buffer to use
			10,							// part size (in MB)
			3,							// maxiumum number of threads to spawn
			true,						// if set, include content-MD5 header
			&MDList,					// optional metadata to send to object
			4,							// how big the queue can grow that feeds the upload thread
			5,							// how many times to retry a part before giving up
			ProgressCallBack,			// optional progress callback
			&Context,					// context for UpdateProgressCB
			Error);						// returned error
		_tprintf(L"\nMPU Upload: %s, %s\n", bMPUUpload ? L"success" : L"fail", (LPCTSTR)Error.Format(true));
#endif
	}
	if (!sDeleteECSPath.IsEmpty())
	{
		CECSConnection::S3_ERROR Error = Conn.DeleteS3(sDeleteECSPath);
		if (Error.IfError())
		{
			_tprintf(_T("Delete error: %s\n"), (LPCTSTR)Error.Format());
			return 1;
		}
		_tprintf(_T("Deleted: %s\n"), (LPCTSTR)sDeleteECSPath);
	}
	if (!sDirPath.IsEmpty())
	{
		CECSConnection::DirEntryList_t DirList;
//		CECSConnection::S3_ERROR Error = Conn.DirListing(sDirPath, DirList);
		CECSConnection::S3_ERROR Error = Conn.DirListingS3Versions(sDirPath, DirList);
		if (Error.IfError())
		{
			_tprintf(_T("listing error: %s\n"), (LPCTSTR)Error.Format());
			return 1;
		}
		for (CECSConnection::DirEntryList_t::const_iterator itList = DirList.begin(); itList != DirList.end(); ++itList)
		{
			_tprintf(_T("%-28s %-24s %I64d\n"), (LPCTSTR)(itList->sName + (itList->bDir ? _T("/ ") : _T(""))),
				(LPCTSTR)DateTimeStr(&itList->Properties.ftLastMod, true, true, true, false, true),
				itList->Properties.llSize);
			_tprintf(_T("  LastMod: %s\n  Size: %lld\n  IsLatest: %d\n  Deleted: %d\n  VersionId: %s\n  ETag: %s\n  Owner: %s\n  OwnerID: %s\n"),
				(LPCTSTR)DateTimeStr(&itList->Properties.ftLastMod, true, true, true, false, true, true),
				itList->Properties.llSize, (int)itList->Properties.bIsLatest, (int)itList->Properties.bDeleted, (LPCTSTR)itList->Properties.sVersionId,
				(LPCTSTR)itList->Properties.sETag, (LPCTSTR)itList->Properties.sOwnerDisplayName, (LPCTSTR)itList->Properties.sOwnerID);
		}
	}
	if (!sReadMetaECSPath.IsEmpty())
	{
		list<CECSConnection::HEADER_STRUCT> MDList;
		CECSConnection::S3_SYSTEM_METADATA Properties;
		CECSConnection::S3_ERROR Error = Conn.ReadProperties(sReadMetaECSPath, Properties, nullptr, &MDList);
		if (!Error.IfError())
		{
			_tprintf(_T("Properties for %s:\n"), (LPCTSTR)sReadMetaECSPath);
			_tprintf(_T("  LastMod: %s\n  Size: %lld\n  IsLatest: %d\n  Deleted: %d\n  VersionId: %s\n  ETag: %s\n  Owner: %s\n  OwnerID: %s\n"),
				(LPCTSTR)DateTimeStr(&Properties.ftLastMod, true, true, true, false, true, true),
				Properties.llSize, (int)Properties.bIsLatest, (int)Properties.bDeleted, (LPCTSTR)Properties.sVersionId,
				(LPCTSTR)Properties.sETag, (LPCTSTR)Properties.sOwnerDisplayName, (LPCTSTR)Properties.sOwnerID);
			for (list<CECSConnection::HEADER_STRUCT>::const_iterator it = MDList.begin(); it != MDList.end(); ++it)
				_tprintf(_T("    %s : %s\n"), (LPCTSTR)it->sHeader, (LPCTSTR)it->sContents);
		}
		else
		{
			_tprintf(_T("read metadata error: %s\n"), (LPCTSTR)Error.Format());
			return 1;
		}
	}
	if (!sDTQueryNamespace.IsEmpty() && !sDTQueryBucket.IsEmpty() && !sDTQueryObject.IsEmpty())
	{
		CECSConnection::DT_QUERY_RESPONSE Response;
		CECSConnection::S3_ERROR Error = Conn.ECSDTQuery(sDTQueryNamespace, sDTQueryBucket, sDTQueryObject, false, nullptr, Response);
		if (Error.IfError())
		{
			_tprintf(_T("DT Query error: %s\n"), (LPCTSTR)Error.Format());
			return 1;
		}
		_tprintf(_T("status = %s\nTotalDataSize = %I64d\nShippedDataSize = %I64d\nShippedDataPercentage = %d\n"),
			Response.bStatus ? _T("true") : _T("false"), Response.ullTotalDataSize, Response.ullShippedDataSize,
			Response.uShippedDataPercentage);
	}
	CString sBadIPMap(Conn.DumpBadIPMap());
	_tprintf(_T("\nBad IP Map:\n%s\n"), (LPCTSTR)sBadIPMap);
	return 0;
}
