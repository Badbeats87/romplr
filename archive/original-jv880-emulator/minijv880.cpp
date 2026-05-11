//
// minidexed.cpp
//
// MiniDexed - Dexed FM synthesizer for bare metal Raspberry Pi
// Copyright (C) 2022  The MiniDexed Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "minijv880.h"
#include <assert.h>
#include <circle/devicenameservice.h>
#include <circle/gpiopin.h>
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/usb/usbmidihost.h>
#include <stdio.h>
#include <string.h>

void set_pixel(unsigned char *screen, int x, int y, bool value) {
  if (!value)
    screen[(y / 8) * 128 + x] |= 1 << (y % 8);
  else
    screen[(y / 8) * 128 + x] &= ~(1 << (y % 8));
}

CMiniJV880 *CMiniJV880::s_pThis = 0;

// Expansion ROM state
#define PATCH_SIZE 0x16a
#define NVRAM_PATCH_OFFSET 0x0d70
#define INTERNAL_BANK_OFFSET 0x0EDA
static uint8_t *s_expData = nullptr;
static int s_expPatchCount = 0;
static uint32_t s_expPatchesOffset = 0;
static volatile int s_pageRequest = -1;
static int s_currentPage = 0;
static volatile int s_cc71Count = 0;
static volatile int s_lastCC71Val = -1;
static volatile int s_anyCCCount = 0;

LOGMODULE("minijv880");

CMiniJV880::CMiniJV880(CConfig *pConfig, CInterruptSystem *pInterrupt,
                       CGPIOManager *pGPIOManager, CI2CMaster *pI2CMaster,
                       FATFS *pFileSystem, CScreenDevice *mScreenUnbuffered)
    : CMultiCoreSupport(CMemorySystem::Get()), m_pConfig(pConfig),
      m_pFileSystem(pFileSystem), m_pSoundDevice(0),
      m_bChannelsSwapped(pConfig->GetChannelsSwapped()),
      m_ScreenUnbuffered(mScreenUnbuffered) {
  assert(m_pConfig);

  s_pThis = this;

  // select the sound device
  const char *pDeviceName = pConfig->GetSoundDevice();
  if (strcmp(pDeviceName, "i2s") == 0) {
    LOGNOTE("I2S mode");
    m_pSoundDevice = new CI2SSoundBaseDevice(
        pInterrupt, 32000, pConfig->GetChunkSize(), false, pI2CMaster,
        pConfig->GetDACI2CAddress(), CI2SSoundBaseDevice::DeviceModeTXOnly,
        2); // 2 channels - L+R
  } else if (strcmp(pDeviceName, "hdmi") == 0) {
#if RASPPI == 5
    LOGNOTE("HDMI mode NOT supported on RPI 5.");
#else
    LOGNOTE("HDMI mode");

    m_pSoundDevice =
        new CHDMISoundBaseDevice(pInterrupt, 32000, pConfig->GetChunkSize());

    // The channels are swapped by default in the HDMI sound driver.
    // TODO: Remove this line, when this has been fixed in the driver.
    m_bChannelsSwapped = !m_bChannelsSwapped;
#endif
  } else {
    LOGNOTE("PWM mode");

    m_pSoundDevice =
        new CPWMSoundBaseDevice(pInterrupt, 32000, pConfig->GetChunkSize());
  }

  screen_buffer = (u8 *)malloc(512);
};

bool CMiniJV880::Initialize(void) {
  assert(m_pConfig);
  assert(m_pSoundDevice);

  LOGNOTE("Loading emu files");
  uint8_t *rom1 = (uint8_t *)malloc(ROM1_SIZE);
  uint8_t *rom2 = (uint8_t *)malloc(ROM2_SIZE);
  uint8_t *nvram = (uint8_t *)malloc(NVRAM_SIZE);
  uint8_t *pcm1 = (uint8_t *)malloc(0x200000);
  uint8_t *pcm2 = (uint8_t *)malloc(0x200000);

  FIL f;
  unsigned int nBytesRead = 0;
  if (f_open(&f, "jv880_rom1.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
    LOGERR("Cannot open jv880_rom1.bin");
    return false;
  }
  f_read(&f, rom1, ROM1_SIZE, &nBytesRead);
  f_close(&f);
  if (f_open(&f, "jv880_rom2.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
    LOGERR("Cannot open jv880_rom1.bin");
    return false;
  }
  f_read(&f, rom2, ROM2_SIZE, &nBytesRead);
  f_close(&f);
  if (f_open(&f, "jv880_nvram.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
    LOGERR("Cannot open jv880_rom1.bin");
    return false;
  }
  f_read(&f, nvram, NVRAM_SIZE, &nBytesRead);
  f_close(&f);
  if (f_open(&f, "jv880_waverom1.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
    LOGERR("Cannot open jv880_rom1.bin");
    return false;
  }
  f_read(&f, pcm1, 0x200000, &nBytesRead);
  f_close(&f);
  if (f_open(&f, "jv880_waverom2.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
    LOGERR("Cannot open jv880_rom1.bin");
    return false;
  }
  f_read(&f, pcm2, 0x200000, &nBytesRead);
  f_close(&f);
  LOGNOTE("Emu files loaded");

  // Load expansion ROM (board 04 - Vintage Synth)
  {
    uint8_t *tempbuf = (uint8_t *)malloc(0x800000);
    if (tempbuf) {
      const char *expFile = "jv880_exp_12.bin";
      if (f_open(&f, expFile, FA_READ | FA_OPEN_EXISTING) == FR_OK) {
        s_expData = (uint8_t *)malloc(0x800000);
        if (s_expData) {
          f_read(&f, tempbuf, 0x800000, &nBytesRead);
          f_close(&f);
          unscramble(tempbuf, s_expData, 0x800000);

          s_expPatchCount = (s_expData[0x66] << 8) | s_expData[0x67];
          s_expPatchesOffset = (s_expData[0x8c] << 24) | (s_expData[0x8d] << 16) |
                               (s_expData[0x8e] << 8) | s_expData[0x8f];

          // Load first 64 patches into Internal bank
          int nLoad = s_expPatchCount < 64 ? s_expPatchCount : 64;
          for (int i = 0; i < nLoad; i++) {
            uint32_t srcOff = s_expPatchesOffset + (uint32_t)i * PATCH_SIZE;
            uint32_t dstOff = INTERNAL_BANK_OFFSET + i * PATCH_SIZE;
            if (srcOff + PATCH_SIZE <= 0x800000 && dstOff + PATCH_SIZE <= NVRAM_SIZE)
              memcpy(nvram + dstOff, s_expData + srcOff, PATCH_SIZE);
          }
          memcpy(nvram + NVRAM_PATCH_OFFSET, s_expData + s_expPatchesOffset, PATCH_SIZE);
          int numPages = (s_expPatchCount + 63) / 64;
          LOGNOTE("Expansion: %s (%d patches, %d pages)", expFile, s_expPatchCount, numPages);
        } else {
          f_close(&f);
        }
      }
      free(tempbuf);
    }
  }

  mcu.startSC55(rom1, rom2, pcm1, pcm2, nvram);

  // Copy expansion waveforms into MCU PCM engine
  if (s_expData)
    memcpy(mcu.pcm.waverom_exp, s_expData, 0x800000);

  free(rom1);
  free(rom2);
  free(nvram);
  free(pcm1);
  free(pcm2);

  // setup and start the sound device
  int Channels = 2; // 16-bit Stereo
  // Need 2 x ChunkSize / Channel queue frames as the audio driver uses
  // two DMA channels each of ChunkSize and one single single frame
  // contains a sample for each of all the channels.
  //
  // See discussion here: https://github.com/rsta2/circle/discussions/453
  if (!m_pSoundDevice->AllocateQueueFrames(2 * m_pConfig->GetChunkSize() /
                                           Channels)) {
    LOGERR("Cannot allocate sound queue");

    return false;
  }

  m_pSoundDevice->SetWriteFormat(SoundFormatSigned16, Channels);

  m_nQueueSizeFrames = m_pSoundDevice->GetQueueSizeFrames();

  m_pSoundDevice->Start();

  CMultiCoreSupport::Initialize();

  return true;
}

void CMiniJV880::Process(bool bPlugAndPlayUpdated) {
  uint32_t *lcd_buffer = mcu.lcd.LCD_Update();

  for (size_t y = 0; y < lcd_height; y++) {
    for (size_t x = 0; x < lcd_width; x++) {
      m_ScreenUnbuffered->SetPixel(x + 800, y + 100,
                                   lcd_buffer[y * lcd_width + x]);
    }
  }

  if (m_KompleteKontrol != 0) {
    m_KompleteKontrol->Update();

    uint32_t btn = 0;
    if (m_KompleteKontrol->status.left)
      btn |= 1 << MCU_BUTTON_CURSOR_L;
    else
      btn &= ~(1 << MCU_BUTTON_CURSOR_L);
    if (m_KompleteKontrol->status.right)
      btn |= 1 << MCU_BUTTON_CURSOR_R;
    else
      btn &= ~(1 << MCU_BUTTON_CURSOR_R);
    if (m_KompleteKontrol->status.loop)
      btn |= 1 << MCU_BUTTON_TONE_SELECT;
    else
      btn &= ~(1 << MCU_BUTTON_TONE_SELECT);
    if (m_KompleteKontrol->status.metro)
      btn |= 1 << MCU_BUTTON_MUTE;
    else
      btn &= ~(1 << MCU_BUTTON_MUTE);
    if (m_KompleteKontrol->status.tempo)
      btn |= 1 << MCU_BUTTON_DATA;
    else
      btn &= ~(1 << MCU_BUTTON_DATA);
    if (m_KompleteKontrol->status.undo)
      btn |= 1 << MCU_BUTTON_MONITOR;
    else
      btn &= ~(1 << MCU_BUTTON_MONITOR);
    if (m_KompleteKontrol->status.quantize)
      btn |= 1 << MCU_BUTTON_COMPARE;
    else
      btn &= ~(1 << MCU_BUTTON_COMPARE);
    if (m_KompleteKontrol->status.jstick_push)
      btn |= 1 << MCU_BUTTON_ENTER;
    else
      btn &= ~(1 << MCU_BUTTON_ENTER);
    if (m_KompleteKontrol->status.ideas)
      btn |= 1 << MCU_BUTTON_UTILITY;
    else
      btn &= ~(1 << MCU_BUTTON_UTILITY);
    if (m_KompleteKontrol->status.play)
      btn |= 1 << MCU_BUTTON_PREVIEW;
    else
      btn &= ~(1 << MCU_BUTTON_PREVIEW);
    if (m_KompleteKontrol->status.quantize)
      btn |= 1 << MCU_BUTTON_PATCH_PERFORM;
    else
      btn &= ~(1 << MCU_BUTTON_PATCH_PERFORM);
    if (m_KompleteKontrol->status.shift)
      btn |= 1 << MCU_BUTTON_EDIT;
    else
      btn &= ~(1 << MCU_BUTTON_EDIT);
    if (m_KompleteKontrol->status.scale)
      btn |= 1 << MCU_BUTTON_SYSTEM;
    else
      btn &= ~(1 << MCU_BUTTON_SYSTEM);
    if (m_KompleteKontrol->status.arp)
      btn |= 1 << MCU_BUTTON_RHYTHM;
    else
      btn &= ~(1 << MCU_BUTTON_RHYTHM);
    mcu.mcu_button_pressed = btn;

    if (m_KompleteKontrol->status.jstick_val > lastEncoderPos ||
        (lastEncoderPos == 15 && m_KompleteKontrol->status.jstick_val == 0))
      mcu.MCU_EncoderTrigger(1);
    else if (m_KompleteKontrol->status.jstick_val < lastEncoderPos ||
             (lastEncoderPos == 0 &&
              m_KompleteKontrol->status.jstick_val == 15))
      mcu.MCU_EncoderTrigger(0);
    lastEncoderPos = m_KompleteKontrol->status.jstick_val;

    for (size_t y = 0; y < 32; y++) {
      for (size_t x = 0; x < 128; x++) {
        int destX = (int)(((float)x / 128) * 820);
        int destY = (int)(((float)y / 32) * 100);
        int sum = 0;
        for (int py = -1; py <= 1; py++) {
          for (int px = -1; px <= 1; px++) {
            if ((destY + py) >= 0 && (destX + px) >= 0) {
              bool pixel =
                  mcu.lcd.lcd_buffer[destY + py][destX + px] == lcd_col1;
              sum += pixel;
            }
          }
        }

        bool pixel = sum > 0;
        // bool pixel = mcu.lcd.lcd_buffer[destY][destX] == lcd_col1;
        set_pixel(screen_buffer, x, y, pixel);

        // m_ScreenUnbuffered->SetPixel(x + 800, y + 300, pixel ? 0xFFFF : 0x0000);
      }
    }

    KompleteKontrolScreenCommand tmp;
    for (size_t row = 0; row < 4; row++) {
      for (size_t column = 0; column < 4; column++) {
        tmp.lengthRow = 1;
        tmp.lengthCol = 32;
        tmp.offsetRow = row;
        tmp.offsetCol = column * 32;
        memcpy(tmp.content, screen_buffer + row * 128 + column * 32, 32);
        m_KompleteKontrol->SendScreen(&tmp);
      }
    }
  }

  {
    static bool midiChecked = false;
    if (!midiChecked) {
      midiChecked = true;
      // Try to register handler on ALL umidi devices
      for (int i = 1; i <= 4; i++) {
        char name[8];
        name[0] = 'u'; name[1] = 'm'; name[2] = 'i'; name[3] = 'd'; name[4] = 'i';
        name[5] = '0' + i; name[6] = 0;
        CUSBMIDIDevice *dev =
            (CUSBMIDIDevice *)CDeviceNameService::Get()->GetDevice(name, FALSE);
        if (dev != 0) {
          dev->RegisterPacketHandler(USBMIDIMessageHandler);
          if (m_pMIDIDevice == 0) {
            m_pMIDIDevice = dev;
            m_pMIDIDevice->RegisterRemovedHandler(DeviceRemovedHandler, this);
          }
          LOGNOTE("Registered handler on %s", name);
        }
      }
      if (m_pMIDIDevice == 0)
        LOGNOTE("No MIDI devices found");
      LOGNOTE("Expansion state: expData=%p patchCount=%d patchesOffset=0x%x",
              s_expData, s_expPatchCount, s_expPatchesOffset);
    }
  }

  // Debug: log CC activity from IRQ context
  {
    static int lastReportedCC = 0;
    static int lastReported71 = 0;
    if (s_anyCCCount != lastReportedCC || s_cc71Count != lastReported71) {
      LOGNOTE("CC debug: anyCC=%d cc71=%d lastCC71val=%d expPatches=%d curPage=%d pageReq=%d",
              s_anyCCCount, s_cc71Count, s_lastCC71Val, s_expPatchCount, s_currentPage, s_pageRequest);
      lastReportedCC = s_anyCCCount;
      lastReported71 = s_cc71Count;
    }
  }

  if (m_KompleteKontrol == 0) {
    m_KompleteKontrol =
        (CUSBKompleteKontrolDevice *)CDeviceNameService::Get()->GetDevice(
            "kompletekontrol1", FALSE);
    if (m_KompleteKontrol != 0) {
      // m_KompleteKontrol->RegisterPacketHandler(s_pMIDIPacketHandler[m_nInstance]);
      m_KompleteKontrol->RegisterRemovedHandler(DeviceRemovedHandler, this);

      m_KompleteKontrol->DisableLocalControls();
      // m_KompleteKontrol->SendLEDs();

      // u8 content[256] = {0};
      // for (size_t i = 0; i < 256; i++) {
      //   content[i] = 0xff;
      // }
      // m_KompleteKontrol->SendScreenUpper(content);
      // m_KompleteKontrol->SendScreenLower(content);
    }
  }
}


// Send Roland SysEx: F0 41 10 46 12 [addr0..3] [data] [checksum] F7
static void sendJV880SysEx(MCU &mcu, u8 addr0, u8 addr1, u8 addr2, u8 addr3, u8 data) {
  u8 checksum = (128 - ((addr0 + addr1 + addr2 + addr3 + data) & 0x7F)) & 0x7F;
  u8 msg[] = { 0xF0, 0x41, 0x10, 0x46, 0x12,
               addr0, addr1, addr2, addr3, data, checksum, 0xF7 };
  mcu.postMidiSC55(msg, sizeof(msg));
}

// Send a parameter to all 4 tones of the temporary patch
// Tone base addresses: 0x28, 0x29, 0x2A, 0x2B
static void sendToAllTones(MCU &mcu, u8 paramOffset, u8 data) {
  for (u8 tone = 0; tone < 4; tone++) {
    sendJV880SysEx(mcu, 0x00, 0x08, 0x28 + tone, paramOffset, data);
  }
}

void CMiniJV880::USBMIDIMessageHandler(unsigned nCable, u8 *pPacket,
                                       unsigned nLength) {
  CMiniJV880 *pThis = static_cast<CMiniJV880 *>(s_pThis);

  // Intercept CCs on any channel
  if (nLength >= 3 && (pPacket[0] & 0xF0) == 0xB0) {
    u8 channel = pPacket[0] & 0x0F;
    u8 cc = pPacket[1];
    u8 val = pPacket[2];
    s_anyCCCount++;

    // CC 70: Bank select — knob 0-127 mapped to 3 banks
    //   0-42  = Internal/Expansion (CC0=80, PC=0)
    //   43-84 = Preset A (CC0=81, PC=0)
    //   85-127 = Preset B (CC0=81, PC=64)
    if (cc == 70) {
      static u8 lastBank = 0xFF;
      u8 bank;
      if (val < 43) bank = 0;
      else if (val < 85) bank = 1;
      else bank = 2;
      if (bank == lastBank) return;
      lastBank = bank;
      u8 msb = (bank == 0) ? 80 : 81;
      u8 pc  = (bank == 2) ? 64 : 0;
      u8 bankMSB[] = { (u8)(0xB0 | channel), 0x00, msb };
      pThis->mcu.postMidiSC55(bankMSB, 3);
      u8 pgm[] = { (u8)(0xC0 | channel), pc };
      pThis->mcu.postMidiSC55(pgm, 2);
      return;
    }

    // CC 71: Expansion page select — knob 0-127 mapped to pages
    if (cc == 71) {
      s_cc71Count++;
      s_lastCC71Val = val;
    }
    if (cc == 71 && s_expPatchCount > 0) {
      int numPages = (s_expPatchCount + 63) / 64;
      int newPage = (val * numPages) / 128;
      if (newPage >= numPages) newPage = numPages - 1;
      if (newPage != s_currentPage) {
        s_currentPage = newPage;
        s_pageRequest = newPage;
      }
      return;
    }

    // Sound editing CCs → JV-880 SysEx
    switch (cc) {
      case 74: // TVF Cutoff Frequency (0-127)
        sendToAllTones(pThis->mcu, 0x4A, val);
        return;
      case 75: // TVF Resonance (0-127)
        sendToAllTones(pThis->mcu, 0x4B, val);
        return;
      case 73: // Attack Time - TVA Env Time 1 (0-127)
        sendToAllTones(pThis->mcu, 0x69, val);
        return;
      case 76: // Decay Time - TVA Env Time 2 (0-127)
        sendToAllTones(pThis->mcu, 0x6B, val);
        return;
      case 72: // Release Time - TVA Env Time 4 (0-127)
        sendToAllTones(pThis->mcu, 0x6F, val);
        return;
      case 77: // LFO1 Rate (0-127)
        sendToAllTones(pThis->mcu, 0x25, val);
        return;
      case 78: // LFO1 TVA Depth (0-127)
        sendToAllTones(pThis->mcu, 0x2C, val);
        return;
      case 79: // Chorus Depth - Patch Common (0-127)
        sendJV880SysEx(pThis->mcu, 0x00, 0x08, 0x20, 0x13, val);
        return;
    }
  }

  // Pass everything else through
  pThis->mcu.postMidiSC55(pPacket, nLength);
}

void CMiniJV880::DeviceRemovedHandler(CDevice *pDevice, void *pContext) {
  LOGERR("CMiniJV880::DeviceRemovedHandler");

  CMiniJV880 *pThis = static_cast<CMiniJV880 *>(pContext);
  assert(pThis != 0);

  if (pDevice == pThis->m_pMIDIDevice)
    pThis->m_pMIDIDevice = 0;
  if (pDevice == pThis->m_KompleteKontrol)
    pThis->m_KompleteKontrol = 0;
}

// Synchronization between Core 2 (MCU) and Core 3 (PCM)
volatile int nFramesNeeded = 0;
volatile bool pcm_active = false;
int16_t output_buffer[65536]; // separate buffer for sound device writes

void CMiniJV880::Run(unsigned nCore) {
  assert(1 <= nCore && nCore < CORES);

  if (nCore == 1) {
    // unused
  } else if (nCore == 2) {
    // emulator (MCU)
    while (true) {
      unsigned nFrames =
          m_nQueueSizeFrames - m_pSoundDevice->GetQueueFramesAvail();
      if (nFrames >= m_nQueueSizeFrames / 2) {
        // Ensure PCM is idle before resetting pointers
        pcm_active = false;
        __sync_synchronize();

        // With oversampling, PCM produces 2 samples per cycle at 64kHz.
        // The sound device runs at 32kHz, so we need nFrames output frames.
        // The emulator produces 2x that many frames (oversampled).
        // sample_write_ptr increments by 1 per L/R pair.
        int emulatorFrames = (int)nFrames * 2; // 2x oversampling
        nFramesNeeded = emulatorFrames;
        mcu.sample_write_ptr = 0;
        __sync_synchronize();

        pcm_active = true;
        __sync_synchronize();

        while (mcu.sample_write_ptr < emulatorFrames) {
          if (!mcu.mcu.ex_ignore)
            mcu.MCU_Interrupt_Handle();
          else
            mcu.mcu.ex_ignore = 0;

          if (!mcu.mcu.sleep)
            mcu.MCU_ReadInstruction();

          mcu.mcu.cycles += 12; // FIXME: assume 12 cycles per instruction

          mcu.TIMER_Clock(mcu.mcu.cycles);
          mcu.MCU_UpdateUART_RX();
          mcu.MCU_UpdateUART_TX();
          mcu.MCU_UpdateAnalog(mcu.mcu.cycles);
        }

        // Stop PCM and wait for it to idle
        pcm_active = false;
        __sync_synchronize();

        // Downsample 2x oversampled float buffers to 32kHz int16_t output
        // 4-tap FIR half-band filter + DC removal + triangular dither
        {
          // DC offset state (persists across calls)
          static float dc_l = 0.0f, dc_r = 0.0f;
          // TPDF dither LFSR state
          static uint32_t lfsr = 0xACE1u;

          // Half-band FIR coefficients: [-1, 9, 9, -1] / 16
          // Good stopband rejection for 2x downsampling
          static const float h0 = -1.0f / 16.0f;
          static const float h1 =  9.0f / 16.0f;

          int outIdx = 0;
          for (int i = 0; i < emulatorFrames; i += 2) {
            // 4-tap FIR: use samples [i-1, i, i+1, i+2]
            int i0 = (i > 0) ? i - 1 : 0;
            int i1 = i;
            int i2 = i + 1;
            int i3 = (i + 2 < emulatorFrames) ? i + 2 : emulatorFrames - 1;

            float l = h0 * mcu.sample_buffer_l[i0] + h1 * mcu.sample_buffer_l[i1]
                    + h1 * mcu.sample_buffer_l[i2] + h0 * mcu.sample_buffer_l[i3];
            float r = h0 * mcu.sample_buffer_r[i0] + h1 * mcu.sample_buffer_r[i1]
                    + h1 * mcu.sample_buffer_r[i2] + h0 * mcu.sample_buffer_r[i3];

            // DC offset removal (slow-tracking high-pass)
            dc_l += (l - dc_l) * 0.0005f;
            dc_r += (r - dc_r) * 0.0005f;
            l -= dc_l;
            r -= dc_r;

            // TPDF dither: two LFSR samples, difference gives triangular distribution
            // Amplitude: ±1 LSB of 16-bit (1/32768)
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
            float d1 = (float)(int16_t)lfsr / 32768.0f;
            lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
            float d2 = (float)(int16_t)lfsr / 32768.0f;
            float dither = (d1 - d2) * (1.0f / 32768.0f);

            // Convert float [-1.0, 1.0] to int16_t with dither and clipping
            int32_t il = (int32_t)(l * 32767.0f + dither);
            int32_t ir = (int32_t)(r * 32767.0f + dither);
            if (il > 32767) il = 32767; else if (il < -32768) il = -32768;
            if (ir > 32767) ir = 32767; else if (ir < -32768) ir = -32768;
            output_buffer[outIdx++] = (int16_t)il;
            output_buffer[outIdx++] = (int16_t)ir;
          }

          int len = outIdx * sizeof(int16_t);
          if (m_pSoundDevice->Write(output_buffer, len) != len) {
            LOGERR("Sound data dropped");
          }
        }

        // Handle expansion page switch request from CC71
        if (s_pageRequest >= 0) {
          int newPage = s_pageRequest;
          s_pageRequest = -1;
          __sync_synchronize();

          int startPatch = newPage * 64;
          int remaining = s_expPatchCount - startPatch;
          int nLoad = remaining < 64 ? remaining : 64;
          if (nLoad > 0) {
            for (int i = 0; i < nLoad; i++) {
              uint32_t srcOff = s_expPatchesOffset + (uint32_t)(startPatch + i) * PATCH_SIZE;
              uint32_t dstOff = INTERNAL_BANK_OFFSET + i * PATCH_SIZE;
              if (srcOff + PATCH_SIZE <= 0x800000 && dstOff + PATCH_SIZE <= NVRAM_SIZE)
                memcpy(mcu.nvram + dstOff, s_expData + srcOff, PATCH_SIZE);
            }
            // Set active patch to first patch of new page
            memcpy(mcu.nvram + NVRAM_PATCH_OFFSET,
                   s_expData + s_expPatchesOffset + (uint32_t)startPatch * PATCH_SIZE,
                   PATCH_SIZE);
            mcu.SC55_Reset();
            LOGNOTE("Page %d: patches %d-%d", newPage + 1, startPatch + 1, startPatch + nLoad);
          }
        }

      }
    }
  } else if (nCore == 3) {
    // pcm chip — gated by Core 2
    while (true) {
      if (pcm_active && mcu.sample_write_ptr < nFramesNeeded) {
        mcu.pcm.PCM_Update(mcu.mcu.cycles);
      }
    }
  }
}
