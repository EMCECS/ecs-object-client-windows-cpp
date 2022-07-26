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

#include "CngAES_GCM.h"
#pragma comment(lib, "Bcrypt.lib")

namespace ecs_sdk
{

	map<CStringW, shared_ptr<CCngAES_GCM::ALG_HANDLE>> CCngAES_GCM::ProviderCache;
	CSimpleRWLock CCngAES_GCM::rwlProviderCache;									// lock used for provider cache

	CCngAES_GCM::CCngAES_GCM()
		: BLOCKSIZE(16)
		, bInitialized(false)
		, bUseOldAesGcm(false)
		, bUseECB(false)
		, hAesAlg(nullptr)
		, hKey(nullptr)
		, hHashAlg(nullptr)
		, hHash(nullptr)
	{
		BCRYPT_INIT_AUTH_MODE_INFO(AuthInfoDecrypt);
		BCRYPT_INIT_AUTH_MODE_INFO(AuthInfoEncrypt);
	}

	CCngAES_GCM::~CCngAES_GCM()
	{
		// clean up hash variables
		CleanUpHash();
		hHashAlg = nullptr;
		hHash = nullptr;
		// clean up encrypt/decrypt variables
		if (hAesAlg != nullptr)
		{
			hAesAlg = nullptr;				// all alg providers are cached. don't close them here!
		}
		if (hKey != nullptr)
		{
			(void)BCryptDestroyKey(hKey);
			hKey = nullptr;
		}
		if (!KeyObject.IsEmpty())
			(void)SecureZeroMemory(KeyObject.GetData(), KeyObject.GetBufSize());
		if (!IVBuf.IsEmpty())
			(void)SecureZeroMemory(IVBuf.GetData(), IVBuf.GetBufSize());
		if (!TagBuffer.IsEmpty())
			(void)SecureZeroMemory(TagBuffer.GetData(), TagBuffer.GetBufSize());
		if (!MacBuffer.IsEmpty())
			(void)SecureZeroMemory(MacBuffer.GetData(), MacBuffer.GetBufSize());
		hAesAlg = nullptr;
		hKey = nullptr;
		bInitialized = false;
	}

	// use any non-keyed hash, such as:
	// BCRYPT_SHA1_ALGORITHM, BCRYPT_SHA256_ALGORITHM, etc.
	// for HMAC, use pbSecret and cbSecret
	void CCngAES_GCM::CreateHash(LPCWSTR HashType, PUCHAR pbSecret, ULONG cbSecret)
	{
		NTSTATUS Status;
		DWORD dwHashObject, dwData;

		// kill any previous hash in progress
		CleanUpHash();
		bool bHMAC = (pbSecret != nullptr) && (cbSecret != 0);
		if (!NT_SUCCESS(Status = OpenCachedAlgorithmProvider(&hHashAlg, HashType, bHMAC)))
			throw CErrorInfo(_T(__FILE__), __LINE__, Status);
		sHashAlgorithm = HashType;
		// get the size of the hash object
		if (!NT_SUCCESS(Status = BCryptGetProperty(hHashAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&dwHashObject, sizeof(DWORD), &dwData, 0)))
			throw CErrorInfo(_T(__FILE__), __LINE__, Status);
		HashObject.SetBufSize(dwHashObject);
		if (!NT_SUCCESS(Status = BCryptCreateHash(hHashAlg, &hHash, HashObject.GetData(), HashObject.GetBufSize(), pbSecret, cbSecret, 0)))
			throw CErrorInfo(_T(__FILE__), __LINE__, Status);
	}

	CStringW CCngAES_GCM::GetHashAlgorithm(void)
	{
		return this->sHashAlgorithm;
	}

	void CCngAES_GCM::CreateHash(LPCWSTR HashType, const CBuffer& KeyBuf)
	{
		CreateHash(HashType, (PUCHAR)KeyBuf.GetData(), KeyBuf.GetBufSize());
	}

	// add data to the hash
	// this can be called any number of times to add more data to the hash
	void CCngAES_GCM::AddHashData(const BYTE* pData, UINT uLen)
	{
		NTSTATUS Status;
		if (!NT_SUCCESS(Status = BCryptHashData(hHash, (PUCHAR)pData, uLen, 0)))
			throw CErrorInfo(_T(__FILE__), __LINE__, Status);
	}

	void CCngAES_GCM::AddHashData(const CBuffer& DataBuf)
	{
		AddHashData(DataBuf.GetData(), DataBuf.GetBufSize());
	}

	// get the resulting hash
	void CCngAES_GCM::GetHashData(CBuffer& Hash, bool bFinish)
	{
		NTSTATUS Status;
		DWORD dwHashLen, dwData;

		try
		{
			// get the size of the hash object
			if (!NT_SUCCESS(Status = BCryptGetProperty(hHashAlg, BCRYPT_HASH_LENGTH, (PBYTE)&dwHashLen, sizeof(DWORD), &dwData, 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
			Hash.SetBufSize(dwHashLen);
			if (bFinish)
			{
				if (!NT_SUCCESS(Status = BCryptFinishHash(hHash, Hash.GetData(), Hash.GetBufSize(), 0)))
					throw CErrorInfo(_T(__FILE__), __LINE__, Status);
				CleanUpHash();
			}
			else
			{
				CBuffer LocalHashObject;
				BCRYPT_HASH_HANDLE hNewHash;
				DWORD dwHashObjLen;
				if (!NT_SUCCESS(Status = BCryptGetProperty(hHashAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&dwHashObjLen, sizeof(DWORD), &dwData, 0)))
					throw CErrorInfo(_T(__FILE__), __LINE__, Status);
				LocalHashObject.SetBufSize(dwHashObjLen);
				// duplicate the hash so we can get the current results without closing out the hash
				if (!NT_SUCCESS(Status = BCryptDuplicateHash(hHash, &hNewHash, LocalHashObject.GetData(), LocalHashObject.GetBufSize(), 0)))
					throw CErrorInfo(_T(__FILE__), __LINE__, Status);
				if (!NT_SUCCESS(Status = BCryptFinishHash(hNewHash, Hash.GetData(), Hash.GetBufSize(), 0)))
					throw CErrorInfo(_T(__FILE__), __LINE__, Status);
				(void)BCryptDestroyHash(hNewHash);
				// the hNewHash is now closed so the LocalHashObject can be freed
			}
		}
		catch (const CErrorInfo& E)
		{
			if (hHashAlg != nullptr)
			{
				CloseCachedAlgorithmProvider(hHashAlg);
				hHashAlg = nullptr;
			}
			if (!bFinish)
				CleanUpHash();
			throw CErrorInfo(_T(__FILE__), __LINE__, E.dwError);
		}
	}

	void CCngAES_GCM::CleanUpHash()
	{
		// destroy hash parameters
		if (hHash != nullptr)
		{
			(void)BCryptDestroyHash(hHash);
			hHash = nullptr;
		}
		if (hHashAlg != nullptr)
		{
			hHashAlg = nullptr;				// all alg providers are cached. don't close them here!
			sHashAlgorithm.Empty();
		}
	}

	bool CCngAES_GCM::Generate256BitKey(const BYTE* pPassword, UINT uPasswordLen, const BYTE* pSalt, UINT uSaltLen, UINT uIteration, CBuffer& Key)
	{
		try
		{
			CreateHash(BCRYPT_SHA256_ALGORITHM);
			AddHashData(pPassword, uPasswordLen);
			AddHashData(pSalt, uSaltLen);
			GetHashData(Key);
			// now hash the thing on itself uIteration times
			for (UINT i = 0; i < uIteration; i++)
			{
				CreateHash(BCRYPT_SHA256_ALGORITHM);
				AddHashData(Key);
				GetHashData(Key);
			}
		}
		catch (const CErrorInfo& E)
		{
			if (hHashAlg != nullptr)
			{
				CloseCachedAlgorithmProvider(hHashAlg);
				hHashAlg = nullptr;
			}
			SetLastError(E.dwError);
			return false;
		}
		return true;
	}

	// use any non-keyed hash, such as:
	// BCRYPT_SHA1_ALGORITHM, BCRYPT_SHA256_ALGORITHM, etc.
	bool CCngAES_GCM::GenerateRandom(BYTE* buffer, int size)
	{
		NTSTATUS Status;
		BCRYPT_ALG_HANDLE hRngAlg = nullptr;

		try
		{
			if (!NT_SUCCESS(Status = OpenCachedAlgorithmProvider(&hRngAlg, BCRYPT_RNG_ALGORITHM, false)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
			if (!NT_SUCCESS(Status = BCryptGenRandom(hRngAlg, buffer, size, 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
		}
		catch (const CErrorInfo& E)
		{
			if (hRngAlg != nullptr)
			{
				CloseCachedAlgorithmProvider(hRngAlg);
				hRngAlg = nullptr;
			}
			Status = E.dwError;
		}
		if (hRngAlg != nullptr)
			hRngAlg = nullptr;				// all alg providers are cached. don't close them here!
		SetLastError(Status);
		return NT_SUCCESS(Status);
	}

	// Sets the Key. The key must be of size KEYSIZE_128,
	// KEYSIZE_192 or KEYSIZE_256.
	void CCngAES_GCM::SetKey(const BYTE* key, int ksize)
	{
		InitAES();
#ifdef _DEBUG
		ASSERT(FALSE == IsBadReadPtr(key, ksize));
#endif
		NTSTATUS Status;
		InitAES();

		// Is someone is re-keying, we need to release the old key here...
		if (hKey != nullptr)
		{
			if (!NT_SUCCESS(Status = BCryptDestroyKey(hKey)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
			hKey = nullptr;
		}

		ASSERT(ksize == KEYSIZE_128 || ksize == KEYSIZE_192 || ksize == KEYSIZE_256);
		if (!(ksize == KEYSIZE_128 || ksize == KEYSIZE_192 || ksize == KEYSIZE_256))
			throw CErrorInfo(_T(__FILE__), __LINE__, NTE_BAD_LEN, _T("SetKey: Key size is not valid"));

		// Generate the key from supplied input key bytes.
		if (!NT_SUCCESS(Status = BCryptGenerateSymmetricKey(hAesAlg, &hKey, KeyObject.GetData(), KeyObject.GetBufSize(), (PBYTE)key, ksize, 0)))
			throw CErrorInfo(_T(__FILE__), __LINE__, Status);
	}

	// Sets the IV. The IV must be of size BLOCKSIZE.
	void CCngAES_GCM::SetIv(const BYTE* iv, int vsize)
	{
		InitAES();
#ifdef _DEBUG
		ASSERT(FALSE == IsBadReadPtr(iv, vsize));
#endif

		ASSERT(nullptr != hKey);
		if (nullptr == hKey)						//lint !e774 (Info -- Boolean within 'if' always evaluates to False
			throw CErrorInfo(_T(__FILE__), __LINE__, NTE_NO_KEY, _T("SetIv: key is not valid"));

		ASSERT(nullptr != iv);
		if (nullptr == iv)						//lint !e774 (Info -- Boolean within 'if' always evaluates to False
			throw CErrorInfo(_T(__FILE__), __LINE__, NTE_NO_KEY, _T("SetIv: IV buffer is nullptr"));

		ASSERT(IV_SIZE == vsize);
		if (IV_SIZE != vsize)						//lint !e774 (Info -- Boolean within 'if' always evaluates to False
			throw CErrorInfo(_T(__FILE__), __LINE__, NTE_BAD_LEN, _T("SetIv: IV block size is not valid"));

		ASSERT(IVBuf.GetBufSize() >= (IV_SIZE + sizeof(DWORD)));
		if (IVBuf.GetBufSize() < (IV_SIZE + sizeof(DWORD)))
			throw CErrorInfo(_T(__FILE__), __LINE__, NTE_BAD_LEN, _T("SetIv: IV block size is not valid"));

		memcpy(IVBuf.GetData(), iv, vsize);
	}

	// Returns the maximum size of the ciphertext, which includes
	// padding for the plaintext
	bool CCngAES_GCM::MaxCipherTextSize(DWORD psize, DWORD& csize)
	{
		DWORD blocks = psize / BLOCKSIZE + 1;

		csize = blocks * BLOCKSIZE;
		return true;
	}

	// Returns the maximum size of the plaintext, which includes
	// removal of padding on the plaintext
	bool CCngAES_GCM::MaxPlainTextSize(DWORD csize, DWORD& psize)
	{
		DWORD blocks = csize / BLOCKSIZE + 1;

		psize = blocks * BLOCKSIZE;
		return true;
	}

	void CCngAES_GCM::InitAES()
	{
		NTSTATUS Status;
		DWORD cbData;

		if (bInitialized)
			return;
		BCRYPT_INIT_AUTH_MODE_INFO(AuthInfoDecrypt);
		BCRYPT_INIT_AUTH_MODE_INFO(AuthInfoEncrypt);
		// Open an algorithm handle.
		if (!NT_SUCCESS(Status = OpenCachedAlgorithmProvider(&hAesAlg, BCRYPT_AES_ALGORITHM, false)))
			throw CErrorInfo(_T(__FILE__), __LINE__, Status);

		// Calculate the size of the buffer to hold the KeyObject.
		DWORD cbKeyObject;
		if (!NT_SUCCESS(Status = BCryptGetProperty(hAesAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbKeyObject, sizeof(DWORD), &cbData, 0)))
			throw CErrorInfo(_T(__FILE__), __LINE__, Status);

		// Allocate the key object on the heap.
		KeyObject.SetBufSize(cbKeyObject);

		// Calculate the block length
		if (!NT_SUCCESS(Status = BCryptGetProperty(hAesAlg, BCRYPT_BLOCK_LENGTH, (PBYTE)&BLOCKSIZE, sizeof(DWORD), &cbData, 0)))
			throw CErrorInfo(_T(__FILE__), __LINE__, Status);

		// Allocate a buffer for the IV. The buffer is consumed during the 
		// encrypt/decrypt process.
		// BUG! They don't mention it in the docs, but the Encrypt/Decrypt routines modify the 4 bytes AFTER the end of the IV
		// so just in case we'll allocate more than necessary to make sure the heap isn't corrupted.
		IVBuf.SetBufSize(IV_SIZE * 2);

		if (!bUseECB)
		{
			if (!NT_SUCCESS(Status = BCryptSetProperty(hAesAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);

			BCRYPT_KEY_LENGTHS_STRUCT TagLength;
			if (!NT_SUCCESS(Status = BCryptGetProperty(hAesAlg, BCRYPT_AUTH_TAG_LENGTH, (PBYTE)&TagLength, sizeof(BCRYPT_KEY_LENGTHS_STRUCT), &cbData, 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
			TagBuffer.SetBufSize(TagLength.dwMaxLength);
			MacBuffer.SetBufSize(TagLength.dwMaxLength);
		}
		else
		{
			if (!NT_SUCCESS(Status = BCryptSetProperty(hAesAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
		}

		bInitialized = true;
	}

	// Encrypt plaintext. psize is the size of the plaintext.
	//  If successful, csize is the size of the ciphertext.
	void CCngAES_GCM::Encrypt(const BYTE* plaintext, /*In*/DWORD psize, /*InOut*/BYTE* ciphertext, /*InOut*/ DWORD& csize, bool bFinal)
	{
		NTSTATUS Status;
		CBuffer IVBufCopy;
		IVBufCopy = IVBuf;
		IVBufCopy.Lock();
		if (!bInitialized)
			throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_FUNCTION);
#ifdef _DEBUG
		// Test the pointer (it is purported good). Not safe for Release builds
		ASSERT(FALSE == IsBadReadPtr(plaintext, psize));
		ASSERT(FALSE == IsBadWritePtr(ciphertext, csize));
#endif

		// sanity check
		ASSERT(plaintext != nullptr || (plaintext == nullptr && 0 == psize));
		if (!(plaintext != nullptr || (plaintext == nullptr && 0 == psize)))			//lint !e774 (Info -- Boolean within 'left side of && within right side of || within argument of ! within if' always evaluates to True
			throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_USER_BUFFER, _T("Encrypt(2): Plain text buffer is not valid"));

		// sanity check
		ASSERT(nullptr != ciphertext);
		if (nullptr == ciphertext)				//lint !e774 (Info -- Boolean within 'if' always evaluates to False
			throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_USER_BUFFER, _T("Encrypt(2): Cipher text buffer is not valid"));

		if (!bUseECB)
		{
			AuthInfoEncrypt.pbNonce = bUseOldAesGcm ? IVBufCopy.GetData() : IVBuf.GetData();
			AuthInfoEncrypt.cbNonce = IV_SIZE;
			AuthInfoEncrypt.pbAuthData = nullptr;
			AuthInfoEncrypt.cbAuthData = 0;
			AuthInfoEncrypt.pbTag = TagBuffer.GetData();
			AuthInfoEncrypt.cbTag = TagBuffer.GetBufSize();
			AuthInfoEncrypt.pbMacContext = MacBuffer.GetData();
			AuthInfoEncrypt.cbMacContext = MacBuffer.GetBufSize();
			SET_CLR_BIT(AuthInfoEncrypt.dwFlags, BCRYPT_AUTH_MODE_CHAIN_CALLS_FLAG, !bFinal);

			// encrypt the buffer
			DWORD cbCipherText;
			if (!NT_SUCCESS(Status = BCryptEncrypt(hKey, (PUCHAR)plaintext, psize, &AuthInfoEncrypt, bUseOldAesGcm ? IVBufCopy.GetData() : IVBuf.GetData(), IV_SIZE, ciphertext, csize, &cbCipherText, 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
			csize = cbCipherText;
		}
		else
		{
			DWORD cbCipherText;
			if (!NT_SUCCESS(Status = BCryptEncrypt(hKey, (PUCHAR)plaintext, psize, nullptr, nullptr, 0, ciphertext, csize, &cbCipherText, 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
			csize = cbCipherText;
		}
	}

	// Decrypt plaintext. csize is the size of the ciphertext.
	//  If successful, psize is the size of the plaintext.
	void CCngAES_GCM::Decrypt(const BYTE* ciphertext, /*In*/DWORD csize, /*InOut*/BYTE* plaintext, /*InOut*/ DWORD& psize, bool bFinal)
	{
		NTSTATUS Status;
		CBuffer IVBufCopy;
		IVBufCopy = IVBuf;
		IVBufCopy.Lock();
		if (!bInitialized)
			throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_FUNCTION);
#ifdef _DEBUG
		// Test the pointer (it is purported good). Not safe for Release builds
		ASSERT(FALSE == IsBadReadPtr(ciphertext, csize));
		ASSERT(FALSE == IsBadWritePtr(plaintext, psize));
#endif

		// sanity check
		ASSERT(nullptr != ciphertext);
		ASSERT(nullptr != plaintext);
		if (nullptr == ciphertext || nullptr == plaintext)		//lint !e845 !e774
			throw CErrorInfo(_T(__FILE__), __LINE__, ERROR_INVALID_USER_BUFFER, _T("Decrypt(2): Buffer is nullptr"));
		if (!bUseECB)
		{
			AuthInfoDecrypt.pbNonce = bUseOldAesGcm ? IVBufCopy.GetData() : IVBuf.GetData();
			AuthInfoDecrypt.cbNonce = IV_SIZE;
			AuthInfoDecrypt.pbAuthData = nullptr;
			AuthInfoDecrypt.cbAuthData = 0;
			AuthInfoDecrypt.pbTag = TagBuffer.GetData();
			AuthInfoDecrypt.cbTag = TagBuffer.GetBufSize();
			AuthInfoDecrypt.pbMacContext = MacBuffer.GetData();
			AuthInfoDecrypt.cbMacContext = MacBuffer.GetBufSize();
			SET_CLR_BIT(AuthInfoDecrypt.dwFlags, BCRYPT_AUTH_MODE_CHAIN_CALLS_FLAG, !bFinal);
			// decrypt buffer
			DWORD cbPlainText;
			if (!NT_SUCCESS(Status = BCryptDecrypt(hKey, (PUCHAR)ciphertext, csize, &AuthInfoDecrypt, bUseOldAesGcm ? IVBufCopy.GetData() : IVBuf.GetData(), IV_SIZE, plaintext, psize, &cbPlainText, 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
			psize = cbPlainText;
		}
		else
		{
			DWORD cbPlainText;
			if (!NT_SUCCESS(Status = BCryptDecrypt(hKey, (PUCHAR)ciphertext, csize, nullptr, nullptr, 0, plaintext, psize, &cbPlainText, 0)))
				throw CErrorInfo(_T(__FILE__), __LINE__, Status);
			psize = cbPlainText;
		}
	}

	// Encrpyt a buffer in-place. bsize is the size of the buffer,
	//  psize is the size of the plaintext. If successful,
	//  csize is the size of the ciphertext. On entry, bsize >= csize.
	void CCngAES_GCM::Encrypt( /*InOut*/BYTE* buffer, /*In*/DWORD bsize, /*In*/DWORD psize, /*Out*/ DWORD& csize_out, bool bFinal)
	{
		DWORD csize = bsize;
		Encrypt(buffer, psize, buffer, csize, bFinal);
		csize_out = csize;
	}

	// Decrpyt a buffer in-place. bsize is the size of the buffer,
	//  csize is the size of the ciphertext. If successful,
	//  psize is the size of the recovered plaintext.
	//  On entry, bsize >= psize.
	void CCngAES_GCM::Decrypt( /*InOut*/BYTE* buffer, /*In*/DWORD bsize, /*In*/DWORD csize, /*Out*/ DWORD& psize_out, bool bFinal)
	{
		DWORD psize = bsize;
		Decrypt(buffer, csize, buffer, psize, bFinal);
		psize_out = psize;
	}

	// returns the calculated tag buffer. This is valid only after the last Encrypt call with bFinal set to true
	void CCngAES_GCM::GetTagBuffer(CBuffer& TagBuf)
	{
		TagBuf = TagBuffer;
	}

	// sets the previously calculated tag buffer. This is valid only before the first Decrypt call. It sets the tag buffer retrieved from the corresponding Encrypt
	void CCngAES_GCM::SetTagBuffer(const CBuffer& TagBuf)
	{
		TagBuffer = TagBuf;
	}

	void CCngAES_GCM::SetTagBuffer(const BYTE* pData, UINT uLen)
	{
		TagBuffer.Load(pData, uLen);
	}

	bool CCngAES_GCM::IfFIPSMode(void)
	{
		BOOLEAN fEnabled;
		NTSTATUS Status = BCryptGetFipsAlgorithmMode(&fEnabled);
		if ((Status != STATUS_SUCCESS) || !fEnabled)
			return false;
		return true;
	}

	NTSTATUS CCngAES_GCM::OpenCachedAlgorithmProvider(BCRYPT_ALG_HANDLE* phAlg, LPCWSTR pszAlgName, bool bHMAC)
	{
		NTSTATUS Status = STATUS_SUCCESS;
		CStringW sAlgName(pszAlgName);
		if (bHMAC)
			sAlgName += _T("!");

		{
			CSimpleRWLockAcquire lock(&rwlProviderCache);			// read lock
			map<CStringW, shared_ptr<ALG_HANDLE>>::const_iterator itMap = ProviderCache.find(sAlgName);
			if (itMap != ProviderCache.end())
			{
				*phAlg = itMap->second->hAlg;
				return STATUS_SUCCESS;
			}
		}
		{
			CSimpleRWLockAcquire lock(&rwlProviderCache, true);			// write lock
			// gotta try the search again because we dropped the lock for an instant and another thread may have created one
			map<CStringW, shared_ptr<ALG_HANDLE>>::const_iterator itMap = ProviderCache.find(sAlgName);
			if (itMap != ProviderCache.end())
			{
				*phAlg = itMap->second->hAlg;
				return STATUS_SUCCESS;
			}
			Status = BCryptOpenAlgorithmProvider(phAlg, pszAlgName, nullptr, bHMAC ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0);
			if (!NT_SUCCESS(Status))
				return Status;
			// now cache this result
			shared_ptr<ALG_HANDLE> Alg = make_shared<ALG_HANDLE>(*phAlg);
			pair<map<CStringW, shared_ptr<ALG_HANDLE>>::iterator, bool> Ret = ProviderCache.insert(make_pair(sAlgName, Alg));
			ASSERT(Ret.second);				// Ret.second == true means the insertion was made. there was no conflict
		}
		return STATUS_SUCCESS;
	}

	void CCngAES_GCM::CloseCachedAlgorithmProvider(BCRYPT_ALG_HANDLE hAlg)
	{
		CSimpleRWLockAcquire lock(&rwlProviderCache, true);			// write lock
		for (map<CStringW, shared_ptr<ALG_HANDLE>>::iterator itMap = ProviderCache.begin(); itMap != ProviderCache.end(); )
		{
			if (itMap->second->hAlg == hAlg)
				itMap = ProviderCache.erase(itMap);
			else
				++itMap;
		}
	}

	CCngAES_GCM::ALG_HANDLE::ALG_HANDLE(BCRYPT_ALG_HANDLE hAlgParam)
		: hAlg(hAlgParam)
	{}

	CCngAES_GCM::ALG_HANDLE::~ALG_HANDLE()
	{
		if (hAlg != nullptr)
		{
			(void)BCryptCloseAlgorithmProvider(hAlg, 0);
			hAlg = nullptr;
		}
	}

} // end namespace ecs_sdk
