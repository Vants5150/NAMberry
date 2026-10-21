//
// sha256.h - streaming SHA-256 and HMAC-SHA256, with no dependencies (usable in Circle and on the host for the tests)
//
#ifndef _sha256_h
#define _sha256_h

#include <stdint.h>
#include <stddef.h>

class CSha256
{
public:
	CSha256 (void) { Reset (); }

	void Reset (void)
	{
		m_H[0] = 0x6a09e667; m_H[1] = 0xbb67ae85; m_H[2] = 0x3c6ef372; m_H[3] = 0xa54ff53a;
		m_H[4] = 0x510e527f; m_H[5] = 0x9b05688c; m_H[6] = 0x1f83d9ab; m_H[7] = 0x5be0cd19;
		m_nLen = 0;
		m_nBuf = 0;
	}

	void Update (const uint8_t *pData, size_t nLen)
	{
		m_nLen += nLen;
		while (nLen > 0)
		{
			size_t n = 64 - m_nBuf;
			if (n > nLen)
			{
				n = nLen;
			}
			for (size_t i = 0; i < n; i++)
			{
				m_Buf[m_nBuf + i] = pData[i];
			}
			m_nBuf += n;
			pData += n;
			nLen -= n;
			if (m_nBuf == 64)
			{
				Block (m_Buf);
				m_nBuf = 0;
			}
		}
	}

	void Final (uint8_t Out[32])
	{
		uint64_t nBits = m_nLen * 8;
		uint8_t Pad = 0x80;
		Update (&Pad, 1);
		uint8_t Zero = 0;
		while (m_nBuf != 56)
		{
			Update (&Zero, 1);
		}
		uint8_t Len[8];
		for (int i = 0; i < 8; i++)
		{
			Len[i] = (uint8_t) (nBits >> (56 - 8 * i));
		}
		Update (Len, 8);
		for (int i = 0; i < 8; i++)
		{
			Out[i * 4 + 0] = (uint8_t) (m_H[i] >> 24);
			Out[i * 4 + 1] = (uint8_t) (m_H[i] >> 16);
			Out[i * 4 + 2] = (uint8_t) (m_H[i] >> 8);
			Out[i * 4 + 3] = (uint8_t) m_H[i];
		}
	}

private:
	static uint32_t Rotr (uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

	void Block (const uint8_t *p)
	{
		static const uint32_t K[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

		uint32_t W[64];
		for (int i = 0; i < 16; i++)
		{
			W[i] = ((uint32_t) p[i * 4] << 24) | ((uint32_t) p[i * 4 + 1] << 16)
			     | ((uint32_t) p[i * 4 + 2] << 8) | (uint32_t) p[i * 4 + 3];
		}
		for (int i = 16; i < 64; i++)
		{
			uint32_t s0 = Rotr (W[i - 15], 7) ^ Rotr (W[i - 15], 18) ^ (W[i - 15] >> 3);
			uint32_t s1 = Rotr (W[i - 2], 17) ^ Rotr (W[i - 2], 19) ^ (W[i - 2] >> 10);
			W[i] = W[i - 16] + s0 + W[i - 7] + s1;
		}

		uint32_t a = m_H[0], b = m_H[1], c = m_H[2], d = m_H[3];
		uint32_t e = m_H[4], f = m_H[5], g = m_H[6], h = m_H[7];
		for (int i = 0; i < 64; i++)
		{
			uint32_t S1 = Rotr (e, 6) ^ Rotr (e, 11) ^ Rotr (e, 25);
			uint32_t ch = (e & f) ^ (~e & g);
			uint32_t t1 = h + S1 + ch + K[i] + W[i];
			uint32_t S0 = Rotr (a, 2) ^ Rotr (a, 13) ^ Rotr (a, 22);
			uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
			uint32_t t2 = S0 + mj;
			h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		m_H[0] += a; m_H[1] += b; m_H[2] += c; m_H[3] += d;
		m_H[4] += e; m_H[5] += f; m_H[6] += g; m_H[7] += h;
	}

	uint32_t m_H[8];
	uint64_t m_nLen;
	uint8_t m_Buf[64];
	size_t m_nBuf;
};

// HMAC-SHA256 streaming: Init (key), Update (data), Final (32 bytes)
class CHmacSha256
{
public:
	void Init (const uint8_t *pKey, size_t nKeyLen)
	{
		uint8_t K[64];
		for (int i = 0; i < 64; i++)
		{
			K[i] = 0;
		}
		if (nKeyLen > 64)
		{
			CSha256 H;
			H.Update (pKey, nKeyLen);
			H.Final (K);
		}
		else
		{
			for (size_t i = 0; i < nKeyLen; i++)
			{
				K[i] = pKey[i];
			}
		}

		uint8_t Ipad[64];
		for (int i = 0; i < 64; i++)
		{
			Ipad[i] = K[i] ^ 0x36;
			m_Opad[i] = K[i] ^ 0x5c;
		}
		m_Inner.Reset ();
		m_Inner.Update (Ipad, 64);
	}

	void Update (const uint8_t *pData, size_t nLen) { m_Inner.Update (pData, nLen); }

	void Final (uint8_t Out[32])
	{
		uint8_t InnerHash[32];
		m_Inner.Final (InnerHash);
		CSha256 Outer;
		Outer.Update (m_Opad, 64);
		Outer.Update (InnerHash, 32);
		Outer.Final (Out);
	}

private:
	CSha256 m_Inner;
	uint8_t m_Opad[64];
};

#endif
