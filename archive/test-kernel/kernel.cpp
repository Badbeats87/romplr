#include "kernel.h"
#include <circle/net/syslogdaemon.h>
#include <circle/net/ipaddress.h>
#include <circle/string.h>

#define DRIVE		"SD:"
#define FIRMWARE_PATH	DRIVE "/firmware/"
#define CONFIG_FILE	DRIVE "/wpa_supplicant.conf"

// Your Mac's IP - syslog messages go here on port 8514
static const u8 SysLogServer[] = {192, 168, 0, 63};
static const u16 usServerPort = 8514;

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

	if (bOK) bOK = m_Screen.Initialize ();
	if (bOK) bOK = m_Serial.Initialize (115200);

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0) pTarget = &m_Screen;
		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK) bOK = m_Interrupt.Initialize ();
	if (bOK) bOK = m_Timer.Initialize ();
	if (bOK) bOK = m_USBHCI.Initialize ();
	if (bOK) bOK = m_EMMC.Initialize ();

	if (bOK)
	{
		if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK)
		{
			m_Logger.Write (FromKernel, LogError, "Cannot mount drive: %s", DRIVE);
			bOK = FALSE;
		}
	}

	if (bOK) bOK = m_WLAN.Initialize ();
	if (bOK) bOK = m_Net.Initialize (FALSE);
	if (bOK) bOK = m_WPASupplicant.Initialize ();

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "WiFi test kernel - compile time: " __DATE__ " " __TIME__);
	m_Logger.Write (FromKernel, LogNotice, "Waiting for WiFi...");

	while (!m_Net.IsRunning ())
	{
		m_Scheduler.MsSleep (100);
	}

	CString IPString;
	m_Net.GetConfig ()->GetIPAddress ()->Format (&IPString);
	m_Logger.Write (FromKernel, LogNotice, "IP address: %s", (const char *) IPString);

	// Start syslog daemon - sends all log output to Mac via UDP
	new CSysLogDaemon (&m_Net, CIPAddress (SysLogServer), usServerPort);
	m_Scheduler.MsSleep (500);

	m_Logger.Write (FromKernel, LogNotice, "Syslog started - sending to 192.168.0.63:%u", usServerPort);
	m_Logger.Write (FromKernel, LogNotice, "Hello from Pi over WiFi!");

	for (unsigned i = 0; ; i++)
	{
		m_Logger.Write (FromKernel, LogNotice, "Heartbeat %u", i);
		m_ActLED.Blink (1);
		m_Scheduler.Sleep (5);
	}

	return ShutdownHalt;
}
