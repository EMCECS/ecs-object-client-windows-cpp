# ECS Object Client Windows C++

ECS Object Client Windows C++ is a native Windows ECS/S3 client. This library supports the following configuration options:
- x64 or x86
- DLL or static library
- Unicode or Ansi support
- MFC statically linked or MFC DLL

It supports the following features:
- Round-robin load balancing
- Proxy server
- Send/receive throttling
- ECS metadata search
- Multi-part uploads
- S3 Lifecycle rules
- Bulk S3 object delete
- ECS Administrative functions
- Allow abort of long running functions
- Support progress callback for long running functions

## Overview

A connection to ECS is established by creating an instance of CECSConnection.
The CECSConnection object is thread-safe and the same object can be used by
multiple threads, or a new CECSConnection object can be created for each thread.

Here is sample code showing how to initialize a connection:
```C++
   CECSConnection Conn;
   // Initialize library
   ECSInitLib();
   deque<CString> IPList;
   // IPList is set to 1 or more entries containing either an IP address or hostname
   // of an ECS node, or the load balancer for ECS
   // if HTTPS is used, IP addresses are not allowed. The ECS Hostname matching the
   // subject in the SSL certificate must be used.
   IPList.push_back(L"object.ecstestdrive.com");      // use ECS Test Drive
   Conn.SetIPList(IPList);

   // set S3 object user credentials
   Conn.SetS3KeyID(L"user1");
   Conn.SetSecret(L"*********");

   // in this example, we are using HTTPS
   Conn.SetSSL(true);
   // the standard port for S3/SSL is 9021, but ECS Test Drive uses a load balancer that uses port 443
   Conn.SetPort(443);
   // give our connection a name
   Conn.SetHost(_T("ECS Test Drive"));
   // set the user agent that usually identifies the application and version
   Conn.SetUserAgent(_T("TestApp/1.0"));
   // optionally set maximum retries and waiting time between retries
   Conn.SetRetries(10, SECONDS(2), SECONDS(4));
   // optionally set SSL protocols that will be allowed
   Conn.SetHttpsProtocol(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2);
```
## CECSConnection::S3_ERROR

All requests return an error code of type S3_ERROR. The error consists of:
* A WIN32 error code. This is generally a Winhttp error indicating that the HTTP connection failed.
For example: ERROR_WINHTTP_TIMEOUT or ERROR_WINHTTP_CONNECTION_ERROR.
* A HTTP error code. If the TCP/IP connection does make it to the ECS, this
field returns the HTTP error code that was received.
* A S3 error code (enum). Generally, if an HTTP error code is received, an S3 error should be returned
to shed more light on the error.

In addition, there are additional fields that may contain additional information:
* sS3Code: S3 error code (string)
* sS3Resource: Resource associated with the error
* sS3RequestID: Request ID associated with the error
* sDetails: ECS Admin: additional details
## Data Transfer Options
Commands that involve sending or receiving data can specify the data in one of 2 ways:
- An in-memory buffer to hold the data, or to receive the data. This is using the CBuffer class which manages the dynamic
allocation of memory. This can be used for all commands, but only if the amount of data is limited and can easily fit in
memory.
- If the data cannot fit in memory, certain commands can use CECSConnection::STREAM_CONTEXT. This consists of a memory queue
that is used to "feed" the data to ECS, or to receive the data from ECS. A separate thread needs to be created that will feed
or consume the data on the queue. Examples of how this works are in FileSupport.cpp.

## CECSConnection class Reference
### Create
Create or overwrite an object on ECS. Contents can be initialized to either a memory pointer (pData) or a stream. Metadata can be initialized using pMDList.
All headers of the response to the PUT can be returned in pReq;
```C++
	S3_ERROR Create(LPCTSTR pszPath, const void *pData = nullptr, DWORD dwLen = 0, const list<HEADER_STRUCT> *pMDList = nullptr, const CBuffer *pChecksum = nullptr, STREAM_CONTEXT *pStreamSend = nullptr, ULONGLONG ullTotalLen = 0ULL, LPCTSTR pIfNoneMatch = nullptr, list <HEADER_REQ> *pReq = nullptr);
```
### DeleteS3
Delete a single object or multiple objects. A version ID can be specified to delete a non-current version.
```C++
   S3_ERROR DeleteS3(LPCTSTR pszPath, LPCTSTR pszVersionId = nullptr);
	S3_ERROR DeleteS3(const list<S3_DELETE_ENTRY>& PathList);
```
### Read
Receive the data of the specified object. If the size can fit in memory, use RetData to receive the data. Otherwise use pStreamReceive to set up a stream.
```C++
	S3_ERROR Read(LPCTSTR pszPath, ULONGLONG lwLen, ULONGLONG lwOffset, CBuffer& RetData, DWORD dwBufOffset = 0, STREAM_CONTEXT *pStreamReceive = nullptr, list<HEADER_REQ> *pRcvHeaders = nullptr, ULONGLONG *pullReturnedLength = nullptr);
```
### DirListing
Get a folder listing of objects. This uses a delimiter of '/'.
```C++
	S3_ERROR DirListing(LPCTSTR pszPath, DirEntryList_t& DirList, bool bSingle = false, DWORD *pdwGetECSRetention = nullptr, LPCTSTR pszObjName = nullptr);
	S3_ERROR DirListingS3Versions(LPCTSTR pszPath, DirEntryList_t& DirList, LPCTSTR pszObjName = nullptr);
```
### S3ServiceInformation
Get owner information and bucket list.
```C++
	S3_ERROR S3ServiceInformation(S3_SERVICE_INFO& ServiceInfo);
```
### UpdateMetadata
Metadata write.
```C++
	void WriteMetadataEntry(list<HEADER_STRUCT>& MDList, LPCTSTR pszTag, const CBuffer& Data);
	void WriteMetadataEntry(list<HEADER_STRUCT>& MDList, LPCTSTR pszTag, const CString& sStr);
	S3_ERROR UpdateMetadata(LPCTSTR pszPath, const list<HEADER_STRUCT>& MDList, const list<CString> *pDeleteTagParam = nullptr);
```
### ReadProperties
Get system and user metadata for object.
```C++
	S3_ERROR ReadProperties(LPCTSTR pszPath, S3_SYSTEM_METADATA& Properties, LPCTSTR pszVersionId = nullptr, list<HEADER_STRUCT> *pMDList = nullptr, list<HEADER_REQ> *pReq = nullptr);
```
### ReadACL, WriteACL
ACL functions.
```C++
	S3_ERROR ReadACL(LPCTSTR pszPath, deque<ACL_ENTRY>& Acls, LPCTSTR pszVersion = nullptr);
	S3_ERROR WriteACL(LPCTSTR pszPath, const deque<ACL_ENTRY>& Acls, LPCTSTR pszVersion = nullptr);
```
### GenerateShareableURL
Create shareable URL for object.
```C++
	CString GenerateShareableURL(LPCTSTR pszPath, SYSTEMTIME *pstExpire);
```
### Bucket Functions
Bucket Functions.
```C++
	S3_ERROR CreateS3Bucket(LPCTSTR pszBucketName);
	S3_ERROR DeleteS3Bucket(LPCTSTR pszBucketName);
	S3_ERROR S3GetBucketVersioning(LPCTSTR pszBucket, E_S3_VERSIONING& Versioning);
	S3_ERROR S3PutBucketVersioning(LPCTSTR pszBucket, E_S3_VERSIONING Versioning);
```
### RenameS3
Rename simulation. S3 does not support rename, so this function will do a PUT/COPY then delete.
```C++
	S3_ERROR RenameS3(LPCTSTR pszOldPath, LPCTSTR pszNewPath, LPCTSTR pszVersionId, bool bCopy, const list<CECSConnection::HEADER_STRUCT> *pMDList, const list<CString> *pDeleteTagParam = nullptr);
```
### DataNodeEndpointS3
Retrieves the ECS version string, plus the list of IP addresses for all nodes.
```C++
	S3_ERROR DataNodeEndpointS3(S3_ENDPOINT_INFO& Endpoint);
```
### Lifecycle Functions
Lifecycle Functions.
```C++
	S3_ERROR S3GetLifecycle(LPCTSTR pszBucket, S3_LIFECYCLE_INFO& Lifecycle);
	S3_ERROR S3PutLifecycle(LPCTSTR pszBucket, const S3_LIFECYCLE_INFO& Lifecycle);
	S3_ERROR S3DeleteLifecycle(LPCTSTR pszBucket);
```

## License

Copyright 2017 EMC Corporation.  All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
