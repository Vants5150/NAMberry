//
// serialrpc.cpp - line protocol between the NAMberry and the ESP32 touch display over the USB serial link (see serialrpc.h)
//
#include "serialrpc.h"
#include <string.h>

CSerialRpc::CSerialRpc (void)
:	m_nLen (0),
	m_bOverflow (FALSE)
{
	m_Line[0] = 0;
}

boolean CSerialRpc::Feed (char c)
{
	if (c == '\n' || c == '\r')
	{
		if (m_nLen == 0 && !m_bOverflow)
		{
			return FALSE;	// an empty line (the second character of "\r\n"): the line before stays valid
		}
		const boolean bOk = m_nLen > 0 && !m_bOverflow;
		const boolean bHad = m_nLen > 0 || m_bOverflow;
		m_Line[m_nLen] = 0;
		m_nLen = 0;
		m_bOverflow = FALSE;
		if (bHad && !bOk)
		{
			m_Line[0] = 0;		// a line that did not fit is dropped as a whole
		}
		return bOk;
	}
	if (m_nLen < SRPC_MAX_LINE)
	{
		m_Line[m_nLen++] = c;
	}
	else
	{
		m_bOverflow = TRUE;
	}
	return FALSE;
}

boolean CSerialRpc::Parse (unsigned *pId, boolean *pPost, const char **ppPath, const char **ppQuery)
{
	char *p = m_Line;
	if (p[0] != 'Q' || p[1] != ' ')
	{
		return FALSE;
	}
	p += 2;

	// id: decimal digits
	if (*p < '0' || *p > '9')
	{
		return FALSE;
	}
	unsigned nId = 0;
	unsigned nDigits = 0;
	while (*p >= '0' && *p <= '9')
	{
		if (++nDigits > 9)
		{
			return FALSE;
		}
		nId = nId * 10 + (unsigned) (*p - '0');
		p++;
	}
	if (*p != ' ')
	{
		return FALSE;
	}
	p++;

	// method
	if ((*p != 'G' && *p != 'P') || p[1] != ' ')
	{
		return FALSE;
	}
	const boolean bPost = *p == 'P';
	p += 2;

	// path, then an optional form/query after one space
	if (*p != '/')
	{
		return FALSE;
	}
	char *pPath = p;
	while (*p != 0 && *p != ' ')
	{
		p++;
	}
	char *pQuery = p;			// points at the terminator if there is none
	if (*p == ' ')
	{
		*p = 0;
		pQuery = p + 1;
	}

	*pId = nId;
	*pPost = bPost;
	*ppPath = pPath;
	*ppQuery = pQuery;
	return TRUE;
}

unsigned CSerialRpc::BuildHeader (char *pOut, unsigned nSize, unsigned nId, unsigned nStatus, unsigned nLength)
{
	// "A " id " " status " " length "\n", written by hand (no printf in this module)
	char T[48];
	unsigned n = 0;
	T[n++] = 'A';
	T[n++] = ' ';
	const unsigned v[3] = {nId, nStatus, nLength};
	for (unsigned k = 0; k < 3; k++)
	{
		char D[12];
		unsigned nd = 0;
		unsigned x = v[k];
		do
		{
			D[nd++] = (char) ('0' + x % 10);
			x /= 10;
		}
		while (x != 0);
		while (nd > 0)
		{
			T[n++] = D[--nd];
		}
		T[n++] = k < 2 ? ' ' : '\n';
	}
	if (n + 1 > nSize)
	{
		return 0;
	}
	memcpy (pOut, T, n);
	pOut[n] = 0;
	return n;
}

boolean CSerialRpc::BuildForm (char *pOut, unsigned nSize, const char *pToken, const char *pForm)
{
	const char Pre[] = "t=";
	const char Mid[] = "&pg=1&";
	const unsigned nNeed = (unsigned) (sizeof Pre - 1 + strlen (pToken) + sizeof Mid - 1 + strlen (pForm) + 1);
	if (nNeed > nSize)
	{
		return FALSE;
	}
	unsigned n = 0;
	memcpy (pOut + n, Pre, sizeof Pre - 1);
	n += sizeof Pre - 1;
	memcpy (pOut + n, pToken, strlen (pToken));
	n += (unsigned) strlen (pToken);
	memcpy (pOut + n, Mid, sizeof Mid - 1);
	n += sizeof Mid - 1;
	memcpy (pOut + n, pForm, strlen (pForm) + 1);
	return TRUE;
}
