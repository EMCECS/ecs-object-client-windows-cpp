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

#include "stdafx.h"


#include <atlbase.h>
#include <atlstr.h>
#include <xmllite.h>
#include "generic_defs.h"
#include "ECSConnection.h"

#include "XmlLiteUtil.h"

namespace ecs_sdk
{

#pragma comment(lib, "XmlLite.lib")

#ifdef DEBUG_DUMP_QUEUES
CCriticalSection *CBufferStream::pcsGlobalCBufferStreamSet;
std::set<CBufferStream *> *CBufferStream::pGlobalCBufferStreamSet;
#endif

struct XML_REC
{
	CStringW sName;
};

// CheckIfInterested
// match the list of XML paths with the current stack location
// if it matches, call the callback
// format of path: //<elem1>/<elem2>/<elem3>...
static HRESULT CheckIfInterested(
	const std::deque<XML_REC>& XmlStack,
	XMLLITE_READER_CB ReaderCB,
	void *pContext,
	IXmlReader *pReader,
	XmlNodeType NodeType,
	const std::list<XML_LITE_ATTRIB> *pAttrList,
	const CStringW *psValue)
{
	CStringW sPath(L"/");
	for (std::deque<XML_REC>::const_iterator itStack = XmlStack.begin(); itStack != XmlStack.end(); ++itStack)
		sPath += L"/" + itStack->sName;
	return ReaderCB(sPath, pContext, pReader, NodeType, pAttrList, psValue);
}

//
// ScanXml
// scan file looking for instances of any of the specified paths
// call the callback for each instance
//
HRESULT ScanXml(
	const CBuffer *pXml,
	void *pContext,
	XMLLITE_READER_CB ReaderCB)
{
	CComPtr<IStream> pBufStream = new CBufferStream(pXml);
	return ScanXmlStream(pBufStream, pContext, ReaderCB);
}

HRESULT ScanXmlStream(
	IStream *pStream,
	void *pContext,
	XMLLITE_READER_CB ReaderCB)
{
	HRESULT hr;
	CComPtr<IXmlReader> pReader;
	XmlNodeType nodeType;
	const WCHAR* pwszPrefix;
	const WCHAR* pwszLocalName;
	const WCHAR* pwszValue;
	UINT cwchPrefix;
	std::deque<XML_REC> XmlStack;

	if (FAILED(hr = CreateXmlReader(__uuidof(IXmlReader), (void**) &pReader, nullptr)))
		return hr;

	if (FAILED(hr = pReader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)))
		return hr;

	if (FAILED(hr = pReader->SetInput(pStream)))
		return hr;

	// read until there are no more nodes

	while (S_OK == (hr = pReader->Read(&nodeType)))
	{
		switch (nodeType)
		{
		case XmlNodeType_XmlDeclaration:
			break;

		case XmlNodeType_Element:
			{
				BOOL bEmptyElement = pReader->IsEmptyElement();
				XML_REC Rec;
				if (FAILED(hr = pReader->GetPrefix(&pwszPrefix, &cwchPrefix)))
					return hr;
				if (FAILED(hr = pReader->GetLocalName(&pwszLocalName, nullptr)))
					return hr;
				if (cwchPrefix > 0)
					Rec.sName = CStringW(pwszPrefix) + L":" + pwszLocalName;
				else
					Rec.sName = pwszLocalName;
				XmlStack.push_back(Rec);
				#ifdef DEBUG
				{
					UINT uDepth;
					(void)pReader->GetDepth(&uDepth);
					ASSERT(XmlStack.size() == (uDepth + 1));
				}
				#endif
				// collect all of the attributes for this element
				std::list<XML_LITE_ATTRIB> AttrList;
				XML_LITE_ATTRIB AttrRec;
				hr = pReader->MoveToFirstAttribute();
				if (FAILED(hr))
					return hr;
				while (hr != S_FALSE)
				{
					if (!pReader->IsDefault())
					{
						if (FAILED(hr = pReader->GetPrefix(&pwszPrefix, &cwchPrefix)))
							return hr;
						if (FAILED(hr = pReader->GetLocalName(&pwszLocalName, nullptr)))
							return hr;
						if (FAILED(hr = pReader->GetValue(&pwszValue, nullptr)))
							return hr;
						if (cwchPrefix > 0)
							AttrRec.sAttrName = CStringW(pwszPrefix) + L":" + pwszLocalName;
						else
							AttrRec.sAttrName = pwszLocalName;
						AttrRec.sValue = pwszValue;
						AttrList.push_back(AttrRec);
					}
  
					if (S_OK != pReader->MoveToNextAttribute())
						break;
				}

				if (FAILED(hr = CheckIfInterested(XmlStack, ReaderCB, pContext, pReader, nodeType, &AttrList, nullptr)))
					return hr;

				if (FAILED(hr = pReader->MoveToElement()))
					return hr;
				if (bEmptyElement)
				{
					// send a fake EndElement if it was empty (meaning it won't get a real end element)
					hr = CheckIfInterested(XmlStack, ReaderCB, pContext, pReader, XmlNodeType_EndElement, nullptr, nullptr);
					if (FAILED(hr))
						return hr;
					XmlStack.pop_back();
				}
			}
			break;

		case XmlNodeType_EndElement:
			{
				hr = CheckIfInterested(XmlStack, ReaderCB, pContext, pReader, nodeType, nullptr, nullptr);
				if (FAILED(hr))
					return hr;
				XML_REC Rec = XmlStack.back();
				XmlStack.pop_back();
#ifdef DEBUG
				CStringW sName;
				if (FAILED(hr = pReader->GetPrefix(&pwszPrefix, &cwchPrefix)))
					return hr;
				if (FAILED(hr = pReader->GetLocalName(&pwszLocalName, nullptr)))
					return hr;
				if (cwchPrefix > 0)
					sName = CStringW(pwszPrefix) + L":" + pwszLocalName;
				else
					sName = pwszLocalName;
				ASSERT(sName == Rec.sName);

				UINT uDepth;
				(void)pReader->GetDepth(&uDepth);
				ASSERT(XmlStack.size() == (uDepth - 1));
#endif
			}
			break;

		case XmlNodeType_Text:
			{
				if (FAILED(hr = pReader->GetValue(&pwszValue, nullptr)))
					return hr;
				CStringW sValue(pwszValue);
				hr = CheckIfInterested(XmlStack, ReaderCB, pContext, pReader, nodeType, nullptr, &sValue);
				if (FAILED(hr))
					return hr;
			}
			break;

		default:
			break;
		}
	}
	return 0;
}

HRESULT ProcessXmlTextField(
	const std::map<CString, XML_FIELD_ENTRY>& FieldMap,
	const CStringW& sPathRoot,							// the XML path without the last field name, such as //bucket_info/
	const CStringW& sXmlPath,
	void* pContext,
	const CStringW* psValue)
{
	if (sPathRoot != sXmlPath.Left(sPathRoot.GetLength()))
		return ERROR_INVALID_DATA;				// wrong path

	CStringW sField(sXmlPath.Mid(sPathRoot.GetLength()));
	if (!sField.IsEmpty() && (sField[0] == L'/'))
		sField.Delete(0, 1);
	std::map<CString, XML_FIELD_ENTRY>::const_iterator it = FieldMap.find((LPCTSTR)sField);
	if (it == FieldMap.end())
		return ERROR_SUCCESS;

	switch (it->second.Type)
	{
	case E_XML_FIELD_TYPE::Bool:
		{
			bool* pBool = (bool*)((char*)pContext + it->second.uOffset);
			if (psValue->CompareNoCase(L"false") == 0)
				*pBool = false;
			else if (psValue->CompareNoCase(L"true") == 0)
				*pBool = true;
			else
				return ERROR_XML_PARSE_ERROR;
		}
		break;
	case E_XML_FIELD_TYPE::String:
		{
			CString* pStr = (CString*)((char*)pContext + it->second.uOffset);
			*pStr = *psValue;
		}
		break;
	case E_XML_FIELD_TYPE::Time:
		{
			FILETIME* pFileTime = (FILETIME*)((char*)pContext + it->second.uOffset);
			DWORD dwError = CECSConnection::ParseISO8601Date(FROM_UNICODE(*psValue), *pFileTime);
			if (dwError != ERROR_SUCCESS)
				return dwError;
		}
		break;
	case E_XML_FIELD_TYPE::U32:
		{
			wchar_t* pEnd = nullptr;
			UINT* pUint = (UINT*)((char*)pContext + it->second.uOffset);
			*pUint = wcstoul(FROM_UNICODE(*psValue), &pEnd, 0);
			if (*pEnd != L'\0')
				return ERROR_XML_PARSE_ERROR;
		}
		break;
	case E_XML_FIELD_TYPE::U64:
		{
			wchar_t* pEnd = nullptr;
			ULONGLONG* pLongLong = (ULONGLONG*)((char*)pContext + it->second.uOffset);
			*pLongLong = _wcstoui64(FROM_UNICODE(*psValue), &pEnd, 0);
			if (*pEnd != L'\0')
				return ERROR_XML_PARSE_ERROR;
		}
		break;
	case E_XML_FIELD_TYPE::S32:
		{
			wchar_t* pEnd = nullptr;
			INT* pInt = (INT*)((char*)pContext + it->second.uOffset);
			*pInt = wcstol(FROM_UNICODE(*psValue), &pEnd, 0);
			if (*pEnd != L'\0')
				return ERROR_XML_PARSE_ERROR;
		}
		break;
	case E_XML_FIELD_TYPE::S64:
		{
			wchar_t* pEnd = nullptr;
			ULONGLONG* pLongLong = (ULONGLONG*)((char*)pContext + it->second.uOffset);
			*pLongLong = _wcstoi64(FROM_UNICODE(*psValue), &pEnd, 0);
			if (*pEnd != L'\0')
				return ERROR_XML_PARSE_ERROR;
		}
		break;
	}
	return ERROR_SUCCESS;
}

} // end namespace ecs_sdk
