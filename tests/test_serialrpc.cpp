// test_serialrpc.cpp - tests of the line protocol between the NAMberry and the ESP32 display (serialrpc.cpp)
// g++ -std=c++17 -Ihost_shim -I<circle>/include test_serialrpc.cpp serialrpc.cpp -o test_serialrpc
#include <cstdio>
#include <cstring>
#include <string>
#include "serialrpc.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static bool FeedLine (CSerialRpc &R, const std::string &s)
{
	bool done = false;
	for (char c : s) done = R.Feed (c) || done;
	return done;
}

int main ()
{
	unsigned id; boolean post; const char *path, *query;

	{	// a GET without form
		CSerialRpc R;
		CHECK (FeedLine (R, "Q 12 G /status\n"));
		CHECK (R.Parse (&id, &post, &path, &query));
		CHECK (id == 12 && !post && strcmp (path, "/status") == 0 && query[0] == 0);
	}
	{	// a POST with a form
		CSerialRpc R;
		CHECK (FeedLine (R, "Q 13 P /g op=set&id=3&k=drive&v=6.5\r\n"));
		CHECK (R.Parse (&id, &post, &path, &query));
		CHECK (id == 13 && post && strcmp (path, "/g") == 0 && strcmp (query, "op=set&id=3&k=drive&v=6.5") == 0);
	}
	{	// a GET with a query
		CSerialRpc R;
		CHECK (FeedLine (R, "Q 7 G /m b=20\n"));
		CHECK (R.Parse (&id, &post, &path, &query));
		CHECK (id == 7 && !post && strcmp (path, "/m") == 0 && strcmp (query, "b=20") == 0);
	}
	{	// test words are lines but not requests
		CSerialRpc R;
		CHECK (FeedLine (R, "ping 5\n"));
		CHECK (!R.Parse (&id, &post, &path, &query));
		CSerialRpc R2;
		CHECK (FeedLine (R2, "Qx\n"));
		CHECK (!R2.Parse (&id, &post, &path, &query));
	}
	{	// malformed requests
		const char *bad[] = {"Q  G /status", "Q 1 X /status", "Q 1 G status", "Q a G /status", "Q 1 G", "Q 1234567890 G /s", "Q 1G /s"};
		for (const char *b : bad)
		{
			CSerialRpc R;
			CHECK (FeedLine (R, std::string (b) + "\n"));
			CHECK (!R.Parse (&id, &post, &path, &query));
		}
	}
	{	// byte by byte, several lines in one stream, empty lines are ignored
		CSerialRpc R;
		std::string stream = "\n\nQ 1 G /a\nQ 2 G /b x=1\r\nping 3\n";
		int lines = 0, reqs = 0; unsigned last = 0;
		for (char c : stream)
		{
			if (R.Feed (c))
			{
				lines++;
				if (R.Parse (&id, &post, &path, &query)) { reqs++; last = id; }
			}
		}
		CHECK (lines == 3 && reqs == 2 && last == 2);
	}
	{	// a line that is too long is dropped as a whole and the next one works
		CSerialRpc R;
		std::string big = "Q 1 P /g " + std::string (SRPC_MAX_LINE + 50, 'x') + "\n";
		CHECK (!FeedLine (R, big));
		CHECK (FeedLine (R, "Q 2 G /status\n"));
		CHECK (R.Parse (&id, &post, &path, &query) && id == 2);
		// exactly at the limit is accepted
		CSerialRpc R3;
		std::string edge = "Q 1 P /g " + std::string (SRPC_MAX_LINE - 9, 'y');
		CHECK (edge.size () == SRPC_MAX_LINE);
		CHECK (FeedLine (R3, edge + "\n"));
		CHECK (R3.Parse (&id, &post, &path, &query));
	}
	{	// reply header
		char H[64];
		unsigned n = CSerialRpc::BuildHeader (H, sizeof H, 12, 200, 107234);
		CHECK (n == strlen ("A 12 200 107234\n") && strcmp (H, "A 12 200 107234\n") == 0);
		n = CSerialRpc::BuildHeader (H, sizeof H, 0, 400, 0);
		CHECK (strcmp (H, "A 0 400 0\n") == 0);
		char Small[8];
		CHECK (CSerialRpc::BuildHeader (Small, sizeof Small, 12, 200, 107234) == 0);
		n = CSerialRpc::BuildHeader (H, sizeof H, 4294967295u, 200, 1);
		CHECK (n > 0 && strcmp (H, "A 4294967295 200 1\n") == 0);
	}
	{	// the form of a POST
		char F[128];
		CHECK (CSerialRpc::BuildForm (F, sizeof F, "abc123def456", "op=set&id=3&k=drive&v=6.5"));
		CHECK (strcmp (F, "t=abc123def456&pg=1&op=set&id=3&k=drive&v=6.5") == 0);
		char Tiny[20];
		CHECK (!CSerialRpc::BuildForm (Tiny, sizeof Tiny, "abc123def456", "op=set&id=3&k=drive&v=6.5"));
		CHECK (CSerialRpc::BuildForm (F, sizeof F, "tok", ""));
		CHECK (strcmp (F, "t=tok&pg=1&") == 0);
	}

	if (fails == 0) printf ("RESULT: all tests passed\n"); else printf ("RESULT: %d FAILED\n", fails);
	return fails == 0 ? 0 : 1;
}
