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
L"Usage:\n\n"
L"   /http                               Don't use SSL\n"
L"   /https                              Use SSL (default)\n"
L"   /endpoint <IP or hostname>          S3/ECS Server\n"
L"   /user <user ID>                     ECS object user\n"
L"   /secret <secret>                    ECS object secret\n"
L"   /list <path, starting with bucket>  Object listing\n"
L"   /create <localfile> <ECSpath>       Create ECS object and initialize with file\n"
L"   /delete <ECSpath>                   Delete ECS object\n"
L"   /read <localfile> <ECSpath>         Read ECS object into file\n"
L"   /readmeta <ECSpath>                 Read all metadata from object\n";


const TCHAR * const CMD_OPTION_ENDPOINT = L"/endpoint";
const TCHAR * const CMD_OPTION_USER = L"/user";
const TCHAR * const CMD_OPTION_SECRET = L"/secret";
const TCHAR * const CMD_OPTION_HTTPS = L"/https";
const TCHAR * const CMD_OPTION_HTTP = L"/http";
const TCHAR * const CMD_OPTION_LIST = L"/list";
const TCHAR * const CMD_OPTION_CREATE = L"/create";
const TCHAR * const CMD_OPTION_DELETE = L"/delete";
const TCHAR * const CMD_OPTION_READ = L"/read";
const TCHAR * const CMD_OPTION_READMETA = L"/readmeta";
const TCHAR * const CMD_OPTION_HELP1 = L"--help";
const TCHAR * const CMD_OPTION_HELP2 = L"-h";
const TCHAR * const CMD_OPTION_HELP3 = L"/?";

WSADATA WsaData;

CString sEndPoint;
CString sUser;
CString sSecret;
CString sDirPath;
CString sCreateLocalPath;
CString sCreateECSPath;
CString sReadLocalPath;
CString sReadECSPath;
CString sReadMetaECSPath;
CString sDeleteECSPath;
bool bHttps = true;


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
			itParam->CompareNoCase(L"/") == 0)
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
            wprintf(L"Fatal Error: MFC initialization failed\n");
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
		}
    }
    else
    {
        wprintf(L"Fatal Error: GetModuleHandle failed\n");
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
		sOutMessage = L"Endpoint not defined";
		return 1;
	}
	if (sUser.IsEmpty())
	{
		sOutMessage = L"User not defined";
		return 1;
	}
	if (sSecret.IsEmpty())
	{
		sOutMessage = L"Secret not defined";
		return 1;
	}
	deque<CString> IPList;
	IPList.push_back(sEndPoint);
	Conn.SetIPList(IPList);
	Conn.SetS3KeyID(sUser);
	Conn.SetSecret(sSecret);
	Conn.SetSSL(bHttps);
	Conn.SetHost(L"ECS Test Drive");

	// get the list of buckets
	CECSConnection::S3_SERVICE_INFO ServiceInfo;
	Error = Conn.S3ServiceInformation(ServiceInfo);
	if (Error.IfError())
	{
		_tprintf(L"S3ServiceInformation error: %s\n", (LPCTSTR)Error.Format());
		return 1;
	}
	// dump service info
	_tprintf(L"OwnerID: %s, Name: %s\n", (LPCTSTR)ServiceInfo.sOwnerID, (LPCTSTR)ServiceInfo.sOwnerDisplayName);
	for (list<CECSConnection::S3_BUCKET_INFO>::const_iterator itList = ServiceInfo.BucketList.begin();
		itList != ServiceInfo.BucketList.end(); ++itList)
	{
		_tprintf(L"  Bucket: %s: %s\n", (LPCTSTR)itList->sName, (LPCTSTR)DateTimeStr(&itList->ftCreationDate, true, true, true, false, true));
	}
	// get the endpoint list
	CECSConnection::S3_ENDPOINT_INFO Endpoint;
	Error = Conn.DataNodeEndpointS3(Endpoint);
	if (Error.IfError())
	{
		_tprintf(L"DataNodeEndpointS3 error: %s\n", (LPCTSTR)Error.Format());
		return 1;
	}
	// dump endpoint info
	_tprintf(L"Version: %s\n", (LPCTSTR)Endpoint.sVersion);
	for (list<CString>::const_iterator itList = Endpoint.EndpointList.begin();
		itList != Endpoint.EndpointList.end(); ++itList)
	{
		_tprintf(L"  Endpoint: %s\n", (LPCTSTR)*itList);
	}

	if (!sCreateECSPath.IsEmpty() && !sCreateLocalPath.IsEmpty())
	{
		CHandle hFile(CreateFile(sCreateLocalPath, FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (hFile.m_h == INVALID_HANDLE_VALUE)
		{
			_tprintf(L"Open error for %s: %s\n", (LPCTSTR)sCreateLocalPath, (LPCTSTR)GetNTLastErrorText());
			return 1;
		}
		LARGE_INTEGER FileSize;
		if (!GetFileSizeEx(hFile, &FileSize))
		{
			_tprintf(L"Get size error for %s: %s\n", (LPCTSTR)sCreateLocalPath, (LPCTSTR)GetNTLastErrorText());
			return 1;
		}
		CBuffer Buf;
		DWORD dwNumRead;
		Buf.SetBufSize((DWORD)FileSize.QuadPart);
		if (!ReadFile(hFile, Buf.GetData(), Buf.GetBufSize(), &dwNumRead, nullptr))
		{
			_tprintf(L"read error for %s: %s\n", (LPCTSTR)sCreateLocalPath, (LPCTSTR)GetNTLastErrorText());
			return 1;
		}
		CECSConnection::S3_ERROR Error = Conn.Create(sCreateECSPath, Buf.GetData(), Buf.GetBufSize());
		if (Error.IfError())
		{
			_tprintf(L"Create error: %s\n", (LPCTSTR)Error.Format());
			return 1;
		}
	}
	if (!sReadECSPath.IsEmpty() && !sReadLocalPath.IsEmpty())
	{
		CECSConnection::S3_ERROR Error = S3Read(Conn, sReadLocalPath, sReadECSPath);
		if (Error.IfError())
		{
			_tprintf(L"Error from S3Read: %s\n", (LPCTSTR)Error.Format());
		}
	}
	if (!sDeleteECSPath.IsEmpty())
	{
		CECSConnection::S3_ERROR Error = Conn.DeleteS3(sDeleteECSPath);
		if (Error.IfError())
		{
			_tprintf(L"Delete error: %s\n", (LPCTSTR)Error.Format());
			return 1;
		}
	}
	if (!sDirPath.IsEmpty())
	{
		CECSConnection::DirEntryList_t DirList;
		CECSConnection::S3_ERROR Error = Conn.DirListing(sDirPath, DirList);
		if (Error.IfError())
		{
			_tprintf(L"listing error: %s\n", (LPCTSTR)Error.Format());
			return 1;
		}
		for (CECSConnection::DirEntryList_t::const_iterator itList = DirList.begin(); itList != DirList.end(); ++itList)
		{
			_tprintf(L"%-28s %-24s %I64d\n", (LPCTSTR)(itList->sName + (itList->bDir ? L"/ " : L"")),
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
			_tprintf(L"Metadata for %s:\n", (LPCTSTR)sReadMetaECSPath);
			for (list<CECSConnection::S3_METADATA_ENTRY>::const_iterator it = MDList.begin(); it != MDList.end(); ++it)
				_tprintf(L"  %s : %s\n", (LPCTSTR)it->sTag, (LPCTSTR)it->sData);
		}
		else
		{
			_tprintf(L"read metadata error: %s\n", (LPCTSTR)Error.Format());
			return 1;
		}
	}
	return 0;
}
