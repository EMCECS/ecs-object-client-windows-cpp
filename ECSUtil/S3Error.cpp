/*
 * Copyright (c) 2017 - 2019, Dell Technologies, Inc. All Rights Reserved.
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

#include "stdafx.h"

using namespace std;

#include "generic_defs.h"
#include "S3Error.h"
#include "CRWLock.h"
#include <set>


struct S3_ERROR_TRANS_INIT
{
	E_S3_ERROR_TYPE S3Error;
	LPCTSTR pszErrorID;
	LPCTSTR pszErrorText;
};

struct S3_ERROR_TRANS
{
	E_S3_ERROR_TYPE S3Error;
	CString sErrorID;
	CString sErrorText;
	S3_ERROR_TRANS()
		: S3Error(S3_ERROR_SUCCESS)
	{}
	S3_ERROR_TRANS(const S3_ERROR_TRANS_INIT& Rec)
	{
		S3Error = Rec.S3Error;
		sErrorID = Rec.pszErrorID;
		sErrorText = Rec.pszErrorText;
	}
};


static S3_ERROR_TRANS_INIT S3ErrorTransTable[] = {
	{ S3_ERROR_AccessDenied, _T("AccessDenied"), _T("Access Denied") },
	{ S3_ERROR_AccountProblem, _T("AccountProblem"), _T("There is a problem with your AWS account that prevents the operation from completing successfully. Please use Contact Us.") },
	{ S3_ERROR_AmbiguousGrantByEmailAddress, _T("AmbiguousGrantByEmailAddress"), _T("The email address you provided is associated with more than one account.") },
	{ S3_ERROR_BadDigest, _T("BadDigest"), _T("The Content-MD5 you specified did not match what we received.") },
	{ S3_ERROR_BucketAlreadyExists, _T("BucketAlreadyExists"), _T("The requested bucket name is not available. The bucket namespace is shared by all users of the system. Please select a different name and try again.") },
	{ S3_ERROR_BucketAlreadyOwnedByYou, _T("BucketAlreadyOwnedByYou"), _T("Your previous request to create the named bucket succeeded and you already own it. You get this error in all AWS regions except US Standard, us-east-1. In us-east-1 region, you will get 200 OK, but it is no-op (if bucket exists it Amazon S3 will not do anything).") },
	{ S3_ERROR_BucketNotEmpty, _T("BucketNotEmpty"), _T("The bucket you tried to delete is not empty.") },
	{ S3_ERROR_CredentialsNotSupported, _T("CredentialsNotSupported"), _T("This request does not support credentials.") },
	{ S3_ERROR_CrossLocationLoggingProhibited, _T("CrossLocationLoggingProhibited"), _T("Cross-location logging not allowed. Buckets in one geographic location cannot log information to a bucket in another location.") },
	{ S3_ERROR_EntityTooSmall, _T("EntityTooSmall"), _T("Your proposed upload is smaller than the minimum allowed object size.") },
	{ S3_ERROR_EntityTooLarge, _T("EntityTooLarge"), _T("Your proposed upload exceeds the maximum allowed object size.") },
	{ S3_ERROR_ExpiredToken, _T("ExpiredToken"), _T("The provided token has expired.") },
	{ S3_ERROR_IllegalVersioningConfigurationException, _T("IllegalVersioningConfigurationException"), _T("Indicates that the versioning configuration specified in the request is invalid.") },
	{ S3_ERROR_IncompleteBody, _T("IncompleteBody"), _T("You did not provide the number of bytes specified by the Content-Length HTTP header") },
	{ S3_ERROR_IncorrectNumberOfFilesInPostRequest, _T("IncorrectNumberOfFilesInPostRequest"), _T("POST requires exactly one file upload per request.") },
	{ S3_ERROR_InlineDataTooLarge, _T("InlineDataTooLarge"), _T("Inline data exceeds the maximum allowed size.") },
	{ S3_ERROR_InternalError, _T("InternalError"), _T("We encountered an internal error. Please try again.") },
	{ S3_ERROR_InvalidAccessKeyId, _T("InvalidAccessKeyId"), _T("The AWS access key Id you provided does not exist in our records.") },
	{ S3_ERROR_InvalidAddressingHeader, _T("InvalidAddressingHeader"), _T("You must specify the Anonymous role.") },
	{ S3_ERROR_InvalidArgument, _T("InvalidArgument"), _T("Invalid Argument") },
	{ S3_ERROR_InvalidBucketName, _T("InvalidBucketName"), _T("The specified bucket is not valid.") },
	{ S3_ERROR_InvalidBucketState, _T("InvalidBucketState"), _T("The request is not valid with the current state of the bucket.") },
	{ S3_ERROR_InvalidDigest, _T("InvalidDigest"), _T("The Content-MD5 you specified is not valid.") },
	{ S3_ERROR_InvalidEncryptionAlgorithmError, _T("InvalidEncryptionAlgorithmError"), _T("The encryption request you specified is not valid. The valid value is AES256.") },
	{ S3_ERROR_InvalidLocationConstraint, _T("InvalidLocationConstraint"), _T("The specified location constraint is not valid. For more information about regions, see How to Select a Region for Your Buckets.") },
	{ S3_ERROR_InvalidObjectState, _T("InvalidObjectState"), _T("The operation is not valid for the current state of the object.") },
	{ S3_ERROR_InvalidPart, _T("InvalidPart"), _T("One or more of the specified parts could not be found. The part might not have been uploaded, or the specified entity tag might not have matched the part's entity tag.") },
	{ S3_ERROR_InvalidPartOrder, _T("InvalidPartOrder"), _T("The list of parts was not in ascending order.Parts list must specified in order by part number.") },
	{ S3_ERROR_InvalidPayer, _T("InvalidPayer"), _T("All access to this object has been disabled.") },
	{ S3_ERROR_InvalidPolicyDocument, _T("InvalidPolicyDocument"), _T("The content of the form does not meet the conditions specified in the policy document.") },
	{ S3_ERROR_InvalidRange, _T("InvalidRange"), _T("The requested range cannot be satisfied.") },
	{ S3_ERROR_InvalidRequest, _T("InvalidRequest"), _T("SOAP requests must be made over an HTTPS connection.") },
	{ S3_ERROR_InvalidSecurity, _T("InvalidSecurity"), _T("The provided security credentials are not valid.") },
	{ S3_ERROR_InvalidSOAPRequest, _T("InvalidSOAPRequest"), _T("The SOAP request body is invalid.") },
	{ S3_ERROR_InvalidStorageClass, _T("InvalidStorageClass"), _T("The storage class you specified is not valid.") },
	{ S3_ERROR_InvalidTargetBucketForLogging, _T("InvalidTargetBucketForLogging"), _T("The target bucket for logging does not exist, is not owned by you, or does not have the appropriate grants for the log-delivery group.") },
	{ S3_ERROR_InvalidToken, _T("InvalidToken"), _T("The provided token is malformed or otherwise invalid.") },
	{ S3_ERROR_InvalidURI, _T("InvalidURI"), _T("Couldn't parse the specified URI.") },
	{ S3_ERROR_KeyTooLong, _T("KeyTooLong"), _T("Your key is too long.") },
	{ S3_ERROR_MalformedACLError, _T("MalformedACLError"), _T("The XML you provided was not well-formed or did not validate against our published schema.") },
	{ S3_ERROR_MalformedPOSTRequest, _T("MalformedPOSTRequest"), _T("The body of your POST request is not well-formed multipart/form-data.") },
	{ S3_ERROR_MalformedXML, _T("MalformedXML"), _T("This happens when the user sends malformed xml (xml that doesn't conform to the published xsd) for the configuration. The error message is, \"The XML you provided was not well - formed or did not validate against our published schema.\"") },
	{ S3_ERROR_MaxMessageLengthExceeded, _T("MaxMessageLengthExceeded"), _T("Your request was too big.") },
	{ S3_ERROR_MaxPostPreDataLengthExceededError, _T("MaxPostPreDataLengthExceededError"), _T("Your POST request fields preceding the upload file were too large.") },
	{ S3_ERROR_MetadataTooLarge, _T("MetadataTooLarge"), _T("Your metadata headers exceed the maximum allowed metadata size.") },
	{ S3_ERROR_MethodNotAllowed, _T("MethodNotAllowed"), _T("The specified method is not allowed against this resource.") },
	{ S3_ERROR_MissingAttachment, _T("MissingAttachment"), _T("A SOAP attachment was expected, but none were found.") },
	{ S3_ERROR_MissingContentLength, _T("MissingContentLength"), _T("You must provide the Content-Length HTTP header.") },
	{ S3_ERROR_MissingRequestBodyError, _T("MissingRequestBodyError"), _T("This happens when the user sends an empty xml document as a request. The error message is, \"Request body is empty.\"") },
	{ S3_ERROR_MissingSecurityElement, _T("MissingSecurityElement"), _T("The SOAP 1.1 request is missing a security element.") },
	{ S3_ERROR_MissingSecurityHeader, _T("MissingSecurityHeader"), _T("Your request is missing a required header.") },
	{ S3_ERROR_NoLoggingStatusForKey, _T("NoLoggingStatusForKey"), _T("There is no such thing as a logging status subresource for a key.") },
	{ S3_ERROR_NoSuchBucket, _T("NoSuchBucket"), _T("The specified bucket does not exist.") },
	{ S3_ERROR_NoSuchKey, _T("NoSuchKey"), _T("The specified key does not exist.") },
	{ S3_ERROR_NoSuchLifecycleConfiguration, _T("NoSuchLifecycleConfiguration"), _T("The lifecycle configuration does not exist.") },
	{ S3_ERROR_NoSuchUpload, _T("NoSuchUpload"), _T("The specified multipart upload does not exist. The upload ID might be invalid, or the multipart upload might have been aborted or completed.") },
	{ S3_ERROR_NoSuchVersion, _T("NoSuchVersion"), _T("Indicates that the version ID specified in the request does not match an existing version.") },
	{ S3_ERROR_NotImplemented, _T("NotImplemented"), _T("A header you provided implies functionality that is not implemented.") },
	{ S3_ERROR_NotSignedUp, _T("NotSignedUp"), _T("Your account is not signed up for the Amazon S3 service. You must sign up before you can use Amazon S3. You can sign up at the following URL: http://aws.amazon.com/s3") },
	{ S3_ERROR_NotSuchBucketPolicy, _T("NotSuchBucketPolicy"), _T("The specified bucket does not have a bucket policy.") },
	{ S3_ERROR_OperationAborted, _T("OperationAborted"), _T("A conflicting conditional operation is currently in progress against this resource. Try again.") },
	{ S3_ERROR_PermanentRedirect, _T("PermanentRedirect"), _T("The bucket you are attempting to access must be addressed using the specified endpoint. Send all future requests to this endpoint.") },
	{ S3_ERROR_PreconditionFailed, _T("PreconditionFailed"), _T("At least one of the preconditions you specified did not hold.") },
	{ S3_ERROR_Redirect, _T("Redirect"), _T("Temporary redirect.") },
	{ S3_ERROR_RestoreAlreadyInProgress, _T("RestoreAlreadyInProgress"), _T("Object restore is already in progress.") },
	{ S3_ERROR_RequestIsNotMultiPartContent, _T("RequestIsNotMultiPartContent"), _T("Bucket POST must be of the enclosure-type multipart/form-data.") },
	{ S3_ERROR_RequestTimeout, _T("RequestTimeout"), _T("Your socket connection to the server was not read from or written to within the timeout period.") },
	{ S3_ERROR_RequestTimeTooSkewed, _T("RequestTimeTooSkewed"), _T("The difference between the request time and the server's time is too large.") },
	{ S3_ERROR_RequestTorrentOfBucketError, _T("RequestTorrentOfBucketError"), _T("Requesting the torrent file of a bucket is not permitted.") },
	{ S3_ERROR_SignatureDoesNotMatch, _T("SignatureDoesNotMatch"), _T("The request signature we calculated does not match the signature you provided. Check your AWS secret access key and signing method. For more information, see REST Authentication and SOAP Authentication for details.") },
	{ S3_ERROR_ServiceUnavailable, _T("ServiceUnavailable"), _T("Reduce your request rate.") },
	{ S3_ERROR_SlowDown, _T("SlowDown"), _T("Reduce your request rate.") },
	{ S3_ERROR_TemporaryRedirect, _T("TemporaryRedirect"), _T("You are being redirected to the bucket while DNS updates.") },
	{ S3_ERROR_TokenRefreshRequired, _T("TokenRefreshRequired"), _T("The provided token must be refreshed.") },
	{ S3_ERROR_TooManyBuckets, _T("TooManyBuckets"), _T("You have attempted to create more buckets than allowed.") },
	{ S3_ERROR_UnexpectedContent, _T("UnexpectedContent"), _T("This request does not support content.") },
	{ S3_ERROR_UnresolvableGrantByEmailAddress, _T("UnresolvableGrantByEmailAddress"), _T("The email address you provided does not match any account on record.") },
	{ S3_ERROR_UserKeyMustBeSpecified, _T("UserKeyMustBeSpecified"), _T("The bucket POST must contain the specified field name. If it is specified, check the order of the fields.") },
	{ S3_ERROR_ObjectUnderRetention, _T("ObjectUnderRetention"), _T("The object is under retention and can't be deleted or modified.") },
	{ S3_ERROR_MetadataSearchNotEnabled, _T("Metadata search not enabled"), _T("Metadata search is not enabled for this bucket.") },
};

CSimpleRWLock rwlS3Translate;
static bool bMapsInitialized = false;
static map<CStringSortNoCase, shared_ptr<S3_ERROR_TRANS>> IDMap;		// indexed by error ID
static map<E_S3_ERROR_TYPE, shared_ptr<S3_ERROR_TRANS>> CodeMap;		// indexed by error code

// PopulateMaps
// the first time this is run, this routine will take the initial data and populate the maps for fast access
static void PopulateMaps()
{
	CSimpleRWLockAcquire lock(&rwlS3Translate, true);			// write lock

	if (bMapsInitialized)										// oops - it must have just gotten initialized by another thread
		return;
	shared_ptr<S3_ERROR_TRANS> Rec;
	for (UINT i = 0; i < _countof(S3ErrorTransTable); i++)
	{
		Rec.reset(new S3_ERROR_TRANS(S3ErrorTransTable[i]));

		pair<map<CStringSortNoCase, shared_ptr<S3_ERROR_TRANS>>::iterator, bool> ret1 = IDMap.insert(make_pair(CStringSortNoCase(S3ErrorTransTable[i].pszErrorID), Rec));
		ASSERT(ret1.second);
		pair<map<E_S3_ERROR_TYPE, shared_ptr<S3_ERROR_TRANS>>::iterator, bool> ret2 = CodeMap.insert(make_pair(S3ErrorTransTable[i].S3Error, Rec));
		ASSERT(ret2.second);
	}
	bMapsInitialized = true;
}

// S3TranslateError
// translate S3 error ID (text string) to an internal error code
E_S3_ERROR_TYPE S3TranslateError(LPCTSTR pszErrorID)
{
	if (!bMapsInitialized)
		PopulateMaps();					// initialize the maps
	{
		CSimpleRWLockAcquire lock(&rwlS3Translate, false);			// read lock
		ASSERT(bMapsInitialized);
		map<CStringSortNoCase, shared_ptr<S3_ERROR_TRANS>>::iterator itMap = IDMap.find(CStringSortNoCase(pszErrorID));
		if (itMap != IDMap.end())
			return itMap->second->S3Error;
	}
	return S3_ERROR_UNKNOWN;
}

// S3ErrorInfo
// get error info given the error code
// returns true if error is recognized
bool S3ErrorInfo(E_S3_ERROR_TYPE Code, CString *psErrorID, CString *psErrorText)
{
	if (!bMapsInitialized)
		PopulateMaps();					// initialize the maps
	{
		CSimpleRWLockAcquire lock(&rwlS3Translate, false);			// read lock
		ASSERT(bMapsInitialized);
		map<E_S3_ERROR_TYPE, shared_ptr<S3_ERROR_TRANS>>::iterator itMap = CodeMap.find(Code);
		if (itMap != CodeMap.end())
		{
			if (psErrorID != nullptr)
				*psErrorID = itMap->second->sErrorID;
			if (psErrorText)
				*psErrorText = itMap->second->sErrorText;
			return true;
		}
	}
	return false;
}
