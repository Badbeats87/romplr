//
// kernel.cpp
//
// WiFi TFTP/HTTP bootloader for Raspberry Pi 4
// Based on Circle sample 38-bootloader + hello_wlan
//
#include "kernel.h"
#include "httpbootserver.h"
#include "tftpbootserver.h"
#include <circle/chainboot.h>
#include <circle/sysconfig.h>
#include <circle/string.h>
#include <assert.h>

#define HTTP_BOOT_PORT		8080

#define DRIVE			"SD:"
#define FIRMWARE_PATH		DRIVE "/firmware/"
#define CONFIG_FILE		DRIVE "/wpa_supplicant.conf"

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_WLAN (FIRMWARE_PATH),
	m_Net (0, 0, 0, 0, DEFAULT_HOSTNAME, NetDeviceTypeWLAN),
	m_WPASupplicant (CONFIG_FILE)
{
	m_ActLED.Blink (5);
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}

		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		bOK = m_USBHCI.Initialize ();
	}

	if (bOK)
	{
		bOK = m_EMMC.Initialize ();
	}

	if (bOK)
	{
		if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK)
		{
			m_Logger.Write (FromKernel, LogError,
					"Cannot mount drive: %s", DRIVE);
			bOK = FALSE;
		}
	}

	if (bOK)
	{
		bOK = m_WLAN.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Net.Initialize (FALSE);
	}

	if (bOK)
	{
		bOK = m_WPASupplicant.Initialize ();
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);
	m_Logger.Write (FromKernel, LogNotice, "WiFi bootloader - waiting for network...");

	while (!m_Net.IsRunning ())
	{
		m_Scheduler.MsSleep (100);
	}

	CString IPString;
	m_Net.GetConfig ()->GetIPAddress ()->Format (&IPString);
	m_Logger.Write (FromKernel, LogNotice, "Open \"http://%s:%u/\" in your web browser!",
			(const char *) IPString, HTTP_BOOT_PORT);
	m_Logger.Write (FromKernel, LogNotice,
			"Try \"tftp -m binary %s -c put kernel8-rpi4.img\" from another computer!",
			(const char *) IPString);

	new CHTTPBootServer (&m_Net, HTTP_BOOT_PORT, KERNEL_MAX_SIZE + 2000);
	new CTFTPBootServer (&m_Net, KERNEL_MAX_SIZE);

	for (unsigned nCount = 0; !IsChainBootEnabled (); nCount++)
	{
		m_Screen.Rotor (0, nCount);

		m_Scheduler.Yield ();
	}

	m_Logger.Write (FromKernel, LogNotice, "Rebooting ...");

	m_Scheduler.Sleep (1);

	return ShutdownReboot;
}
