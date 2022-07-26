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

#pragma once

#if defined(_AFXDLL)
#define CString_basic(BaseType) ATL::CStringT<BaseType, StrTraitMFC_DLL<BaseType>>
#else
#define CString_basic(BaseType) ATL::CStringT<BaseType, StrTraitMFC<BaseType>>
#endif


template <typename BaseType>
class CStringPassword_basic : public CString_basic(BaseType)
{
public:
	typedef CString_basic(BaseType) CThisBaseString;

public:
	CStringPassword_basic() {}
	~CStringPassword_basic();
	CStringPassword_basic(const CStringPassword_basic& Str);
	CStringPassword_basic(_In_ const CThisBaseString& strSrc);
	CStringPassword_basic(_In_opt_z_ const typename CThisBaseString::XCHAR* pszSrc);
	CStringPassword_basic(_In_ CThisBaseString::PCXSTR pch, _In_ int nLength);
	CStringPassword_basic(_In_ CThisBaseString::XCHAR ch, _In_ int nLength = 1);
	CStringPassword_basic& operator=(_In_ const CStringPassword_basic& strSrc);
	CStringPassword_basic& operator=(_In_ const CThisBaseString& strSrc);
	CStringPassword_basic& operator=(_In_z_ CThisBaseString::PCXSTR strSrc);
	CStringPassword_basic& operator=(_In_z_ CThisBaseString::XCHAR ch);
	void SecurePurge(void)
	{
		LPWSTR pBuf = __super::GetBuffer();
		std::ignore = SecureZeroMemory(pBuf, __super::GetAllocLength() * sizeof(wchar_t));
		__super::ReleaseBuffer();
	}
};

using CStringPasswordA = CStringPassword_basic<char>;
using CStringPasswordW = CStringPassword_basic<wchar_t>;
using CStringPasswordU8 = CStringPassword_basic<char8_t>;
using CStringPasswordU16 = CStringPassword_basic<char16_t>;
using CStringPasswordU32 = CStringPassword_basic<char32_t>;

using CStringPassword = CStringPassword_basic<wchar_t>;



template <typename BaseType>
CStringPassword_basic<BaseType>::~CStringPassword_basic()
{
	SecurePurge();
}

template <typename BaseType>
CStringPassword_basic<BaseType>::CStringPassword_basic(const CStringPassword_basic& Str)
	: CString(Str)
{}

template <typename BaseType>
CStringPassword_basic<BaseType>::CStringPassword_basic(_In_ const CThisBaseString& strSrc)
	: CThisBaseString(strSrc)
{}

template <typename BaseType>
CStringPassword_basic<BaseType>::CStringPassword_basic(_In_opt_z_ const typename CThisBaseString::XCHAR* pszSrc)
	: CThisBaseString(pszSrc)
{}

template <typename BaseType>
CStringPassword_basic<BaseType>::CStringPassword_basic(
	_In_ CThisBaseString::PCXSTR pch,
	_In_ int nLength)
	: CThisBaseString(pch, nLength)
{}

template <typename BaseType>
CStringPassword_basic<BaseType>::CStringPassword_basic(
	_In_ CThisBaseString::XCHAR ch,
	_In_ int nLength)
	: CThisBaseString(ch, nLength)
{}


template <typename BaseType>
CStringPassword_basic<BaseType>& CStringPassword_basic<BaseType>::operator=(_In_ const CStringPassword_basic<BaseType>& strSrc)
{
	CThisBaseString::operator=(strSrc);

	return(*this);
}

template <typename BaseType>
CStringPassword_basic<BaseType>& CStringPassword_basic<BaseType>::operator=(_In_ const CThisBaseString& strSrc)
{
	CThisBaseString::operator=(strSrc);

	return(*this);
}

template <typename BaseType>
CStringPassword_basic<BaseType>& CStringPassword_basic<BaseType>::operator=(_In_z_ CThisBaseString::PCXSTR strSrc)
{
	CThisBaseString::operator=(strSrc);

	return(*this);
}

template <typename BaseType>
CStringPassword_basic<BaseType>& CStringPassword_basic<BaseType>::operator=(_In_z_ CThisBaseString::XCHAR ch)
{
	CThisBaseString::operator=(ch);

	return(*this);
}
