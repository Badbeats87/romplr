//
// kernel.cpp
//
// Minimal rompler kernel — HDMI + audio + USB + SD card.
//

#include "kernel.h"
#include <circle/synchronize.h>
#include <circle/memory.h>
#include <circle/string.h>
#include <assert.h>

// C-callable high-memory allocator for sample pool
extern "C" void *alloc_high_mem (size_t nSize)
{
	return CMemorySystem::HeapAllocate (nSize, HEAP_HIGH);
}

// C-callable MIDI poll callback — called from sample_bank during SD reads
extern "C" void midi_poll_callback (void)
{
	CKernel *pKernel = CKernel::Get ();
	if (pKernel)
		pKernel->PollMIDI ();
}

#define DRIVE		"SD:"

static const char FromKernel[] = "kernel";

// ─── CSynthSound ─────────────────────────────────────────────

CSynthSound::CSynthSound (CInterruptSystem *pInterrupt)
:	CPWMSoundBaseDevice (pInterrupt, SAMPLE_RATE, CHUNK_SIZE),
	m_Rompler ()
{
	rompler_init (&m_Rompler);
}

void CSynthSound::NoteOn (u8 ucNote, u8 ucVelocity)
{
	m_Lock.Acquire ();
	voice_note_on (&m_Rompler.voices,
		       &m_Rompler.patches[m_Rompler.current_patch],
		       ucNote, ucVelocity);
	m_Lock.Release ();
}

void CSynthSound::NoteOff (u8 ucNote)
{
	m_Lock.Acquire ();
	voice_note_off (&m_Rompler.voices, ucNote);
	m_Lock.Release ();
}

void CSynthSound::MIDIByte (u8 ucByte)
{
	m_Lock.Acquire ();
	rompler_midi (&m_Rompler, ucByte);
	m_Lock.Release ();
}

unsigned CSynthSound::GetInstrumentCount (void) const
{
	return (unsigned) m_Rompler.sample_bank.num_instruments;
}

unsigned CSynthSound::GetSamplePoolUsed (void) const
{
	return (unsigned) m_Rompler.sample_bank.pool_used;
}

unsigned CSynthSound::GetChunk (u32 *pBuffer, unsigned nChunkSize)
{
	unsigned nFrames = nChunkSize / 2;
	unsigned nRange  = GetRange ();
	unsigned nHalf   = nRange / 2;

	m_Lock.Acquire ();

	for (unsigned f = 0; f < nFrames; f++)
	{
		int32_t l = 0;
		int32_t r = 0;
		rompler_render (&m_Rompler, &l, &r);

		/* Soft-clip via Bhaskara tanh approximation (matches Linux version).
		 * Scale Q15 input to ±3.0 range: s = sample / 10922 (≈32767/3).
		 * tanh_approx(s) = s*(27+s²) / (27+9*s²), output in ±1.0 range.
		 * Use 64-bit to avoid overflow. */
		for (int ch = 0; ch < 2; ch++)
		{
			int32_t s = (ch == 0) ? l : r;
			int64_t x3 = (int64_t) s * 3;  /* scale to ±3.0 in Q15 */
			int64_t x2 = x3 * x3 >> 15;    /* x² in Q15 */
			int64_t num = x3 * (884736 + x2); /* x*(27*Q15 + x²) */
			int64_t den = 884736 + 9 * x2;    /* 27*Q15 + 9*x² */
			int32_t clipped = (int32_t)(num / den);
			clipped = CLAMP (clipped, -32767, 32767);
			int out = (int) nHalf + (int) ((int64_t) clipped * nHalf / 32767);
			pBuffer[f * 2 + ch] = (u32) CLAMP (out, 0, (int) nRange);
		}
	}

	m_Lock.Release ();

	return nChunkSize;
}

// ─── CKernel ───────────────────────────────────────────────────

CKernel *CKernel::s_pThis = 0;

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_pSound (0),
	m_bPollingMode (FALSE),
	m_pMIDIDevice (0)
{
	s_pThis = this;
	m_ActLED.Blink (5);
}

CKernel::~CKernel (void)
{
	delete m_pSound;
	m_pSound = 0;
	s_pThis = 0;
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

	// EMMC + FatFS for SD card
	if (bOK) bOK = m_EMMC.Initialize ();
	if (bOK)
	{
		if (f_mount (&m_FileSystem, DRIVE, 1) != FR_OK)
		{
			m_Logger.Write (FromKernel, LogError, "Cannot mount SD");
			bOK = FALSE;
		}
	}

	// USB (for MIDI keyboard)
	if (bOK) bOK = m_USBHCI.Initialize ();

	// Audio
	if (bOK)
	{
		m_pSound = new CSynthSound (&m_Interrupt);
		assert (m_pSound);
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "");
	m_Logger.Write (FromKernel, LogNotice,
		"========================================");
	m_Logger.Write (FromKernel, LogNotice,
		"  ROMPLER BARE-METAL");
	m_Logger.Write (FromKernel, LogNotice,
		"  Compiled " __DATE__ " " __TIME__);
	m_Logger.Write (FromKernel, LogNotice,
		"========================================");

	// Start audio (plays silence until instruments load)
	if (!m_pSound->Start ())
	{
		m_Logger.Write (FromKernel, LogPanic, "PWM sound start failed");
	}
	m_Logger.Write (FromKernel, LogNotice,
		"PWM audio running at %u Hz", SAMPLE_RATE);

	// Set up MIDI poll callback so SD reads don't block MIDI
	sample_bank_set_poll_cb (midi_poll_callback);

	boolean bLoading = (sample_bank_pending_count () > 0) ? TRUE : FALSE;
	m_Logger.Write (FromKernel, LogNotice,
		"%u instrument loaded, %u more pending",
		m_pSound->GetInstrumentCount (),
		(unsigned) sample_bank_pending_count ());

	m_Logger.Write (FromKernel, LogNotice,
		"Waiting for USB MIDI keyboard...");

	// Main loop — poll for MIDI device, lazy-load instruments between polls
	for (unsigned nCount = 0; m_pSound->IsActive (); nCount++)
	{
		boolean bUpdated = m_USBHCI.UpdatePlugAndPlay ();

		if (m_pMIDIDevice == 0 && bUpdated)
		{
			m_pMIDIDevice = (CUSBMIDIDevice *)
				CDeviceNameService::Get ()->GetDevice ("umidi1", FALSE);

			if (m_pMIDIDevice != 0)
			{
				m_pMIDIDevice->RegisterRemovedHandler (USBDeviceRemovedHandler);
				m_pMIDIDevice->RegisterPacketHandler (MIDIPacketHandler);

				m_Logger.Write (FromKernel, LogNotice,
					">>> USB MIDI device found! <<<");

				// Switch to polling mode to avoid race with IRQ handler
				m_USBHCI.DisableXHCIInterrupts ();
				m_bPollingMode = TRUE;
			}
		}

		// Poll xHCI event ring (only after interrupts disabled)
		if (m_bPollingMode)
			m_USBHCI.PollEvents ();

		// Load one pending instrument per main-loop pass
		if (bLoading)
		{
			if (!rompler_load_next_pending (m_pSound->GetRompler ()))
			{
				bLoading = FALSE;
				m_Logger.Write (FromKernel, LogNotice,
					"All %u instruments loaded",
					m_pSound->GetInstrumentCount ());
			}
		}

		// Check UART for reboot command ('r')
		char cBuf;
		int nRead = m_Serial.Read (&cBuf, 1);
		if (nRead == 1 && cBuf == 'r')
		{
			m_Logger.Write (FromKernel, LogNotice, "Reboot requested via UART");
			m_pSound->Cancel ();
			m_Timer.MsDelay (500);
			return ShutdownReboot;
		}

		// Periodic heartbeat (less frequent)
		if (nCount % 20000000 == 0 && nCount > 0)
		{
			m_Logger.Write (FromKernel, LogNotice,
				"alive (midi=%s, inst=%u)",
				m_pMIDIDevice ? "yes" : "no",
				m_pSound->GetInstrumentCount ());
		}
	}

	return ShutdownHalt;
}

void CKernel::MIDIPacketHandler (unsigned nCable, u8 *pPacket, unsigned nLength)
{
	assert (s_pThis != 0);

	CString msg;
	msg.Format ("MIDI [cable%u len%u]:", nCable, nLength);
	for (unsigned i = 0; i < nLength && i < 16; i++)
	{
		CString hex;
		hex.Format (" %02X", pPacket[i]);
		msg.Append (hex);
	}
	CLogger::Get ()->Write (FromKernel, LogNotice, (const char *) msg);

	CSynthSound *pSound = s_pThis->GetSound ();
	if (!pSound)
		return;

	// Feed raw MIDI bytes to synth (Circle already parses USB MIDI packets)
	for (unsigned i = 0; i < nLength; i++)
	{
		pSound->MIDIByte (pPacket[i]);
	}
}

void CKernel::USBDeviceRemovedHandler (CDevice *pDevice, void *pContext)
{
	assert (s_pThis != 0);

	if (s_pThis->m_pMIDIDevice == (CUSBMIDIDevice *) pDevice)
	{
		CLogger::Get ()->Write (FromKernel, LogNotice,
			"USB MIDI device removed");
		s_pThis->m_pMIDIDevice = 0;
	}
}
