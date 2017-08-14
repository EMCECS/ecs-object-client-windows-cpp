/*
 * Copyright (c) 1994 - 2017, EMC Corporation.
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * + Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * + The name of EMC Corporation may not be used to endorse or promote
 *   products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "stdafx.h"


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
	CStringW sName;
};

// CheckIfInterested
// match the list of XML paths with the current stack location
// if it matches, call the callback
// format of path: //<elem1>/<elem2>/<elem3>...
static HRESULT CheckIfInterested(
	const deque<XML_REC>& XmlStack,
	XMLLITE_READER_CB ReaderCB,
	void *pContext,
	IXmlReader *pReader,
	XmlNodeType NodeType,
	const list<XML_LITE_ATTRIB> *pAttrList,
	const CStringW *psValue)
{
	CStringW sPath(L"/");
	for (deque<XML_REC>::const_iterator itStack = XmlStack.begin(); itStack != XmlStack.end(); ++itStack)
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
							AttrRec.sAttrName = CStringW(pwszPrefix) + L":" + pwszLocalName;
						else
							AttrRec.sAttrName = pwszLocalName;
						AttrRec.sValue = pwszValue;
						AttrList.push_back(AttrRec);
					}
  
					if (S_OK != pReader->MoveToNextAttribute())
						break;
				}

				if (FAILED(hr = CheckIfInterested(XmlStack, ReaderCB, pContext, pReader, nodeType, &AttrList, NULL)))
					return hr;

				if (FAILED(hr = pReader->MoveToElement()))
					return hr;
				if (bEmptyElement)
				{
					// send a fake EndElement if it was empty (meaning it won't get a real end element)
					hr = CheckIfInterested(XmlStack, ReaderCB, pContext, pReader, XmlNodeType_EndElement, NULL, NULL);
					if (FAILED(hr))
						return hr;
					XmlStack.pop_back();
				}
			}
			break;

		case XmlNodeType_EndElement:
			{
				hr = CheckIfInterested(XmlStack, ReaderCB, pContext, pReader, nodeType, NULL, NULL);
				if (FAILED(hr))
					return hr;
				XML_REC Rec = XmlStack.back();
				XmlStack.pop_back();
#ifdef DEBUG
				CStringW sName;
				if (FAILED(hr = pReader->GetPrefix(&pwszPrefix, &cwchPrefix)))
					return hr;
				if (FAILED(hr = pReader->GetLocalName(&pwszLocalName, NULL)))
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
				if (FAILED(hr = pReader->GetValue(&pwszValue, NULL)))
					return hr;
				CStringW sValue(pwszValue);
				hr = CheckIfInterested(XmlStack, ReaderCB, pContext, pReader, nodeType, NULL, &sValue);
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
