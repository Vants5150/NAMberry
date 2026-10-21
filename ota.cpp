//
// ota.cpp
//
#include "ota.h"
#include "sha256.h"
#include <circle/util.h>

#define KERNEL_FILE	"SD:/kernel_2712.img"
#define BACKUP_FILE	"SD:/kernel_2712.bak"
#define UPDATE_IMG	"SD:/update.img"
#define UPDATE_SIG	"SD:/update.sig"
#define KEY_FILE	"SD:/ota.key"

#define MIN_KERNEL_SIZE	(64 * 1024)
#define MAX_KERNEL_SIZE	(2 * 1024 * 1024)		// same limit as configure (--kernel-max-size 2)

static const char FromOta[] = "ota";

// "name.adn" (NAM model) or "name.air" (cabinet IR): 1..40 characters among letters, digits, _ and -, then the extension
static boolean IsModelName (const char *pName)
{
	unsigned nLen = strlen (pName);
	if (nLen < 5 || nLen > 44 || (strcmp (pName + nLen - 4, ".adn") != 0 && strcmp (pName + nLen - 4, ".air") != 0))
	{
		return FALSE;
	}
	for (unsigned i = 0; i + 4 < nLen; i++)
	{
		char c = pName[i];
		boolean bOk = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
		if (!bOk)
		{
			return FALSE;
		}
	}
	return TRUE;
}

COtaServer::COtaServer (CNetSubSystem *pNetSubSystem, FATFS *pFileSystem, const u8 *pAllowedIP)
:	CTFTPFatFsFileServer (pNetSubSystem, pFileSystem, "SD:/"),
	m_bPending (FALSE),
	m_bModelDone (FALSE)
{
	m_szModel[0] = 0;
	for (int i = 0; i < 4; i++)
	{
		m_Allowed[i] = pAllowedIP[i];
	}
}

COtaServer::~COtaServer (void)
{
}

boolean COtaServer::IsAccessAllowed (const CIPAddress *pForeignIP, const char *pFilename,
				     boolean bWriteRequest)
{
	if (!bWriteRequest)
	{
		return FALSE;		// no reading (otherwise wpa_supplicant.conf could be downloaded)
	}

	if (!(*pForeignIP == m_Allowed))
	{
		return FALSE;
	}

	return strcmp (pFilename, "update.img") == 0 || strcmp (pFilename, "update.sig") == 0 || IsModelName (pFilename);
}

boolean COtaServer::TakeModelFile (char *pName, unsigned nSize)
{
	if (!m_bModelDone)
	{
		return FALSE;
	}
	unsigned i = 0;
	for (; i + 1 < nSize && m_szModel[i]; i++)
	{
		pName[i] = m_szModel[i];
	}
	pName[i] = 0;
	m_bModelDone = FALSE;
	return TRUE;
}

void COtaServer::UpdateStatus (TStatus Status, const char *pFileName)
{
	if (Status != StatusWriteCompleted)
	{
		return;
	}
	if (strcmp (pFileName, "update.sig") == 0)
	{
		m_bPending = TRUE;
	}
	if (IsModelName (pFileName))
	{
		unsigned i = 0;
		for (; i + 1 < sizeof m_szModel && pFileName[i]; i++)
		{
			m_szModel[i] = pFileName[i];
		}
		m_szModel[i] = 0;
		m_bModelDone = TRUE;
	}
}

// reads a text file with n hexadecimal characters (spaces and newlines ignored) and converts them into bytes
static boolean ReadHexFile (const char *pPath, u8 *pOut, unsigned nBytes)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return FALSE;
	}

	char Buffer[200];
	UINT nRead = 0;
	FRESULT Result = f_read (&File, Buffer, sizeof Buffer, &nRead);
	f_close (&File);
	if (Result != FR_OK)
	{
		return FALSE;
	}

	unsigned nNibbles = 0;
	for (UINT i = 0; i < nRead; i++)
	{
		char c = Buffer[i];
		int v;
		if (c >= '0' && c <= '9')
		{
			v = c - '0';
		}
		else if (c >= 'a' && c <= 'f')
		{
			v = c - 'a' + 10;
		}
		else if (c >= 'A' && c <= 'F')
		{
			v = c - 'A' + 10;
		}
		else if (c == ' ' || c == '\r' || c == '\n' || c == '\t')
		{
			continue;
		}
		else
		{
			return FALSE;
		}

		if (nNibbles >= nBytes * 2)
		{
			return FALSE;		// too long
		}
		if (nNibbles % 2 == 0)
		{
			pOut[nNibbles / 2] = (u8) (v << 4);
		}
		else
		{
			pOut[nNibbles / 2] |= (u8) v;
		}
		nNibbles++;
	}

	return nNibbles == nBytes * 2;
}

static void RemoveUpdateFiles (void)
{
	f_unlink (UPDATE_IMG);
	f_unlink (UPDATE_SIG);
}

int OtaApply (CLogger *pLogger)
{
	u8 Key[32];
	u8 Sig[32];

	if (!ReadHexFile (KEY_FILE, Key, sizeof Key))
	{
		pLogger->Write (FromOta, LogError, "OTA: ota.key missing or invalid (64 hexadecimal characters)");
		RemoveUpdateFiles ();
		return -1;
	}

	if (!ReadHexFile (UPDATE_SIG, Sig, sizeof Sig))
	{
		pLogger->Write (FromOta, LogError, "OTA: update.sig missing or invalid");
		RemoveUpdateFiles ();
		return -2;
	}

	FIL File;
	if (f_open (&File, UPDATE_IMG, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		pLogger->Write (FromOta, LogError, "OTA: update.img missing");
		RemoveUpdateFiles ();
		return -3;
	}

	FSIZE_t nSize = f_size (&File);
	if (nSize < MIN_KERNEL_SIZE || nSize > MAX_KERNEL_SIZE)
	{
		pLogger->Write (FromOta, LogError, "OTA: implausible size (%u bytes)", (unsigned) nSize);
		f_close (&File);
		RemoveUpdateFiles ();
		return -4;
	}

	static u8 Buffer[8192];
	CHmacSha256 Hmac;
	Hmac.Init (Key, sizeof Key);

	FSIZE_t nTotal = 0;
	while (nTotal < nSize)
	{
		UINT nRead = 0;
		if (f_read (&File, Buffer, sizeof Buffer, &nRead) != FR_OK || nRead == 0)
		{
			pLogger->Write (FromOta, LogError, "OTA: read error on update.img");
			f_close (&File);
			RemoveUpdateFiles ();
			return -5;
		}

		Hmac.Update (Buffer, nRead);
		nTotal += nRead;
	}
	f_close (&File);

	u8 Computed[32];
	Hmac.Final (Computed);

	unsigned nDiff = 0;
	for (int i = 0; i < 32; i++)
	{
		nDiff |= (unsigned) (Computed[i] ^ Sig[i]);
	}
	if (nDiff != 0)
	{
		pLogger->Write (FromOta, LogError, "OTA: signature NOT valid, update refused");
		RemoveUpdateFiles ();
		return -6;
	}

	pLogger->Write (FromOta, LogNotice, "OTA: valid signature (%u bytes), replacing the kernel", (unsigned) nSize);

	f_unlink (BACKUP_FILE);
	if (f_rename (KERNEL_FILE, BACKUP_FILE) != FR_OK)
	{
		pLogger->Write (FromOta, LogError, "OTA: cannot save the current kernel as .bak");
		RemoveUpdateFiles ();
		return -7;
	}

	if (f_rename (UPDATE_IMG, KERNEL_FILE) != FR_OK)
	{
		pLogger->Write (FromOta, LogError, "OTA: cannot install the new kernel, restoring the old one");
		f_rename (BACKUP_FILE, KERNEL_FILE);
		RemoveUpdateFiles ();
		return -8;
	}

	f_unlink (UPDATE_SIG);

	// the new kernel starts with the unstable boot counter at zero
	FIL Counter;
	if (f_open (&Counter, "SD:/boot.cnt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		UINT nWritten = 0;
		f_write (&Counter, "0\n", 2, &nWritten);
		f_close (&Counter);
	}
	return 0;
}
