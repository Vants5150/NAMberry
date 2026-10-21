//
// dhcpcore.h - logic part of the fallback network's DHCP server (no sockets, so it can be tested on the PC)
//
#ifndef _dhcpcore_h
#define _dhcpcore_h

#include <circle/types.h>

#define DHCPS_MAX_LEASES	24

// Logic part, without sockets: takes a received message and prepares the reply (also tested on the PC).
class CDhcpCore
{
public:
	// own = address of the board (a.b.c.d, /24 network); the pool is a.b.c.nFirst .. a.b.c.nLast
	CDhcpCore (const u8 *pOwnIP, unsigned nFirst, unsigned nLast, unsigned nLeaseSeconds);

	// returns the length of the reply in pReply (>= 300 bytes) or 0 if the message needs no reply
	unsigned Handle (const u8 *pMsg, unsigned nLength, u8 *pReply, unsigned nReplySize);

	unsigned LeaseCount (void) const;
	boolean LeaseOf (unsigned n, u8 *pMac, u8 *pHost) const;

private:
	int FindLease (const u8 *pMac) const;
	int AllocLease (const u8 *pMac);

	u8 m_Own[4];
	unsigned m_nFirst, m_nLast, m_nLease;
	struct TLease
	{
		boolean Used;
		u8 Mac[6];
		u8 Host;
	} m_Lease[DHCPS_MAX_LEASES];
};

#endif
