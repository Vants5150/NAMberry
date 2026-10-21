//
// dhcpserver.cpp - see dhcpserver.h
//
#include "dhcpserver.h"
#include <circle/net/in.h>
#include <circle/logger.h>
#include <circle/util.h>

static const char FromDhcpS[] = "dhcps";

volatile unsigned g_nDhcpLeases = 0;

#define MSG_OFFER	2
#define MSG_ACK		5
#define DHCP_MIN_REPLY	300

CDhcpServer::CDhcpServer (CNetSubSystem *pNet, const u8 *pOwnIP, unsigned nFirst, unsigned nLast)
:	m_pNet (pNet),
	m_Core (pOwnIP, nFirst, nLast, 3600)
{
	SetName ("dhcps");
}

CDhcpServer::~CDhcpServer (void)
{
}

void CDhcpServer::Run (void)
{
	CSocket Socket (m_pNet, IPPROTO_UDP);
	if (Socket.Bind (67) < 0 || Socket.SetOptionBroadcast (TRUE) != 0)
	{
		CLogger::Get ()->Write (FromDhcpS, LogError, "port 67 not available");
		return;
	}
	CLogger::Get ()->Write (FromDhcpS, LogNotice, "DHCP server active");

	CIPAddress Broadcast;
	Broadcast.SetBroadcast ();
	static u8 Msg[FRAME_BUFFER_SIZE];
	static u8 Reply[DHCP_MIN_REPLY + 16];
	while (1)
	{
		CIPAddress From;
		u16 nPort = 0;
		const int nLen = Socket.ReceiveFrom (Msg, sizeof Msg, 0, &From, &nPort);
		if (nLen <= 0)
		{
			continue;
		}
		const unsigned nOut = m_Core.Handle (Msg, (unsigned) nLen, Reply, sizeof Reply);
		g_nDhcpLeases = m_Core.LeaseCount ();
		if (nOut > 0)
		{
			if (Socket.SendTo (Reply, nOut, 0, Broadcast, 68) < 0)
			{
				CLogger::Get ()->Write (FromDhcpS, LogWarning, "reply not sent");
			}
			else
			{
				CLogger::Get ()->Write (FromDhcpS, LogNotice, "reply %s to %02X:%02X:%02X:%02X:%02X:%02X",
							Reply[242] == MSG_OFFER ? "OFFER" : (Reply[242] == MSG_ACK ? "ACK" : "NAK"),
							Msg[28], Msg[29], Msg[30], Msg[31], Msg[32], Msg[33]);
			}
		}
	}
}
