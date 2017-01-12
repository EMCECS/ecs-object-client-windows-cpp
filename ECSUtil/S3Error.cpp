//
// Copyright (C) 1994 - 2014 EMC Corporation
// All rights reserved.
//


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
	{ S3_ERROR_AccessDenied, L"AccessDenied", L"Access Denied" },
	{ S3_ERROR_AccountProblem, L"AccountProblem", L"There is a problem with your AWS account that prevents the operation from completing successfully. Please use Contact Us." },
	{ S3_ERROR_AmbiguousGrantByEmailAddress, L"AmbiguousGrantByEmailAddress", L"The email address you provided is associated with more than one account." },
	{ S3_ERROR_BadDigest, L"BadDigest", L"The Content-MD5 you specified did not match what we received." },
	{ S3_ERROR_BucketAlreadyExists, L"BucketAlreadyExists", L"The requested bucket name is not available. The bucket namespace is shared by all users of the system. Please select a different name and try again." },
	{ S3_ERROR_BucketAlreadyOwnedByYou, L"BucketAlreadyOwnedByYou", L"Your previous request to create the named bucket succeeded and you already own it. You get this error in all AWS regions except US Standard, us-east-1. In us-east-1 region, you will get 200 OK, but it is no-op (if bucket exists it Amazon S3 will not do anything)." },
	{ S3_ERROR_BucketNotEmpty, L"BucketNotEmpty", L"The bucket you tried to delete is not empty." },
	{ S3_ERROR_CredentialsNotSupported, L"CredentialsNotSupported", L"This request does not support credentials." },
	{ S3_ERROR_CrossLocationLoggingProhibited, L"CrossLocationLoggingProhibited", L"Cross-location logging not allowed. Buckets in one geographic location cannot log information to a bucket in another location." },
	{ S3_ERROR_EntityTooSmall, L"EntityTooSmall", L"Your proposed upload is smaller than the minimum allowed object size." },
	{ S3_ERROR_EntityTooLarge, L"EntityTooLarge", L"Your proposed upload exceeds the maximum allowed object size." },
	{ S3_ERROR_ExpiredToken, L"ExpiredToken", L"The provided token has expired." },
	{ S3_ERROR_IllegalVersioningConfigurationException, L"IllegalVersioningConfigurationException", L"Indicates that the versioning configuration specified in the request is invalid." },
	{ S3_ERROR_IncompleteBody, L"IncompleteBody", L"You did not provide the number of bytes specified by the Content-Length HTTP header" },
	{ S3_ERROR_IncorrectNumberOfFilesInPostRequest, L"IncorrectNumberOfFilesInPostRequest", L"POST requires exactly one file upload per request." },
	{ S3_ERROR_InlineDataTooLarge, L"InlineDataTooLarge", L"Inline data exceeds the maximum allowed size." },
	{ S3_ERROR_InternalError, L"InternalError", L"We encountered an internal error. Please try again." },
	{ S3_ERROR_InvalidAccessKeyId, L"InvalidAccessKeyId", L"The AWS access key Id you provided does not exist in our records." },
	{ S3_ERROR_InvalidAddressingHeader, L"InvalidAddressingHeader", L"You must specify the Anonymous role." },
	{ S3_ERROR_InvalidArgument, L"InvalidArgument", L"Invalid Argument" },
	{ S3_ERROR_InvalidBucketName, L"InvalidBucketName", L"The specified bucket is not valid." },
	{ S3_ERROR_InvalidBucketState, L"InvalidBucketState", L"The request is not valid with the current state of the bucket." },
	{ S3_ERROR_InvalidDigest, L"InvalidDigest", L"The Content-MD5 you specified is not valid." },
	{ S3_ERROR_InvalidEncryptionAlgorithmError, L"InvalidEncryptionAlgorithmError", L"The encryption request you specified is not valid. The valid value is AES256." },
	{ S3_ERROR_InvalidLocationConstraint, L"InvalidLocationConstraint", L"The specified location constraint is not valid. For more information about regions, see How to Select a Region for Your Buckets." },
	{ S3_ERROR_InvalidObjectState, L"InvalidObjectState", L"The operation is not valid for the current state of the object." },
	{ S3_ERROR_InvalidPart, L"InvalidPart", L"One or more of the specified parts could not be found. The part might not have been uploaded, or the specified entity tag might not have matched the part's entity tag." },
	{ S3_ERROR_InvalidPartOrder, L"InvalidPartOrder", L"The list of parts was not in ascending order.Parts list must specified in order by part number." },
	{ S3_ERROR_InvalidPayer, L"InvalidPayer", L"All access to this object has been disabled." },
	{ S3_ERROR_InvalidPolicyDocument, L"InvalidPolicyDocument", L"The content of the form does not meet the conditions specified in the policy document." },
	{ S3_ERROR_InvalidRange, L"InvalidRange", L"The requested range cannot be satisfied." },
	{ S3_ERROR_InvalidRequest, L"InvalidRequest", L"SOAP requests must be made over an HTTPS connection." },
	{ S3_ERROR_InvalidSecurity, L"InvalidSecurity", L"The provided security credentials are not valid." },
	{ S3_ERROR_InvalidSOAPRequest, L"InvalidSOAPRequest", L"The SOAP request body is invalid." },
	{ S3_ERROR_InvalidStorageClass, L"InvalidStorageClass", L"The storage class you specified is not valid." },
	{ S3_ERROR_InvalidTargetBucketForLogging, L"InvalidTargetBucketForLogging", L"The target bucket for logging does not exist, is not owned by you, or does not have the appropriate grants for the log-delivery group." },
	{ S3_ERROR_InvalidToken, L"InvalidToken", L"The provided token is malformed or otherwise invalid." },
	{ S3_ERROR_InvalidURI, L"InvalidURI", L"Couldn't parse the specified URI." },
	{ S3_ERROR_KeyTooLong, L"KeyTooLong", L"Your key is too long." },
	{ S3_ERROR_MalformedACLError, L"MalformedACLError", L"The XML you provided was not well-formed or did not validate against our published schema." },
	{ S3_ERROR_MalformedPOSTRequest, L"MalformedPOSTRequest", L"The body of your POST request is not well-formed multipart/form-data." },
	{ S3_ERROR_MalformedXML, L"MalformedXML", L"This happens when the user sends malformed xml (xml that doesn't conform to the published xsd) for the configuration. The error message is, \"The XML you provided was not well - formed or did not validate against our published schema.\"" },
	{ S3_ERROR_MaxMessageLengthExceeded, L"MaxMessageLengthExceeded", L"Your request was too big." },
	{ S3_ERROR_MaxPostPreDataLengthExceededError, L"MaxPostPreDataLengthExceededError", L"Your POST request fields preceding the upload file were too large." },
	{ S3_ERROR_MetadataTooLarge, L"MetadataTooLarge", L"Your metadata headers exceed the maximum allowed metadata size." },
	{ S3_ERROR_MethodNotAllowed, L"MethodNotAllowed", L"The specified method is not allowed against this resource." },
	{ S3_ERROR_MissingAttachment, L"MissingAttachment", L"A SOAP attachment was expected, but none were found." },
	{ S3_ERROR_MissingContentLength, L"MissingContentLength", L"You must provide the Content-Length HTTP header." },
	{ S3_ERROR_MissingRequestBodyError, L"MissingRequestBodyError", L"This happens when the user sends an empty xml document as a request. The error message is, \"Request body is empty.\"" },
	{ S3_ERROR_MissingSecurityElement, L"MissingSecurityElement", L"The SOAP 1.1 request is missing a security element." },
	{ S3_ERROR_MissingSecurityHeader, L"MissingSecurityHeader", L"Your request is missing a required header." },
	{ S3_ERROR_NoLoggingStatusForKey, L"NoLoggingStatusForKey", L"There is no such thing as a logging status subresource for a key." },
	{ S3_ERROR_NoSuchBucket, L"NoSuchBucket", L"The specified bucket does not exist." },
	{ S3_ERROR_NoSuchKey, L"NoSuchKey", L"The specified key does not exist." },
	{ S3_ERROR_NoSuchLifecycleConfiguration, L"NoSuchLifecycleConfiguration", L"The lifecycle configuration does not exist." },
	{ S3_ERROR_NoSuchUpload, L"NoSuchUpload", L"The specified multipart upload does not exist. The upload ID might be invalid, or the multipart upload might have been aborted or completed." },
	{ S3_ERROR_NoSuchVersion, L"NoSuchVersion", L"Indicates that the version ID specified in the request does not match an existing version." },
	{ S3_ERROR_NotImplemented, L"NotImplemented", L"A header you provided implies functionality that is not implemented." },
	{ S3_ERROR_NotSignedUp, L"NotSignedUp", L"Your account is not signed up for the Amazon S3 service. You must sign up before you can use Amazon S3. You can sign up at the following URL: http://aws.amazon.com/s3" },
	{ S3_ERROR_NotSuchBucketPolicy, L"NotSuchBucketPolicy", L"The specified bucket does not have a bucket policy." },
	{ S3_ERROR_OperationAborted, L"OperationAborted", L"A conflicting conditional operation is currently in progress against this resource. Try again." },
	{ S3_ERROR_PermanentRedirect, L"PermanentRedirect", L"The bucket you are attempting to access must be addressed using the specified endpoint. Send all future requests to this endpoint." },
	{ S3_ERROR_PreconditionFailed, L"PreconditionFailed", L"At least one of the preconditions you specified did not hold." },
	{ S3_ERROR_Redirect, L"Redirect", L"Temporary redirect." },
	{ S3_ERROR_RestoreAlreadyInProgress, L"RestoreAlreadyInProgress", L"Object restore is already in progress." },
	{ S3_ERROR_RequestIsNotMultiPartContent, L"RequestIsNotMultiPartContent", L"Bucket POST must be of the enclosure-type multipart/form-data." },
	{ S3_ERROR_RequestTimeout, L"RequestTimeout", L"Your socket connection to the server was not read from or written to within the timeout period." },
	{ S3_ERROR_RequestTimeTooSkewed, L"RequestTimeTooSkewed", L"The difference between the request time and the server's time is too large." },
	{ S3_ERROR_RequestTorrentOfBucketError, L"RequestTorrentOfBucketError", L"Requesting the torrent file of a bucket is not permitted." },
	{ S3_ERROR_SignatureDoesNotMatch, L"SignatureDoesNotMatch", L"The request signature we calculated does not match the signature you provided. Check your AWS secret access key and signing method. For more information, see REST Authentication and SOAP Authentication for details." },
	{ S3_ERROR_ServiceUnavailable, L"ServiceUnavailable", L"Reduce your request rate." },
	{ S3_ERROR_SlowDown, L"SlowDown", L"Reduce your request rate." },
	{ S3_ERROR_TemporaryRedirect, L"TemporaryRedirect", L"You are being redirected to the bucket while DNS updates." },
	{ S3_ERROR_TokenRefreshRequired, L"TokenRefreshRequired", L"The provided token must be refreshed." },
	{ S3_ERROR_TooManyBuckets, L"TooManyBuckets", L"You have attempted to create more buckets than allowed." },
	{ S3_ERROR_UnexpectedContent, L"UnexpectedContent", L"This request does not support content." },
	{ S3_ERROR_UnresolvableGrantByEmailAddress, L"UnresolvableGrantByEmailAddress", L"The email address you provided does not match any account on record." },
	{ S3_ERROR_UserKeyMustBeSpecified, L"UserKeyMustBeSpecified", L"The bucket POST must contain the specified field name. If it is specified, check the order of the fields." },
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
			if (psErrorID != NULL)
				*psErrorID = itMap->second->sErrorID;
			if (psErrorText)
				*psErrorText = itMap->second->sErrorText;
			return true;
		}
	}
	return false;
}
