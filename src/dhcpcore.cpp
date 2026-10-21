//
// dhcpcore.cpp - see dhcpcore.h
//
#include "dhcpcore.h"
#include <circle/util.h>

#define DHCP_FIXED_SIZE		240		// BOOTP header up to and including the magic cookie
#define DHCP_MIN_REPLY		300

#define OPT_SUBNET	1
#define OPT_ROUTER	3
#define OPT_DNS		6
#define OPT_REQIP	50
#define OPT_LEASE	51
#define OPT_MSGTYPE	53
#define OPT_SERVERID	54
#define OPT_RENEWAL	58
#define OPT_REBIND	59
#define OPT_END		255

#define MSG_DISCOVER	1
#define MSG_OFFER	2
#define MSG_REQUEST	3
#define MSG_DECLINE	4
#define MSG_ACK		5
#define MSG_NAK		6
#define MSG_RELEASE	7

CDhcpCore::CDhcpCore (const u8 *pOwnIP, unsigned nFirst, unsigned nLast, unsigned nLeaseSeconds)
:	m_nFirst (nFirst),
	m_nLast (nLast),
	m_nLease (nLeaseSeconds)
{
	memcpy (m_Own, pOwnIP, 4);
	for (unsigned i = 0; i < DHCPS_MAX_LEASES; i++)
	{
		m_Lease[i].Used = FALSE;
	}
}

unsigned CDhcpCore::LeaseCount (void) const
{
	unsigned n = 0;
	for (unsigned i = 0; i < DHCPS_MAX_LEASES; i++)
	{
		if (m_Lease[i].Used)
		{
			n++;
		}
	}
	return n;
}

boolean CDhcpCore::LeaseOf (unsigned n, u8 *pMac, u8 *pHost) const
{
	unsigned k = 0;
	for (unsigned i = 0; i < DHCPS_MAX_LEASES; i++)
	{
		if (m_Lease[i].Used && k++ == n)
		{
			memcpy (pMac, m_Lease[i].Mac, 6);
			*pHost = m_Lease[i].Host;
			return TRUE;
		}
	}
	return FALSE;
}

int CDhcpCore::FindLease (const u8 *pMac) const
{
	for (unsigned i = 0; i < DHCPS_MAX_LEASES; i++)
	{
		if (m_Lease[i].Used && memcmp (m_Lease[i].Mac, pMac, 6) == 0)
		{
			return (int) i;
		}
	}
	return -1;
}

// assigns the MAC the first free address of the pool (or the one it already had)
int CDhcpCore::AllocLease (const u8 *pMac)
{
	const int nOld = FindLease (pMac);
	if (nOld >= 0)
	{
		return nOld;
	}
	for (unsigned host = m_nFirst; host <= m_nLast; host++)
	{
		boolean bTaken = FALSE;
		for (unsigned i = 0; i < DHCPS_MAX_LEASES; i++)
		{
			if (m_Lease[i].Used && m_Lease[i].Host == host)
			{
				bTaken = TRUE;
				break;
			}
		}
		if (bTaken || host == m_Own[3])
		{
			continue;
		}
		for (unsigned i = 0; i < DHCPS_MAX_LEASES; i++)
		{
			if (!m_Lease[i].Used)
			{
				m_Lease[i].Used = TRUE;
				memcpy (m_Lease[i].Mac, pMac, 6);
				m_Lease[i].Host = (u8) host;
				return (int) i;
			}
		}
		return -1;
	}
	return -1;
}

static unsigned PutOpt (u8 *p, unsigned n, u8 nCode, const u8 *pVal, unsigned nLen)
{
	p[n++] = nCode;
	p[n++] = (u8) nLen;
	for (unsigned i = 0; i < nLen; i++)
	{
		p[n++] = pVal[i];
	}
	return n;
}

static void Be32 (u8 *p, u32 v)
{
	p[0] = (u8) (v >> 24);
	p[1] = (u8) (v >> 16);
	p[2] = (u8) (v >> 8);
	p[3] = (u8) v;
}

unsigned CDhcpCore::Handle (const u8 *pMsg, unsigned nLength, u8 *pReply, unsigned nReplySize)
{
	if (nLength < DHCP_FIXED_SIZE + 3 || nReplySize < DHCP_MIN_REPLY + 16)
	{
		return 0;
	}
	if (pMsg[0] != 1 || pMsg[1] != 1 || pMsg[2] != 6)		// BOOTREQUEST, Ethernet, MAC di 6 byte
	{
		return 0;
	}
	if (pMsg[236] != 0x63 || pMsg[237] != 0x82 || pMsg[238] != 0x53 || pMsg[239] != 0x63)
	{
		return 0;
	}

	// options
	unsigned nType = 0;
	const u8 *pReqIp = 0;
	const u8 *pServerId = 0;
	for (unsigned i = DHCP_FIXED_SIZE; i < nLength;)
	{
		const u8 c = pMsg[i++];
		if (c == 0)
		{
			continue;
		}
		if (c == OPT_END || i >= nLength)
		{
			break;
		}
		const unsigned l = pMsg[i++];
		if (i + l > nLength)
		{
			break;
		}
		if (c == OPT_MSGTYPE && l == 1)
		{
			nType = pMsg[i];
		}
		else if (c == OPT_REQIP && l == 4)
		{
			pReqIp = pMsg + i;
		}
		else if (c == OPT_SERVERID && l == 4)
		{
			pServerId = pMsg + i;
		}
		i += l;
	}

	const u8 *pMac = pMsg + 28;
	unsigned nReplyType = 0;
	int nLease = -1;
	if (nType == MSG_DISCOVER)
	{
		nLease = AllocLease (pMac);
		if (nLease < 0)
		{
			return 0;			// pool exhausted
		}
		nReplyType = MSG_OFFER;
	}
	else if (nType == MSG_REQUEST)
	{
		if (pServerId != 0 && memcmp (pServerId, m_Own, 4) != 0)
		{
			return 0;			// the client chose another server
		}
		const u8 *pWant = pReqIp != 0 ? pReqIp : pMsg + 12;		// option 50, otherwise ciaddr (renewal)
		const boolean bSameNet = memcmp (pWant, m_Own, 3) == 0;
		if (!bSameNet)
		{
			nReplyType = MSG_NAK;
		}
		else
		{
			nLease = FindLease (pMac);
			if (nLease < 0 && pWant[3] >= m_nFirst && pWant[3] <= m_nLast && pWant[3] != m_Own[3])
			{
				// the client remembers an address given before a reboot: we recognize it if it is free
				boolean bTaken = FALSE;
				for (unsigned i = 0; i < DHCPS_MAX_LEASES; i++)
				{
					if (m_Lease[i].Used && m_Lease[i].Host == pWant[3])
					{
						bTaken = TRUE;
					}
				}
				if (!bTaken)
				{
					for (unsigned i = 0; i < DHCPS_MAX_LEASES; i++)
					{
						if (!m_Lease[i].Used)
						{
							m_Lease[i].Used = TRUE;
							memcpy (m_Lease[i].Mac, pMac, 6);
							m_Lease[i].Host = pWant[3];
							nLease = (int) i;
							break;
						}
					}
				}
			}
			if (nLease >= 0 && m_Lease[nLease].Host == pWant[3])
			{
				nReplyType = MSG_ACK;
			}
			else
			{
				nReplyType = MSG_NAK;
			}
		}
	}
	else if (nType == MSG_RELEASE)
	{
		const int n = FindLease (pMac);
		if (n >= 0)
		{
			m_Lease[n].Used = FALSE;
		}
		return 0;
	}
	else
	{
		return 0;
	}

	memset (pReply, 0, DHCP_MIN_REPLY + 16);
	pReply[0] = 2;					// BOOTREPLY
	pReply[1] = 1;
	pReply[2] = 6;
	memcpy (pReply + 4, pMsg + 4, 4);		// xid
	memcpy (pReply + 10, pMsg + 10, 2);		// flags
	if (nReplyType != MSG_NAK && nLease >= 0)
	{
		memcpy (pReply + 16, m_Own, 3);
		pReply[19] = m_Lease[nLease].Host;	// yiaddr
	}
	memcpy (pReply + 20, m_Own, 4);			// siaddr
	memcpy (pReply + 24, pMsg + 24, 4);		// giaddr
	memcpy (pReply + 28, pMsg + 28, 16);		// chaddr
	pReply[236] = 0x63;
	pReply[237] = 0x82;
	pReply[238] = 0x53;
	pReply[239] = 0x63;

	unsigned n = DHCP_FIXED_SIZE;
	const u8 t = (u8) nReplyType;
	n = PutOpt (pReply, n, OPT_MSGTYPE, &t, 1);
	n = PutOpt (pReply, n, OPT_SERVERID, m_Own, 4);
	if (nReplyType != MSG_NAK)
	{
		u8 v[4];
		Be32 (v, m_nLease);
		n = PutOpt (pReply, n, OPT_LEASE, v, 4);
		Be32 (v, m_nLease / 2);
		n = PutOpt (pReply, n, OPT_RENEWAL, v, 4);
		Be32 (v, m_nLease * 7 / 8);
		n = PutOpt (pReply, n, OPT_REBIND, v, 4);
		static const u8 Mask[4] = {255, 255, 255, 0};
		n = PutOpt (pReply, n, OPT_SUBNET, Mask, 4);
		n = PutOpt (pReply, n, OPT_ROUTER, m_Own, 4);
		n = PutOpt (pReply, n, OPT_DNS, m_Own, 4);
	}
	pReply[n++] = OPT_END;
	return n < DHCP_MIN_REPLY ? DHCP_MIN_REPLY : n;
}

