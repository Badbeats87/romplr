//
// kernel.h
//
// Minimal rompler kernel — HDMI + audio + USB + SD card.
// Cooperative scheduler for background instrument loading.
//

#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbmidi.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <circle/types.h>

extern "C" {
#include "../../synth/baremetal/rompler.h"
}

#define SAMPLE_RATE     48000
#define CHUNK_SIZE      2048

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CSynthSound : public CPWMSoundBaseDevice
{
public:
	CSynthSound (CInterruptSystem *pInterrupt);
	~CSynthSound (void) {}

	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override;

	void NoteOn (u8 ucNote, u8 ucVelocity);
	void NoteOff (u8 ucNote);
	void MIDIByte (u8 ucByte);
	unsigned GetInstrumentCount (void) const;
	unsigned GetSamplePoolUsed (void) const;
	Rompler *GetRompler (void) { return &m_Rompler; }

private:
	Rompler   m_Rompler;
	CSpinLock m_Lock;
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	TShutdownMode Run (void);

	static CKernel *Get (void) { return s_pThis; }
	CSynthSound *GetSound (void) { return m_pSound; }
	void PollMIDI (void) { if (m_bPollingMode) m_USBHCI.PollEvents (); }

private:
	static void MIDIPacketHandler (unsigned nCable, u8 *pPacket,
				       unsigned nLength);
	static void USBDeviceRemovedHandler (CDevice *pDevice, void *pContext);

	// do not change this order
	CActLED            m_ActLED;
	CKernelOptions     m_Options;
	CDeviceNameService m_DeviceNameService;
	CScreenDevice      m_Screen;
	CSerialDevice      m_Serial;
	CExceptionHandler  m_ExceptionHandler;
	CInterruptSystem   m_Interrupt;
	CTimer             m_Timer;
	CLogger            m_Logger;
	CUSBHCIDevice      m_USBHCI;
	CEMMCDevice        m_EMMC;
	FATFS              m_FileSystem;

	CSynthSound       *m_pSound;
	boolean            m_bPollingMode;

	CUSBMIDIDevice * volatile m_pMIDIDevice;

	static CKernel    *s_pThis;
};

#endif
