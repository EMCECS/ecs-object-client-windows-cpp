//
// Copyright (C) 1994 - 2011 EMC Corporation
// All rights reserved.
//


#include "stdafx.h"

static const char cvs_rev[]				= "$Revision: 3649 $";

#include <atlbase.h>
#include <atlstr.h>
#include <xmllite.h>
#include "generic_defs.h"

#include "XmlLiteUtil.h"

#pragma comment(lib, "XmlLite.lib")

#ifdef DEBUG_DUMP_QUEUES
CCriticalSection *CBufferStream::pcsGlobalCBufferStreamSet;
std::set<CBufferStream *> *CBufferStream::pGlobalCBufferStreamSet;
#endif

	struct XML_REC
{
	CString sName;
};

// CheckIfInterested
// match the list of XML paths with the current stack location
// if it matches, call the callback
// format of path: //<elem1>/<elem2>/<elem3>...
static HRESULT CheckIfInterested(
	const list<CString>& XmlPaths,
	const deque<XML_REC>& XmlStack,
	XMLLITE_READER_CB ReaderCB,
	void *pContext,
	IXmlReader *pReader,
	XmlNodeType NodeType,
	const list<XML_LITE_ATTRIB> *pAttrList,
	const CString *psValue)
{
	HRESULT hr;
	for (list<CString>::const_iterator itList = XmlPaths.begin(); itList != XmlPaths.end(); ++itList)
	{
		if (itList->IsEmpty())
		{
			// if empty, then we want a callback for every element
			CString sPath(L"/");
			for (deque<XML_REC>::const_iterator itStack = XmlStack.begin(); itStack != XmlStack.end(); ++itStack)
				sPath += L"/" + itStack->sName;
			return ReaderCB(sPath, pContext, pReader, NodeType, pAttrList, psValue);
		}
		UINT iElem = 0;
		int pos = 0;
		CString sElement;
		for (;;)
		{
			sElement = itList->Tokenize(L"/", pos);
			if (sElement.IsEmpty())
			{
				if (XmlStack.size() != iElem)
					break;
				// found it!
				hr = ReaderCB(*itList, pContext, pReader, NodeType, pAttrList, psValue);
				if (FAILED(hr))
					return hr;
				break;
			}
			if (XmlStack.size() <= iElem)
				break;
			if (sElement != XmlStack[iElem].sName)
				break;
			iElem++;
		}
	}
	return 0;
}

//
// ScanXml
// scan file looking for instances of any of the specified paths
// call the callback for each instance
//
HRESULT ScanXml(
	const CBuffer *pXml,
	const list<CString>& XmlPaths,
	void *pContext,
	XMLLITE_READER_CB ReaderCB)
{
	CComPtr<IStream> pBufStream = new CBufferStream(pXml);
	return ScanXmlStream(pBufStream, XmlPaths, pContext, ReaderCB);
}

HRESULT ScanXmlStream(
	IStream *pStream,
	const list<CString>& XmlPaths,
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
	deque<XML_REC> XmlStack;

	if (FAILED(hr = CreateXmlReader(__uuidof(IXmlReader), (void**) &pReader, NULL)))
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
				if (FAILED(hr = pReader->GetLocalName(&pwszLocalName, NULL)))
					return hr;
				if (cwchPrefix > 0)
					Rec.sName = CString(pwszPrefix) + L":" + pwszLocalName;
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
				list<XML_LITE_ATTRIB> AttrList;
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
						if (FAILED(hr = pReader->GetLocalName(&pwszLocalName, NULL)))
							return hr;
						if (FAILED(hr = pReader->GetValue(&pwszValue, NULL)))
							return hr;
						if (cwchPrefix > 0)
							AttrRec.sAttrName = CString(pwszPrefix) + L":" + pwszLocalName;
						else
							AttrRec.sAttrName = pwszLocalName;
						AttrRec.sValue = pwszValue;
						AttrList.push_back(AttrRec);
					}
  
					if (S_OK != pReader->MoveToNextAttribute())
						break;
				}

				if (FAILED(hr = CheckIfInterested(XmlPaths, XmlStack, ReaderCB, pContext, pReader, nodeType, &AttrList, NULL)))
					return hr;

				if (FAILED(hr = pReader->MoveToElement()))
					return hr;
				if (bEmptyElement)
				{
					// send a fake EndElement if it was empty (meaning it won't get a real end element)
					hr = CheckIfInterested(XmlPaths, XmlStack, ReaderCB, pContext, pReader, XmlNodeType_EndElement, NULL, NULL);
					if (FAILED(hr))
						return hr;
					XML_REC Rec = XmlStack.back();
					XmlStack.pop_back();
				}
			}
			break;

		case XmlNodeType_EndElement:
			{
				hr = CheckIfInterested(XmlPaths, XmlStack, ReaderCB, pContext, pReader, nodeType, NULL, NULL);
				if (FAILED(hr))
					return hr;
				XML_REC Rec = XmlStack.back();
				XmlStack.pop_back();
#ifdef DEBUG
				CString sName;
				if (FAILED(hr = pReader->GetPrefix(&pwszPrefix, &cwchPrefix)))
					return hr;
				if (FAILED(hr = pReader->GetLocalName(&pwszLocalName, NULL)))
					return hr;
				if (cwchPrefix > 0)
					sName = CString(pwszPrefix) + L":" + pwszLocalName;
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
				if (FAILED(hr = pReader->GetValue(&pwszValue, NULL)))
					return hr;
				CString sValue(pwszValue);
				hr = CheckIfInterested(XmlPaths, XmlStack, ReaderCB, pContext, pReader, nodeType, NULL, &sValue);
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
