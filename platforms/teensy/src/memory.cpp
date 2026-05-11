/*
 * Platform memory allocator for Teensy 4.1
 *
 * Provides alloc_high_mem() called by sample_bank.c to allocate
 * the sample pool.  Tries PSRAM first (if soldered), falls back
 * to a pre-allocated DMAMEM buffer in RAM2.
 *
 * Memory map (Teensy 4.1):
 *   RAM1 (DTCM)  512 KB — stack, .data, .bss, heap (malloc)
 *   RAM2 (DMAMEM) 512 KB — DMA buffers, our sample pool
 *   PSRAM          0/8/16 MB — optional external memory
 */
#include <Arduino.h>

/* PSRAM size detected by Teensy startup code (0 if not installed) */
extern "C" uint8_t external_psram_size;

#ifndef STREAM_FROM_SD
/* Pre-allocated sample pool in DMAMEM (RAM2).
 * SAMPLE_POOL_SAMPLES is set by build_flags (default 204800 = ~400 KB). */
DMAMEM static int16_t sample_pool_dmamem[SAMPLE_POOL_SAMPLES];
#endif

extern "C" void *alloc_high_mem(size_t size)
{
#ifdef STREAM_FROM_SD
    /* Streaming mode: no sample pool needed */
    Serial.println("Sample pool: NONE (SD streaming mode)");
    return NULL;
#else
    /* Try PSRAM first — extmem_malloc returns NULL if not present */
    if (external_psram_size > 0) {
        void *p = extmem_malloc(size);
        if (p) {
            Serial.printf("Sample pool: %u KB in PSRAM (%u MB chip)\n",
                          (unsigned)(size / 1024), external_psram_size);
            return p;
        }
    }

    /* Fall back to pre-allocated DMAMEM buffer */
    if (size <= sizeof(sample_pool_dmamem)) {
        Serial.printf("Sample pool: %u KB in DMAMEM\n",
                      (unsigned)(size / 1024));
        return sample_pool_dmamem;
    }

    Serial.printf("alloc_high_mem: FAILED (%u KB requested, %u KB available)\n",
                  (unsigned)(size / 1024),
                  (unsigned)(sizeof(sample_pool_dmamem) / 1024));
    return NULL;
#endif
}
