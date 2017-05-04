// S3Test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <afxsock.h>
#include <list>
#include <deque>
#include "S3Test.h"
#include "ECSUtil.h"
#include "ECSConnection.h"
#include "NTERRTXT.H"

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
_T("   /readmeta <ECSpath>                 Read all metadata from object\n");


const TCHAR * const CMD_OPTION_ENDPOINT = _T("/endpoint");
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
const TCHAR * const CMD_OPTION_HELP1 = _T("--help");
const TCHAR * const CMD_OPTION_HELP2 = _T("-h");
const TCHAR * const CMD_OPTION_HELP3 = _T("/?");

WSADATA WsaData;

CString sEndPoint;
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
bool bHttps = true;
INTERNET_PORT wPort = 9021;

using namespace std;

static int DoTest(CString& sOutMessage);

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

			wPort = (INTERNET_PORT)_tcstoul(*itParam, NULL, 10);
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

			CECSConnection::Init();

			CString sOutMessage;
			list<CString> CmdArgs;
			for (int i = 0; i<argc; i++)
				CmdArgs.push_back(CString(argv[i]));

			if (!ParseArguments(CmdArgs, sOutMessage))
				_tprintf(_T("%s\n"), (LPCTSTR)sOutMessage);
			nRetCode = DoTest(sOutMessage);

			_tprintf(_T("Press ENTER to continue..."));
			cin.get();
		}
    }
    else
    {
        _tprintf(_T("Fatal Error: GetModuleHandle failed\n"));
        nRetCode = 1;
    }

    return nRetCode;
}

static int DoTest(CString& sOutMessage)
{
	CECSConnection Conn;
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
	Conn.SetIPList(IPList);
	Conn.SetS3KeyID(sUser);
	Conn.SetSecret(sSecret);
	Conn.SetSSL(bHttps);
	Conn.SetPort(wPort);
	Conn.SetHost(_T("ECS Test Drive"));

	// get the list of buckets
	CECSConnection::S3_SERVICE_INFO ServiceInfo;
	Error = Conn.S3ServiceInformation(ServiceInfo);
	if (Error.IfError())
	{
		_tprintf(_T("S3ServiceInformation error: %s\n"), (LPCTSTR)Error.Format());
		return 1;
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
		CECSConnection::S3_ERROR Error = S3Read(Conn, sReadLocalPath, sReadECSPath);
		if (Error.IfError())
		{
			_tprintf(_T("Error from S3Read: %s\n"), (LPCTSTR)Error.Format());
		}
	}
	if (!sWriteECSPath.IsEmpty() && !sWriteLocalPath.IsEmpty())
	{
		CECSConnection::S3_ERROR Error = S3Write(Conn, sWriteLocalPath, sWriteECSPath);
		if (Error.IfError())
		{
			_tprintf(_T("Error from S3Write: %s\n"), (LPCTSTR)Error.Format());
		}
	}
	if (!sDeleteECSPath.IsEmpty())
	{
		CECSConnection::S3_ERROR Error = Conn.DeleteS3(sDeleteECSPath);
		if (Error.IfError())
		{
			_tprintf(_T("Delete error: %s\n"), (LPCTSTR)Error.Format());
			return 1;
		}
	}
	if (!sDirPath.IsEmpty())
	{
		CECSConnection::DirEntryList_t DirList;
		CECSConnection::S3_ERROR Error = Conn.DirListing(sDirPath, DirList);
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
		}
	}
	if (!sReadMetaECSPath.IsEmpty())
	{
		list<CECSConnection::S3_METADATA_ENTRY> MDList;
		CECSConnection::S3_ERROR Error = Conn.ReadMetadataBulk(sReadMetaECSPath, MDList, nullptr);
		if (!Error.IfError())
		{
			_tprintf(_T("Metadata for %s:\n"), (LPCTSTR)sReadMetaECSPath);
			for (list<CECSConnection::S3_METADATA_ENTRY>::const_iterator it = MDList.begin(); it != MDList.end(); ++it)
				_tprintf(_T("  %s : %s\n"), (LPCTSTR)it->sTag, (LPCTSTR)it->sData);
		}
		else
		{
			_tprintf(_T("read metadata error: %s\n"), (LPCTSTR)Error.Format());
			return 1;
		}
	}
	return 0;
}
