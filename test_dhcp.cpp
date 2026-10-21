// test_dhcp.cpp - test of the fallback network's DHCP server (logic part only), on Linux without hardware.
// g++ -std=c++17 -O1 -fsanitize=address,undefined -I<circle>/include -I. test_dhcp.cpp dhcpcore.cpp -o test_dhcp
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "dhcpcore.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const u8 OWN[4] = {192, 168, 4, 1};

// client message: type (1 discover, 3 request), final MAC, optional address request and server id
static unsigned Msg (u8 *p, unsigned nType, u8 nMacLast, const u8 *pReq, const u8 *pServer, const u8 *pCi = 0)
{
	memset (p, 0, 400);
	p[0] = 1; p[1] = 1; p[2] = 6;
	p[4] = 0x12; p[5] = 0x34; p[6] = 0x56; p[7] = 0x78;
	p[28] = 0xAA; p[29] = 0xBB; p[30] = 0xCC; p[31] = 0xDD; p[32] = 0xEE; p[33] = nMacLast;
	if (pCi)
	{
		memcpy (p + 12, pCi, 4);
	}
	p[236] = 0x63; p[237] = 0x82; p[238] = 0x53; p[239] = 0x63;
	unsigned n = 240;
	p[n++] = 53; p[n++] = 1; p[n++] = (u8) nType;
	if (pReq) { p[n++] = 50; p[n++] = 4; memcpy (p + n, pReq, 4); n += 4; }
	if (pServer) { p[n++] = 54; p[n++] = 4; memcpy (p + n, pServer, 4); n += 4; }
	p[n++] = 255;
	return n;
}

static int Opt (const u8 *r, unsigned len, u8 code, const u8 **pv)
{
	for (unsigned i = 240; i + 1 < len;)
	{
		if (r[i] == 255) break;
		if (r[i] == 0) { i++; continue; }
		const unsigned l = r[i + 1];
		if (r[i] == code) { *pv = r + i + 2; return (int) l; }
		i += 2 + l;
	}
	return -1;
}

int main ()
{
	CDhcpCore S (OWN, 10, 12, 3600);
	u8 m[400], r[400];
	const u8 *v;

	// DISCOVER -> OFFER .10
	unsigned n = Msg (m, 1, 1, 0, 0);
	unsigned rl = S.Handle (m, n, r, sizeof r);
	CHECK (rl >= 300 && r[0] == 2 && Opt (r, rl, 53, &v) == 1 && v[0] == 2);
	CHECK (r[16] == 192 && r[17] == 168 && r[18] == 4 && r[19] == 10);
	CHECK (r[4] == 0x12 && r[7] == 0x78 && r[33] == 1 && r[28] == 0xAA);			// xid and MAC repeated
	CHECK (Opt (r, rl, 54, &v) == 4 && memcmp (v, OWN, 4) == 0);
	CHECK (Opt (r, rl, 3, &v) == 4 && memcmp (v, OWN, 4) == 0);
	CHECK (Opt (r, rl, 1, &v) == 4 && v[0] == 255 && v[3] == 0);
	CHECK (Opt (r, rl, 51, &v) == 4 && v[2] == 0x0E && v[3] == 0x10);			// 3600 s

	// REQUEST of the offered address -> ACK
	const u8 Ip10[4] = {192, 168, 4, 10};
	n = Msg (m, 3, 1, Ip10, OWN);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (rl >= 300 && Opt (r, rl, 53, &v) == 1 && v[0] == 5 && r[19] == 10);
	CHECK (S.LeaseCount () == 1);

	// same client again: same address; second client: .11
	n = Msg (m, 1, 1, 0, 0);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (r[19] == 10);
	n = Msg (m, 1, 2, 0, 0);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (r[19] == 11);
	n = Msg (m, 3, 2, 0, 0, 0);
	const u8 Ip11[4] = {192, 168, 4, 11};
	n = Msg (m, 3, 2, Ip11, 0);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (Opt (r, rl, 53, &v) == 1 && v[0] == 5 && r[19] == 11);

	// renewal: ciaddr without option 50
	n = Msg (m, 3, 2, 0, 0, Ip11);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (Opt (r, rl, 53, &v) == 1 && v[0] == 5);

	// address of another network -> NAK; address of another client -> NAK
	const u8 Foreign[4] = {192, 168, 1, 50};
	n = Msg (m, 3, 3, Foreign, 0);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (rl > 0 && Opt (r, rl, 53, &v) == 1 && v[0] == 6 && r[19] == 0);
	n = Msg (m, 3, 3, Ip10, 0);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (rl > 0 && Opt (r, rl, 53, &v) == 1 && v[0] == 6);

	// the client chose another server: no reply
	const u8 Other[4] = {192, 168, 4, 99};
	n = Msg (m, 3, 1, Ip10, Other);
	CHECK (S.Handle (m, n, r, sizeof r) == 0);

	// board reboot: a client remembers .12 (free) and requests it without DISCOVER
	const u8 Ip12[4] = {192, 168, 4, 12};
	n = Msg (m, 3, 4, Ip12, 0);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (Opt (r, rl, 53, &v) == 1 && v[0] == 5 && r[19] == 12);
	// pool full: DISCOVER of a fourth client with no reply
	n = Msg (m, 1, 5, 0, 0);
	CHECK (S.Handle (m, n, r, sizeof r) == 0);
	// RELEASE frees the address
	n = Msg (m, 7, 4, 0, 0);
	CHECK (S.Handle (m, n, r, sizeof r) == 0);
	n = Msg (m, 1, 5, 0, 0);
	rl = S.Handle (m, n, r, sizeof r);
	CHECK (rl > 0 && r[19] == 12);

	// malformed messages: no reply and no error (tested with the sanitizers)
	CHECK (S.Handle (m, 10, r, sizeof r) == 0);
	n = Msg (m, 1, 6, 0, 0);
	m[0] = 2;
	CHECK (S.Handle (m, n, r, sizeof r) == 0);					// not a request
	n = Msg (m, 1, 6, 0, 0);
	m[236] = 0;
	CHECK (S.Handle (m, n, r, sizeof r) == 0);					// wrong cookie
	n = Msg (m, 1, 6, 0, 0);
	CHECK (S.Handle (m, n, r, 100) == 0);						// reply buffer too small
	unsigned long long seed = 7;
	for (int i = 0; i < 200000; i++)
	{
		n = Msg (m, 1 + (unsigned) (seed >> 60) % 8, (u8) (seed >> 33), 0, 0);
		seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
		const unsigned cut = 230 + (unsigned) (seed >> 40) % 60;
		for (int k = 0; k < 3; k++)
		{
			seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
			m[240 + (seed >> 40) % 12] = (u8) (seed >> 20);
		}
		S.Handle (m, cut < n ? cut : n, r, sizeof r);
	}
	printf (fails ? "\nRESULT: %d tests failed\n" : "\nRESULT: all tests passed\n", fails);
	return fails ? 1 : 0;
}
