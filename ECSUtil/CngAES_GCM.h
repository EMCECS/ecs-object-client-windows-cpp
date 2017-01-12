#pragma once

#include "exportdef.h"
#include <bcrypt.h>
#include "cbuffer.h"

class ECSUTIL_EXT_CLASS CCngAES_GCM
{
public:
	enum Parameters { KEYSIZE_128 = 16, KEYSIZE_192 = 24, KEYSIZE_256 = 32, IV_SIZE = 12 };
	DWORD BLOCKSIZE;

public:
    explicit CCngAES_GCM();
    virtual ~CCngAES_GCM( );

    // Generate a random block of bytes using the underlying CSP.
    bool GenerateRandom( BYTE* buffer, int size );

    // Sets the Key. The key must be of size KEYSIZE_128,
    // KEYSIZE_192, or KEYSIZE_256. After calling SetKey,
    // for intial keying or re-keying, call SetIv.
    void SetKey( const BYTE* key, int ksize = KEYSIZE_128 );

    // Sets the IV. The IV must be of size BLOCKSIZE. Call
    //  anytime to syncronize the IV under a key.
    void SetIv(const BYTE* iv, int vsize );

    // Encrpyt a buffer in-place. bsize is the size of the buffer,
    //  psize is the size of the plaintext. If successful,
    //  csize is the size of the ciphertext. On entry, bsize >= csize.
    void Encrypt( /*InOut*/BYTE* buffer, /*In*/DWORD bsize, /*In*/DWORD psize, /*Out*/ DWORD& csize, bool bFinal );

    // Decrpyt a buffer in-place. bsize is the size of the buffer,
    //  csize is the size of the ciphertext. If successful,
    //  psize is the size of the recovered plaintext.
    //  On entry, bsize >= psize.
    void Decrypt( /*InOut*/BYTE* buffer, /*In*/DWORD bsize, /*In*/DWORD csize, /*Out*/ DWORD& psize, bool bFinal );

    // Encrypt plaintext. psize is the size of the plaintext.
    //  If successful, csize is the size of the ciphertext.
    void Encrypt( /*In*/const BYTE* plaintext, /*In*/DWORD psize, /*InOut*/BYTE* ciphertext, /*InOut*/ DWORD& csize, bool bFinal );

    // Decrypt plaintext. csize is the size of the ciphertext.
    //  If successful, psize is the size of the plaintext.
    void Decrypt( /*In*/const BYTE* ciphertext, /*In*/DWORD csize, /*InOut*/BYTE* plaintext, /*InOut*/ DWORD& psize, bool bFinal );

	// use sha256 to generate a key
	bool Generate256BitKey(const BYTE *pPassword, UINT uPasswordLen, const BYTE *pSalt, UINT uSaltLen, UINT uIteration, CBuffer& Key);

	// use any non-keyed hash, such as:
	// BCRYPT_SHA1_ALGORITHM, BCRYPT_SHA256_ALGORITHM, etc.
	void CreateHash(LPCWSTR HashType, PUCHAR pbSecret = NULL, ULONG cbSecret = 0);
	void CreateHash(LPCWSTR HashType, const CBuffer& KeyBuf);
	CString GetHashAlgorithm(void);

	// add data to the hash
	// this can be called any number of times to add more data to the hash
	void AddHashData(const BYTE *pData, UINT uLen);
	void AddHashData(const CBuffer& DataBuf);

	// get the resulting hash
	void GetHashData(CBuffer& Hash, bool bFinish = true);

	// Returns the maximum size of the ciphertext, which includes
	// padding for the plaintext
	bool MaxCipherTextSize( DWORD psize, DWORD& csize );

	// Returns the maximum size of the plaintext, which includes
	// removal of padding on the plaintext
	bool MaxPlainTextSize( DWORD csize, DWORD& psize );

	// returns the calculated tag buffer. This is valid only after the last Encrypt call with bFinal set to true
	void GetTagBuffer(CBuffer& TagBuf);

	// sets the previously calculated tag buffer. This is valid only before the first Decrypt call. It sets the tag buffer retrieved from the corresponding Encrypt
	void SetTagBuffer(const CBuffer& TagBuf);
	void SetTagBuffer(const BYTE *pData, UINT uLen);

	static bool IfCngSupported(void)
	{
		return true;
	}

	static bool IfFIPSMode(void);

	void SetUseOldAesGcm(void)
	{
		bUseOldAesGcm = true;
	}

	void SetUseECB(void)
	{
		bUseECB = true;
	}

private:
	void CleanUpHash();
	void InitAES();

private:
	bool bInitialized;
	bool bUseOldAesGcm;					// use old algorithm where it is dependent on block size (this is used to decode old files encrypted using the old, flawed method)
	bool bUseECB;
	BCRYPT_ALG_HANDLE hAesAlg;
	BCRYPT_KEY_HANDLE hKey;
	CBuffer KeyObject;
	CBuffer IVBuf;
	CBuffer TagBuffer;
	CBuffer MacBuffer;
	// hash variables
	BCRYPT_ALG_HANDLE hHashAlg;
	BCRYPT_HASH_HANDLE hHash;
	CBuffer HashObject;
	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO AuthInfoEncrypt;
	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO AuthInfoDecrypt;
	CString sHashAlgorithm;				// saved hash algorithm
};

