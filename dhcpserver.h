//
// dhcpserver.h - small DHCP server (RFC 2131) for the fallback network: assigns an address to the devices that connect to the board's access
// point. Circle does not have one. A single server, a single /24 subnet, fixed addresses per MAC as long as the board stays on.
//
#ifndef _dhcpserver_h
#define _dhcpserver_h

#include <circle/sched/task.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/net/ipaddress.h>
#include <circle/types.h>
#include "dhcpcore.h"

extern volatile unsigned g_nDhcpLeases;		// how many devices have received an address (for the reboot of the fallback network without clients)

class CDhcpServer : public CTask
{
public:
	CDhcpServer (CNetSubSystem *pNet, const u8 *pOwnIP, unsigned nFirst, unsigned nLast);
	~CDhcpServer (void);

	void Run (void) override;

private:
	CNetSubSystem *m_pNet;
	CDhcpCore m_Core;
};

#endif
