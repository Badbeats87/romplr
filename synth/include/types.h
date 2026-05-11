#ifndef SYNTH_TYPES_H
#define SYNTH_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif

#ifndef CLAMP
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

#define Q15_ONE 32767
#define Q15_MUL(a, b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> 15))

#ifndef SYNTH_SAMPLE_RATE
#define SYNTH_SAMPLE_RATE 48000u
#endif

#ifndef PCM_SAMPLE_RATE
#define PCM_SAMPLE_RATE   44100u
#endif

#define PHASE_BITS 24
#define PHASE_ONE  (1u << PHASE_BITS)
#define PHASE_MASK (PHASE_ONE - 1u)

#endif /* SYNTH_TYPES_H */
