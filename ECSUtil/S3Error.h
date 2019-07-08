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

#pragma once
#include "exportdef.h"
#include "stdafx.h"

enum E_S3_ERROR_TYPE
{
	// S3 errors
	S3_ERROR_SUCCESS = 0,							// success
	S3_ERROR_UNKNOWN,								// the S3 error is a new one or not documented. The text should have more information
	S3_ERROR_AccessDenied,							// Access Denied
	S3_ERROR_AccountProblem,						// There is a problem with your AWS account that prevents the operation from completing successfully. Please use Contact Us.
	S3_ERROR_AmbiguousGrantByEmailAddress,			// The email address you provided is associated with more than one account.
	S3_ERROR_BadDigest,								// The Content-MD5 you specified did not match what we received.
	S3_ERROR_BucketAlreadyExists,					// The requested bucket name is not available. The bucket namespace is shared by all users of the system. Please select a different name and try again.
	S3_ERROR_BucketAlreadyOwnedByYou,				// Your previous request to create the named bucket succeeded and you already own it. You get this error in all AWS regions except US Standard, us-east-1. In us-east-1 region, you will get 200 OK, but it is no-op (if bucket exists it Amazon S3 will not do anything).
	S3_ERROR_BucketNotEmpty,						// The bucket you tried to delete is not empty.
	S3_ERROR_CredentialsNotSupported,				// This request does not support credentials.
	S3_ERROR_CrossLocationLoggingProhibited,		// Cross-location logging not allowed. Buckets in one geographic location cannot log information to a bucket in another location.
	S3_ERROR_EntityTooSmall,						// Your proposed upload is smaller than the minimum allowed object size.
	S3_ERROR_EntityTooLarge,						// Your proposed upload exceeds the maximum allowed object size.
	S3_ERROR_ExpiredToken,							// The provided token has expired.
	S3_ERROR_IllegalVersioningConfigurationException,	// Indicates that the versioning configuration specified in the request is invalid.
	S3_ERROR_IncompleteBody,						// You did not provide the number of bytes specified by the Content-Length HTTP header
	S3_ERROR_IncorrectNumberOfFilesInPostRequest,	// POST requires exactly one file upload per request.
	S3_ERROR_InlineDataTooLarge,					// Inline data exceeds the maximum allowed size.
	S3_ERROR_InternalError,							// We encountered an internal error. Please try again.
	S3_ERROR_InvalidAccessKeyId,					// The AWS access key Id you provided does not exist in our records.
	S3_ERROR_InvalidAddressingHeader,				// You must specify the Anonymous role.
	S3_ERROR_InvalidArgument,						// Invalid Argument
	S3_ERROR_InvalidBucketName,						// The specified bucket is not valid.
	S3_ERROR_InvalidBucketState,					// The request is not valid with the current state of the bucket.
	S3_ERROR_InvalidDigest,							// The Content-MD5 you specified is not valid.
	S3_ERROR_InvalidEncryptionAlgorithmError,		// The encryption request you specified is not valid. The valid value is AES256.
	S3_ERROR_InvalidLocationConstraint,				// The specified location constraint is not valid. For more information about regions, see How to Select a Region for Your Buckets. 
	S3_ERROR_InvalidObjectState,					// The operation is not valid for the current state of the object.
	S3_ERROR_InvalidPart,							// One or more of the specified parts could not be found. The part might not have been uploaded, or the specified entity tag might not have matched the part's entity tag.
	S3_ERROR_InvalidPartOrder,						// The list of parts was not in ascending order.Parts list must specified in order by part number.
	S3_ERROR_InvalidPayer,							// All access to this object has been disabled.
	S3_ERROR_InvalidPolicyDocument,					// The content of the form does not meet the conditions specified in the policy document.
	S3_ERROR_InvalidRange,							// The requested range cannot be satisfied.
	S3_ERROR_InvalidRequest,						// SOAP requests must be made over an HTTPS connection.
	S3_ERROR_InvalidSecurity,						// The provided security credentials are not valid.
	S3_ERROR_InvalidSOAPRequest,					// The SOAP request body is invalid.
	S3_ERROR_InvalidStorageClass,					// The storage class you specified is not valid.
	S3_ERROR_InvalidTargetBucketForLogging,			// The target bucket for logging does not exist, is not owned by you, or does not have the appropriate grants for the log-delivery group. 
	S3_ERROR_InvalidToken,							// The provided token is malformed or otherwise invalid.
	S3_ERROR_InvalidURI,							// Couldn't parse the specified URI.
	S3_ERROR_KeyTooLong,							// Your key is too long.
	S3_ERROR_MalformedACLError,						// The XML you provided was not well-formed or did not validate against our published schema.
	S3_ERROR_MalformedPOSTRequest,					// The body of your POST request is not well-formed multipart/form-data.
	S3_ERROR_MalformedXML,							// This happens when the user sends malformed xml (xml that doesn't conform to the published xsd) for the configuration. The error message is, "The XML you provided was not well-formed or did not validate against our published schema." 
	S3_ERROR_MaxMessageLengthExceeded,				// Your request was too big.
	S3_ERROR_MaxPostPreDataLengthExceededError,		// Your POST request fields preceding the upload file were too large.
	S3_ERROR_MetadataTooLarge,						// Your metadata headers exceed the maximum allowed metadata size.
	S3_ERROR_MethodNotAllowed,						// The specified method is not allowed against this resource.
	S3_ERROR_MissingAttachment,						// A SOAP attachment was expected, but none were found.
	S3_ERROR_MissingContentLength,					// You must provide the Content-Length HTTP header.
	S3_ERROR_MissingRequestBodyError,				// This happens when the user sends an empty xml document as a request. The error message is, "Request body is empty." 
	S3_ERROR_MissingSecurityElement,				// The SOAP 1.1 request is missing a security element.
	S3_ERROR_MissingSecurityHeader,					// Your request is missing a required header.
	S3_ERROR_NoLoggingStatusForKey,					// There is no such thing as a logging status subresource for a key.
	S3_ERROR_NoSuchBucket,							// The specified bucket does not exist.
	S3_ERROR_NoSuchKey,								// The specified key does not exist.
	S3_ERROR_NoSuchLifecycleConfiguration,			// The lifecycle configuration does not exist. 
	S3_ERROR_NoSuchUpload,							// The specified multipart upload does not exist. The upload ID might be invalid, or the multipart upload might have been aborted or completed.
	S3_ERROR_NoSuchVersion,							// Indicates that the version ID specified in the request does not match an existing version.
	S3_ERROR_NotImplemented,						// A header you provided implies functionality that is not implemented.
	S3_ERROR_NotSignedUp,							// Your account is not signed up for the Amazon S3 service. You must sign up before you can use Amazon S3. You can sign up at the following URL: http://aws.amazon.com/s3
	S3_ERROR_NotSuchBucketPolicy,					// The specified bucket does not have a bucket policy.
	S3_ERROR_OperationAborted,						// A conflicting conditional operation is currently in progress against this resource. Try again.
	S3_ERROR_PermanentRedirect,						// The bucket you are attempting to access must be addressed using the specified endpoint. Send all future requests to this endpoint.
	S3_ERROR_PreconditionFailed,					// At least one of the preconditions you specified did not hold.
	S3_ERROR_Redirect,								// Temporary redirect.
	S3_ERROR_RestoreAlreadyInProgress,				// Object restore is already in progress.
	S3_ERROR_RequestIsNotMultiPartContent,			// Bucket POST must be of the enclosure-type multipart/form-data.
	S3_ERROR_RequestTimeout,						// Your socket connection to the server was not read from or written to within the timeout period.
	S3_ERROR_RequestTimeTooSkewed,					// The difference between the request time and the server's time is too large.
	S3_ERROR_RequestTorrentOfBucketError,			// Requesting the torrent file of a bucket is not permitted.
	S3_ERROR_SignatureDoesNotMatch,					// The request signature we calculated does not match the signature you provided. Check your AWS secret access key and signing method. For more information, see REST Authentication and SOAP Authentication for details.
	S3_ERROR_ServiceUnavailable,					// Reduce your request rate.
	S3_ERROR_SlowDown,								// Reduce your request rate.
	S3_ERROR_TemporaryRedirect,						// You are being redirected to the bucket while DNS updates.
	S3_ERROR_TokenRefreshRequired,					// The provided token must be refreshed.
	S3_ERROR_TooManyBuckets,						// You have attempted to create more buckets than allowed.
	S3_ERROR_UnexpectedContent,						// This request does not support content.
	S3_ERROR_UnresolvableGrantByEmailAddress,		// The email address you provided does not match any account on record.
	S3_ERROR_UserKeyMustBeSpecified,				// The bucket POST must contain the specified field name. If it is specified, check the order of the fields.
	S3_ERROR_ObjectUnderRetention,					// The object is under retention and can't be deleted or modified.
	S3_ERROR_MetadataSearchNotEnabled,				// Metadata search is not enabled for this bucket.
	S3_ERROR_LAST_CODE,								// sentinel
};

extern ECSUTIL_EXT_API E_S3_ERROR_TYPE S3TranslateError(LPCTSTR pszErrorID);

// S3ErrorInfo
// get error info given the error code
// returns true if error is recognized
extern ECSUTIL_EXT_API bool S3ErrorInfo(E_S3_ERROR_TYPE Code, CString *psErrorID = nullptr, CString *psErrorText = nullptr);
