//
// ota.h - kernel update over Wi-Fi (TFTP + HMAC-SHA256 signature)
//
// Protocol (PC/rpi5-nvme side: ota_push.py):
//   1. writes "update.img" (the new kernel) via TFTP
//   2. writes "update.sig" (64 hexadecimal characters = HMAC-SHA256 of the kernel with the key in SD:/ota.key)
// The server refuses: every read, every other file name and every sender other than OTA_ALLOWED_IP.
// When update.sig has been written in full, the main kernel calls OtaApply().
//
#ifndef _ota_h
#define _ota_h

#include <tftpfileserver/tftpfatfsfileserver.h>
#include <circle/logger.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/ipaddress.h>
#include <fatfs/ff.h>
#include <circle/types.h>

class COtaServer : public CTFTPFatFsFileServer
{
public:
	COtaServer (CNetSubSystem *pNetSubSystem, FATFS *pFileSystem, const u8 *pAllowedIP);
	~COtaServer (void);

	boolean IsAccessAllowed (const CIPAddress *pForeignIP, const char *pFilename,
				 boolean bWriteRequest) override;
	void UpdateStatus (TStatus Status, const char *pFileName) override;

	boolean Pending (void) const { return m_bPending; }
	void ClearPending (void) { m_bPending = FALSE; }

	// NAM model files: "name.adn" (letters, digits, _ and -, up to 40 characters; write-only, only from the allowed
	// address; integrity is verified by the CRC32 of the .adn format). If a file has been written in full it copies the
	// name into pName and returns TRUE (one at a time).
	boolean TakeModelFile (char *pName, unsigned nSize);

private:
	u8 m_Allowed[4];
	volatile boolean m_bPending;
	char m_szModel[48];
	volatile boolean m_bModelDone;
};

// Verifies update.img with update.sig and ota.key; if the signature is valid it replaces kernel_2712.img
// (the old one stays as kernel_2712.bak). Returns 0 = applied; <0 = refused (code in the log).
// In any case update.img and update.sig are deleted if not applied.
int OtaApply (CLogger *pLogger);

#endif
