// Test of sha256.h on the host with known test vectors (FIPS 180-4 and RFC 4231). usage: sha256_test
#include <cstdio>
#include <cstring>
#include <string>
#include "sha256.h"

static std::string Hex (const uint8_t *p, size_t n)
{
	static const char *d = "0123456789abcdef";
	std::string s;
	for (size_t i = 0; i < n; i++)
	{
		s += d[p[i] >> 4];
		s += d[p[i] & 15];
	}
	return s;
}

static int g_fail = 0;
static void Check (const char *what, const std::string &got, const char *want)
{
	bool ok = got == want;
	std::printf("%-46s %s\n", what, ok ? "OK" : "FAILED");
	if (!ok)
	{
		std::printf("   expected %s\n   got %s\n", want, got.c_str());
		g_fail++;
	}
}

int main ()
{
	uint8_t out[32];

	CSha256 a;
	a.Update ((const uint8_t *) "abc", 3);
	a.Final (out);
	Check ("SHA-256(\"abc\")", Hex (out, 32), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

	CSha256 e;
	e.Final (out);
	Check ("SHA-256(\"\")", Hex (out, 32), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

	CSha256 l;
	const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	l.Update ((const uint8_t *) two, strlen (two));
	l.Final (out);
	Check ("SHA-256 (448 bits, two blocks)", Hex (out, 32), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	// a million 'a', in pieces of irregular size (tests the streaming)
	CSha256 m;
	uint8_t chunk[1000];
	memset (chunk, 'a', sizeof chunk);
	for (int i = 0; i < 1000; i++)
	{
		m.Update (chunk, i % 2 ? 1000 : 1000);
	}
	m.Final (out);
	Check ("SHA-256 (1,000,000 x 'a')", Hex (out, 32), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

	// same message in skewed pieces
	CSha256 s;
	const char *msg = "The quick brown fox jumps over the lazy dog";
	for (size_t i = 0; i < strlen (msg); i += 7)
	{
		size_t n = strlen (msg) - i < 7 ? strlen (msg) - i : 7;
		s.Update ((const uint8_t *) msg + i, n);
	}
	s.Final (out);
	Check ("SHA-256 in 7-byte pieces", Hex (out, 32), "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

	// RFC 4231, case 1
	uint8_t key1[20];
	memset (key1, 0x0b, sizeof key1);
	CHmacSha256 h1;
	h1.Init (key1, sizeof key1);
	h1.Update ((const uint8_t *) "Hi There", 8);
	h1.Final (out);
	Check ("HMAC-SHA256 RFC 4231 case 1", Hex (out, 32), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

	// RFC 4231, case 2
	CHmacSha256 h2;
	h2.Init ((const uint8_t *) "Jefe", 4);
	h2.Update ((const uint8_t *) "what do ya want ", 16);
	h2.Update ((const uint8_t *) "for nothing?", 12);
	h2.Final (out);
	Check ("HMAC-SHA256 RFC 4231 case 2 (streaming)", Hex (out, 32), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

	// RFC 4231, case 6: 131-byte key (longer than the block)
	uint8_t key6[131];
	memset (key6, 0xaa, sizeof key6);
	CHmacSha256 h6;
	h6.Init (key6, sizeof key6);
	const char *m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
	h6.Update ((const uint8_t *) m6, strlen (m6));
	h6.Final (out);
	Check ("HMAC-SHA256 RFC 4231 case 6 (long key)", Hex (out, 32), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

	std::printf(g_fail ? "\nFALLITI: %d\n" : "\nALL OK\n", g_fail);
	return g_fail ? 1 : 0;
}
