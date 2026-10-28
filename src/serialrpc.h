//
// serialrpc.h - line protocol between the NAMberry and the ESP32 touch display over the USB serial (CDC) link ("the web API over a cable")
//
// Request (display -> NAMberry), one line:   Q <id> <G|P> <path> [<form>]\n
//     Q 12 G /status\n
//     Q 13 P /g op=set&id=3&k=drive&v=6.5\n          (the NAMberry adds the session token and the page flag to a P form)
//     Q 14 G /m b=20\n                                (a G with a form: the part after the '?' of the web request)
// Reply (NAMberry -> display):               A <id> <status> <length>\n<length bytes of body>
// <status> is the HTTP status of the web handler (200 = ok). The body is what the web page would receive (JSON for the API).
// Lines that do not start with "Q " are not requests (the link test words "ping N" and so on).
// No hardware dependency: it is tested on the PC (test_serialrpc.cpp).
//
#ifndef _serialrpc_h
#define _serialrpc_h

#include <circle/types.h>

#define SRPC_MAX_LINE	480

class CSerialRpc
{
public:
	CSerialRpc (void);

	// feeds one received byte; returns TRUE when a line is complete (then Line() is valid until the next Feed)
	boolean Feed (char c);
	const char *Line (void) const { return m_Line; }

	// parses the line completed by Feed() as a request. The strings point into the line (the separators are replaced by 0).
	// Returns FALSE if the line is not a well formed request (then it is a test word or garbage)
	boolean Parse (unsigned *pId, boolean *pPost, const char **ppPath, const char **ppQuery);

	// "A <id> <status> <length>\n"; returns the length of the header (0 if it does not fit)
	static unsigned BuildHeader (char *pOut, unsigned nSize, unsigned nId, unsigned nStatus, unsigned nLength);

	// the form of a POST as the web handlers want it: "t=<token>&pg=1&<form>"; returns FALSE if it does not fit
	static boolean BuildForm (char *pOut, unsigned nSize, const char *pToken, const char *pForm);

private:
	char m_Line[SRPC_MAX_LINE + 1];
	unsigned m_nLen;
	boolean m_bOverflow;
};

#endif
