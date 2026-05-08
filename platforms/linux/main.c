/* Step 6: ADSR envelope + SVF lowpass filter + MIDI CC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sched.h>
#include <sys/mman.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE   44100
#define CHANNELS      1
#define PERIOD_SIZE   512   /* bcm2835 driver minimum is 10ms = 441 frames */
#define PERIODS       3
#define MAX_PERIOD    1024  /* max period ALSA might give us */
#define MAX_ZONES     64
#define NUM_VOICES    8
#define ROM_SAMPLE_RATE 32000
#define NUM_ROM_WAVES   108
#define MAX_SAMPLE_BUFFERS 256
#define LOOP_XFADE_SAMPLES 1024

/* ---- Zone (used for both WAV and ROM) ---- */
typedef struct {
    const int16_t *data;
    int len;
    int sample_rate;
    int root_note;
    int lo_note;
    int hi_note;
    int lo_vel;
    int hi_vel;
    int loop_start;  /* 0 = no loop */
    int loop_end;
} Zone;

/* ---- Instrument ---- */
typedef struct {
    char name[32];
    Zone zones[MAX_ZONES];
    int num_zones;
    double gain;  /* normalization gain */
} Instrument;

/* ---- ROM wave table entry (from JD-800) ---- */
typedef struct {
    uint8_t  high_key;
    uint8_t  root_key;
    uint8_t  volume;
    uint8_t  pad;
    uint32_t wav_offset;
    uint32_t length;
    uint32_t loop_start;
} RomZone;

typedef struct {
    char     name[13];
    uint8_t  num_zones;
    uint8_t  rom;
    uint8_t  pad;
    RomZone  zones[16];
} RomWave;

#include "wave_table.h"  /* g_rom_waves[108] */

/* ---- Envelope ---- */
enum { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    int stage;
    double value;        /* 0.0 to 1.0 */
    double attack_rate;  /* increment per sample */
    double decay_rate;   /* decrement per sample */
    double sustain;      /* sustain level */
    double release_rate; /* decrement per sample */
} AmpEnv;

/* ---- SVF filter state ---- */
typedef struct {
    double low;
    double band;
} SVF;

/* ---- State ---- */
static int16_t *rom_pcm = NULL;
static int rom_pcm_len = 0;
static int16_t *sample_buffers[MAX_SAMPLE_BUFFERS];
static int num_sample_buffers = 0;

#define MAX_INSTRUMENTS 1024
static Instrument instruments[MAX_INSTRUMENTS];
static int num_instruments = 0;
static int current_inst = 0;

/* ---- Deferred (lazy) loading ---- */
static char g_pending_dir[512];
static char *g_pending_sf2[256];
static int g_num_pending = 0;

/* Voice */
typedef struct {
    int note;          /* -1 = free */
    int vel;
    double pos;
    double rate;
    double target_rate;
    uint32_t age;
    Zone *zone;
    double zone_gain;  /* per-instrument normalization gain */
    AmpEnv env;
    SVF filt;
} Voice;

static Voice voices[NUM_VOICES];
static uint32_t voice_age = 0;
static pthread_mutex_t vlock = PTHREAD_MUTEX_INITIALIZER;

/* ---- Global parameters (controlled by MIDI CC) ---- */
static double g_cutoff_hz  = 6000.0;  /* SVF cutoff */
static double g_resonance  = 1.0;     /* Q factor */
static double g_attack_ms  = 5.0;
static double g_decay_ms   = 200.0;
static double g_sustain    = 0.8;
static double g_release_ms = 80.0;
static double g_volume     = 1.0;
static int g_demo_mode     = 0;

/* ---- LFO ---- */
static double g_lfo_rate   = 2.0;   /* Hz */
static double g_lfo_phase  = 0.0;   /* 0..1 */
static double g_lfo_filt   = 0.0;   /* depth: filter cutoff mod (0..1) */
static double g_lfo_pitch  = 0.0;   /* depth: pitch mod in semitones (0..1) */
static double g_lfo_amp    = 0.0;   /* depth: amplitude tremolo (0..1) */
static int    g_lfo_wave   = 0;     /* 0=tri, 1=sin, 2=square, 3=saw */

/* ---- Filter envelope, Portamento, Chorus, Reverb ---- */
static double g_filt_env   = 0.0;   /* CC82: filter envelope amount (0..1) */
static double g_portamento = 0.0;   /* CC5: portamento time in seconds */
static double g_chorus_mix = 0.0;   /* CC93: chorus wet/dry */
static double g_reverb_mix = 0.0;   /* CC91: reverb wet/dry */
static int    g_last_note  = -1;    /* last played note for portamento */

/* ---- Chorus (triangle-modulated delay, no sin()) ---- */
#define CHORUS_BUF_SIZE 2048
static float chorus_buf[CHORUS_BUF_SIZE];
static int chorus_wpos = 0;
static double chorus_phase1 = 0.0;
static double chorus_phase2 = 0.0;

/* ---- Reverb (Schroeder: 4 comb + 2 allpass, float to save memory) ---- */
#define RV_COMB1 1557
#define RV_COMB2 1617
#define RV_COMB3 1491
#define RV_COMB4 1422
#define RV_AP1   225
#define RV_AP2   556
static float rv_c1[RV_COMB1], rv_c2[RV_COMB2], rv_c3[RV_COMB3], rv_c4[RV_COMB4];
static int rv_c1p, rv_c2p, rv_c3p, rv_c4p;
static float rv_a1[RV_AP1], rv_a2[RV_AP2];
static int rv_a1p, rv_a2p;

/* ---- Fast math approximations (hot path) ---- */

/* Fast sin(x) for x in [-pi, pi] using Bhaskara approximation, ~0.18% max error */
static inline double fast_sin_pi(double x)
{
    /* Normalize to [0, 2*pi] then use identity */
    double x2 = x * x;
    double num = 16.0 * x * (M_PI - x);
    double den = 5.0 * M_PI * M_PI - 4.0 * x * (M_PI - x);
    (void)x2;
    return num / den;
}

/* Fast sin for full-range phase p in [0,1] → sin(2*pi*p) */
static inline double fast_sin_phase(double p)
{
    /* Map p to [0, pi] for Bhaskara, negate for second half-cycle */
    int neg = 0;
    double x = 2.0 * p;  /* [0, 2] */
    if (x > 1.0) { x = x - 1.0; neg = 1; }  /* now x in [0,1], neg for [pi,2pi] */
    x *= M_PI;  /* [0, pi] */
    double num = 16.0 * x * (M_PI - x);
    double den = 5.0 * M_PI * M_PI - 4.0 * x * (M_PI - x);
    double s = num / den;
    return neg ? -s : s;
}

/* Fast pow(2, x) for |x| < 4, polynomial approx ~0.1% error */
static inline double fast_pow2(double x)
{
    double a = 1.0 + 0.693147 * x + 0.240227 * x * x
             + 0.055504 * x * x * x + 0.009618 * x * x * x * x;
    return a;
}

/* Fast 2*sin(pi*fc/SR) — the SVF coefficient, Taylor approx for small fc/SR */
static inline double fast_svf_coeff(double fc, double sr)
{
    double x = M_PI * fc / sr;
    /* sin(x) ≈ x - x³/6 + x⁵/120 for small x (fc < 14000, x < 1.0) */
    double x2 = x * x;
    double s = x * (1.0 - x2 * (1.0/6.0 - x2 * (1.0/120.0)));
    return 2.0 * s;
}

/* Loop-aware sample fetch for Hermite interpolation.
   in_loop: true when playback position is inside the loop (idx >= loop_start).
   This ensures backward lookups wrap around the loop boundary correctly. */
static inline int16_t fetch_sample(const Zone *z, int i, int loop_start, int loop_end, int loop_len, int in_loop)
{
    if (in_loop) {
        while (i >= loop_end) i -= loop_len;
        while (i < loop_start) i += loop_len;
    } else {
        if (i < 0) i = 0;
        if (i >= z->len) i = z->len - 1;
    }
    return z->data[i];
}

static int g_lfo_prev_wave = 0;
static double g_lfo_xfade = 1.0; /* 1.0 = fully on new wave, 0.0 = fully on old */

static double lfo_wave_val(int wave, double p)
{
    switch (wave) {
    case 0: return (p < 0.5) ? (4.0 * p - 1.0) : (3.0 - 4.0 * p);
    case 1: return fast_sin_phase(p);
    case 2: return (p < 0.5) ? 1.0 : -1.0;
    case 3: return 2.0 * p - 1.0;
    default: return 0.0;
    }
}

static double lfo_tick(void)
{
    g_lfo_phase += g_lfo_rate / SAMPLE_RATE;
    if (g_lfo_phase >= 1.0) g_lfo_phase -= 1.0;
    double p = g_lfo_phase;
    double val = lfo_wave_val(g_lfo_wave, p);
    if (g_lfo_xfade < 1.0) {
        double old_val = lfo_wave_val(g_lfo_prev_wave, p);
        val = old_val * (1.0 - g_lfo_xfade) + val * g_lfo_xfade;
        g_lfo_xfade += 1.0 / (SAMPLE_RATE * 0.01); /* 10ms crossfade */
        if (g_lfo_xfade > 1.0) g_lfo_xfade = 1.0;
    }
    return val;
}

/* ---- WAV sample pool ---- */
static int16_t sample_pool[8 * 1024 * 1024]; /* 8M samples = 16MB */
static int pool_used = 0;

static int has_suffix(const char *s, const char *suffix)
{
    size_t slen = strlen(s), xlen = strlen(suffix);
    return slen >= xlen && strcasecmp(s + slen - xlen, suffix) == 0;
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void copy_sf_name(char *dst, size_t dst_size, const unsigned char *src, size_t src_size)
{
    size_t n = 0;
    while (n + 1 < dst_size && n < src_size && src[n] != '\0') {
        dst[n] = (char)src[n];
        n++;
    }
    dst[n] = '\0';
}

static int load_rom_pcm(const char *path);
static void load_rom_instruments(void);

/* ---- Note name parsing (last match) ---- */
static int parse_note_name(const char *s, int *out)
{
    int found = -1;
    for (const char *p = s; *p; p++) {
        char c = toupper(*p);
        if (c < 'A' || c > 'G') continue;
        int base;
        switch (c) {
            case 'C': base = 0; break; case 'D': base = 2; break;
            case 'E': base = 4; break; case 'F': base = 5; break;
            case 'G': base = 7; break; case 'A': base = 9; break;
            case 'B': base = 11; break; default: continue;
        }
        const char *q = p + 1;
        while (*q == ' ' || *q == '-') q++;
        if (*q == '#') { base++; q++; }
        else if (*q == 'b') { base--; q++; }
        while (*q == ' ' || *q == '-') q++;
        if (!isdigit(*q)) continue;
        int octave = *q - '0';
        found = (octave + 1) * 12 + base;
    }
    if (found >= 0) { *out = found; return 0; }
    return -1;
}

static int contains_ci(const char *s, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0) return 1;
    for (; *s; s++) {
        size_t i = 0;
        while (i < nlen && s[i] &&
               tolower((unsigned char)s[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen) return 1;
    }
    return 0;
}

static void infer_velocity_range(const char *name, int *lo, int *hi)
{
    *lo = 0;
    *hi = 127;
    if (contains_ci(name, "hard") || contains_ci(name, "_hard_") ||
        contains_ci(name, "hrd") || contains_ci(name, " hi.")) {
        *lo = 85;
        *hi = 127;
    } else if (contains_ci(name, "soft") || contains_ci(name, "_soft_") ||
               contains_ci(name, "sft") || contains_ci(name, " lo.")) {
        *lo = 0;
        *hi = 84;
    }
}

/* ---- WAV loader ---- */
static int load_wav_file(const char *path, const int16_t **out_data, int *out_len,
                         int *out_rate, int *out_loop_start, int *out_loop_end)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char riff[4]; uint32_t fsz; char wave[4];
    fread(riff, 1, 4, f); fread(&fsz, 4, 1, f); fread(wave, 1, 4, f);
    if (memcmp(riff, "RIFF", 4) || memcmp(wave, "WAVE", 4)) { fclose(f); return -1; }
    int got_fmt = 0;
    int data_loaded = 0;
    *out_loop_start = 0;
    *out_loop_end = 0;
    while (!feof(f)) {
        char id[4]; uint32_t sz;
        if (fread(id, 1, 4, f) != 4) break;
        if (fread(&sz, 4, 1, f) != 1) break;
        long next = ftell(f) + sz + (sz & 1);
        if (memcmp(id, "fmt ", 4) == 0) {
            uint16_t fmt, ch; uint32_t sr;
            fread(&fmt, 2, 1, f); fread(&ch, 2, 1, f); fread(&sr, 4, 1, f);
            if (fmt != 1 || ch != 1) { fclose(f); return -1; }
            *out_rate = sr;
        } else if (memcmp(id, "smpl", 4) == 0 && sz >= 60) {
            uint32_t fields[9];
            if (fread(fields, 4, 9, f) == 9 && fields[7] > 0) {
                uint32_t loop[6];
                if (fread(loop, 4, 6, f) == 6 && loop[2] < loop[3]) {
                    *out_loop_start = (int)loop[2];
                    *out_loop_end = (int)loop[3];
                }
            }
        } else if (memcmp(id, "data", 4) == 0 && got_fmt) {
            int n = sz / 2;
            int max_pool = (int)(sizeof(sample_pool)/sizeof(sample_pool[0]));
            if (pool_used + n > max_pool) { fclose(f); return -1; }
            *out_data = &sample_pool[pool_used];
            fread((int16_t *)&sample_pool[pool_used], 2, n, f);
            *out_len = n;
            pool_used += n;
            data_loaded = 1;
        }
        if (memcmp(id, "fmt ", 4) == 0) {
            got_fmt = 1;
        }
        fseek(f, next, SEEK_SET);
    }
    fclose(f);
    if (data_loaded) {
        if (*out_loop_end > *out_len) *out_loop_end = *out_len;
        if (*out_loop_start < 0 || *out_loop_start >= *out_loop_end) {
            *out_loop_start = 0;
            *out_loop_end = 0;
        }
        return 0;
    }
    return -1;
}

/* ---- Load WAV instrument from directory ---- */
static int zone_cmp(const void *a, const void *b) {
    const Zone *za = a;
    const Zone *zb = b;
    if (za->root_note != zb->root_note)
        return za->root_note - zb->root_note;
    return za->lo_vel - zb->lo_vel;
}

static int string_cmp(const void *a, const void *b)
{
    const char *const *sa = a;
    const char *const *sb = b;
    return strcasecmp(*sa, *sb);
}

static void normalize_instrument(Instrument *inst) {
    int32_t peak = 0;
    for (int z = 0; z < inst->num_zones; z++) {
        const int16_t *data = inst->zones[z].data;
        int len = inst->zones[z].len;
        for (int i = 0; i < len; i++) {
            int32_t v = data[i] < 0 ? -data[i] : data[i];
            if (v > peak) peak = v;
        }
    }
    if (peak > 0)
        inst->gain = 24000.0 / (double)peak;
    else
        inst->gain = 1.0;
    if (inst->gain > 4.0) inst->gain = 4.0;
    if (inst->gain < 0.1) inst->gain = 0.1;
}

static int load_wav_instrument(const char *dir)
{
    if (num_instruments >= MAX_INSTRUMENTS) return -1;
    Instrument *inst = &instruments[num_instruments];
    memset(inst, 0, sizeof(*inst));

    const char *slash = strrchr(dir, '/');
    const char *base = slash ? slash + 1 : dir;
    if (*base == '\0' && slash > dir) {
        const char *p = slash - 1;
        while (p > dir && *p != '/') p--;
        if (*p == '/') p++;
        int len = (int)(slash - p);
        if (len > 31) len = 31;
        memcpy(inst->name, p, len);
    } else {
        strncpy(inst->name, base, 31);
    }

    DIR *d = opendir(dir);
    if (!d) { perror(dir); return -1; }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (inst->num_zones >= MAX_ZONES) break;
        int namelen = strlen(ent->d_name);
        if (namelen < 5 || strcasecmp(ent->d_name + namelen - 4, ".wav") != 0) continue;
        int note;
        if (parse_note_name(ent->d_name, &note) < 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        Zone *z = &inst->zones[inst->num_zones];
        if (load_wav_file(path, &z->data, &z->len, &z->sample_rate,
                          &z->loop_start, &z->loop_end) < 0) continue;
        z->root_note = note;
        infer_velocity_range(ent->d_name, &z->lo_vel, &z->hi_vel);
        inst->num_zones++;
        printf("  zone %-32s note=%d vel=%d-%d loop=%d-%d\n",
               ent->d_name, z->root_note, z->lo_vel, z->hi_vel,
               z->loop_start, z->loop_end);
    }
    closedir(d);
    if (inst->num_zones == 0) return -1;

    qsort(inst->zones, inst->num_zones, sizeof(Zone), zone_cmp);
    for (int i = 0; i < inst->num_zones; i++) {
        inst->zones[i].lo_note = (i == 0) ? 0 :
            (inst->zones[i-1].root_note + inst->zones[i].root_note) / 2 + 1;
        inst->zones[i].hi_note = (i == inst->num_zones - 1) ? 127 :
            (inst->zones[i].root_note + inst->zones[i+1].root_note) / 2;
    }

    normalize_instrument(inst);
    printf("[%3d] WAV: %-20s (%d zones) gain=%.2f\n", num_instruments, inst->name, inst->num_zones, inst->gain);
    num_instruments++;
    return num_instruments - 1;
}

typedef struct {
    char name[21];
    uint16_t bag_index;
} SfPreset;

typedef struct {
    uint16_t gen_index;
} SfBag;

typedef struct {
    uint16_t oper;
    uint16_t amount;
} SfGen;

typedef struct {
    char name[21];
    uint16_t bag_index;
} SfInst;

typedef struct {
    char name[21];
    uint32_t start;
    uint32_t end;
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t sample_rate;
    uint8_t root_key;
} SfSample;

typedef struct {
    const unsigned char *ptr;
    uint32_t size;
} SfChunk;

static SfChunk sf2_find_chunk(const unsigned char *data, size_t size, const char id[4])
{
    SfChunk out = {0, 0};
    if (size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "sfbk", 4) != 0)
        return out;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const unsigned char *chunk = data + pos;
        uint32_t chunk_size = rd32(chunk + 4);
        size_t next = pos + 8 + chunk_size + (chunk_size & 1);
        if (next > size) break;

        if (memcmp(chunk, id, 4) == 0) {
            out.ptr = chunk + 8;
            out.size = chunk_size;
            return out;
        }

        if (memcmp(chunk, "LIST", 4) == 0 && chunk_size >= 4) {
            size_t sub = pos + 12;
            size_t end = pos + 8 + chunk_size;
            while (sub + 8 <= end) {
                const unsigned char *schunk = data + sub;
                uint32_t schunk_size = rd32(schunk + 4);
                size_t snext = sub + 8 + schunk_size + (schunk_size & 1);
                if (snext > end) break;
                if (memcmp(schunk, id, 4) == 0) {
                    out.ptr = schunk + 8;
                    out.size = schunk_size;
                    return out;
                }
                sub = snext;
            }
        }

        pos = next;
    }

    return out;
}

static int sf_gen_range_lo(uint16_t amount) { return amount & 0xff; }
static int sf_gen_range_hi(uint16_t amount) { return (amount >> 8) & 0xff; }

static int16_t sf_gen_s16(uint16_t amount) { return (int16_t)amount; }

static void zone_sort_and_fill(Instrument *inst)
{
    qsort(inst->zones, inst->num_zones, sizeof(Zone), zone_cmp);
    for (int i = 0; i < inst->num_zones; i++) {
        int prev_root = -1, next_root = -1;
        for (int j = i - 1; j >= 0; j--) {
            if (inst->zones[j].root_note != inst->zones[i].root_note) {
                prev_root = inst->zones[j].root_note;
                break;
            }
        }
        for (int j = i + 1; j < inst->num_zones; j++) {
            if (inst->zones[j].root_note != inst->zones[i].root_note) {
                next_root = inst->zones[j].root_note;
                break;
            }
        }
        if (inst->zones[i].lo_note < 0)
            inst->zones[i].lo_note = (prev_root < 0) ? 0 :
                (prev_root + inst->zones[i].root_note) / 2 + 1;
        if (inst->zones[i].hi_note < 0)
            inst->zones[i].hi_note = (next_root < 0) ? 127 :
                (inst->zones[i].root_note + next_root) / 2;
    }
}

static int load_sf2_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0) { fclose(f); return -1; }

    unsigned char *data = malloc((size_t)file_size);
    if (!data) { fclose(f); return -1; }
    if (fread(data, 1, (size_t)file_size, f) != (size_t)file_size) {
        fclose(f);
        free(data);
        return -1;
    }
    fclose(f);

    SfChunk smpl = sf2_find_chunk(data, (size_t)file_size, "smpl");
    SfChunk phdr = sf2_find_chunk(data, (size_t)file_size, "phdr");
    SfChunk pbag = sf2_find_chunk(data, (size_t)file_size, "pbag");
    SfChunk pgen = sf2_find_chunk(data, (size_t)file_size, "pgen");
    SfChunk inst = sf2_find_chunk(data, (size_t)file_size, "inst");
    SfChunk ibag = sf2_find_chunk(data, (size_t)file_size, "ibag");
    SfChunk igen = sf2_find_chunk(data, (size_t)file_size, "igen");
    SfChunk shdr = sf2_find_chunk(data, (size_t)file_size, "shdr");

    if (!smpl.ptr || !phdr.ptr || !pbag.ptr || !pgen.ptr || !inst.ptr ||
        !ibag.ptr || !igen.ptr || !shdr.ptr) {
        fprintf(stderr, "SF2: missing required chunks: %s\n", path);
        free(data);
        return -1;
    }

    int n_phdr = phdr.size / 38;
    int n_pbag = pbag.size / 4;
    int n_pgen = pgen.size / 4;
    int n_inst = inst.size / 22;
    int n_ibag = ibag.size / 4;
    int n_igen = igen.size / 4;
    int n_shdr = shdr.size / 46;
    if (n_phdr < 2 || n_pbag < 2 || n_inst < 2 || n_ibag < 2 || n_shdr < 2) {
        free(data);
        return -1;
    }

    int16_t *samples = malloc(smpl.size);
    if (!samples) {
        free(data);
        return -1;
    }
    memcpy(samples, smpl.ptr, smpl.size);
    if (num_sample_buffers < MAX_SAMPLE_BUFFERS) {
        sample_buffers[num_sample_buffers++] = samples;
    }

    SfPreset *presets = calloc((size_t)n_phdr, sizeof(SfPreset));
    SfBag *pbags = calloc((size_t)n_pbag, sizeof(SfBag));
    SfGen *pgens = calloc((size_t)n_pgen, sizeof(SfGen));
    SfInst *insts = calloc((size_t)n_inst, sizeof(SfInst));
    SfBag *ibags = calloc((size_t)n_ibag, sizeof(SfBag));
    SfGen *igens = calloc((size_t)n_igen, sizeof(SfGen));
    SfSample *shdrs = calloc((size_t)n_shdr, sizeof(SfSample));
    if (!presets || !pbags || !pgens || !insts || !ibags || !igens || !shdrs) {
        free(presets); free(pbags); free(pgens); free(insts);
        free(ibags); free(igens); free(shdrs); free(data);
        return -1;
    }

    for (int i = 0; i < n_phdr; i++) {
        const unsigned char *p = phdr.ptr + i * 38;
        copy_sf_name(presets[i].name, sizeof(presets[i].name), p, 20);
        presets[i].bag_index = rd16(p + 24);
    }
    for (int i = 0; i < n_pbag; i++) pbags[i].gen_index = rd16(pbag.ptr + i * 4);
    for (int i = 0; i < n_pgen; i++) {
        pgens[i].oper = rd16(pgen.ptr + i * 4);
        pgens[i].amount = rd16(pgen.ptr + i * 4 + 2);
    }
    for (int i = 0; i < n_inst; i++) {
        const unsigned char *p = inst.ptr + i * 22;
        copy_sf_name(insts[i].name, sizeof(insts[i].name), p, 20);
        insts[i].bag_index = rd16(p + 20);
    }
    for (int i = 0; i < n_ibag; i++) ibags[i].gen_index = rd16(ibag.ptr + i * 4);
    for (int i = 0; i < n_igen; i++) {
        igens[i].oper = rd16(igen.ptr + i * 4);
        igens[i].amount = rd16(igen.ptr + i * 4 + 2);
    }
    for (int i = 0; i < n_shdr; i++) {
        const unsigned char *p = shdr.ptr + i * 46;
        copy_sf_name(shdrs[i].name, sizeof(shdrs[i].name), p, 20);
        shdrs[i].start = rd32(p + 20);
        shdrs[i].end = rd32(p + 24);
        shdrs[i].loop_start = rd32(p + 28);
        shdrs[i].loop_end = rd32(p + 32);
        shdrs[i].sample_rate = rd32(p + 36);
        shdrs[i].root_key = p[40];
    }

    int added = 0;
    int max_presets = n_phdr - 1;  /* final record is EOP */
    for (int pidx = 0; pidx < max_presets && num_instruments < MAX_INSTRUMENTS; pidx++) {
        Instrument *out = &instruments[num_instruments];
        memset(out, 0, sizeof(*out));
        snprintf(out->name, sizeof(out->name), "%s", presets[pidx].name[0] ?
                 presets[pidx].name : path_basename(path));

        int pbag_start = presets[pidx].bag_index;
        int pbag_end = presets[pidx + 1].bag_index;
        if (pbag_start < 0 || pbag_end > n_pbag || pbag_start >= pbag_end) continue;

        for (int pb = pbag_start; pb < pbag_end && pb + 1 < n_pbag &&
             out->num_zones < MAX_ZONES; pb++) {
            int pkey_lo = 0, pkey_hi = 127, inst_id = -1;
            int pgen_start = pbags[pb].gen_index;
            int pgen_end = pbags[pb + 1].gen_index;
            if (pgen_start < 0 || pgen_end > n_pgen) continue;

            for (int g = pgen_start; g < pgen_end; g++) {
                if (pgens[g].oper == 43) {
                    pkey_lo = sf_gen_range_lo(pgens[g].amount);
                    pkey_hi = sf_gen_range_hi(pgens[g].amount);
                } else if (pgens[g].oper == 41) {
                    inst_id = pgens[g].amount;
                }
            }
            if (inst_id < 0 || inst_id >= n_inst - 1) continue;

            int ibag_start = insts[inst_id].bag_index;
            int ibag_end = insts[inst_id + 1].bag_index;
            if (ibag_start < 0 || ibag_end > n_ibag || ibag_start >= ibag_end) continue;

            for (int ib = ibag_start; ib < ibag_end && ib + 1 < n_ibag &&
                 out->num_zones < MAX_ZONES; ib++) {
                int key_lo = pkey_lo, key_hi = pkey_hi;
                int sample_id = -1, root_override = -1, sample_modes = 0;
                int start_offset = 0, end_offset = 0;
                int loop_start_offset = 0, loop_end_offset = 0;
                int igen_start = ibags[ib].gen_index;
                int igen_end = ibags[ib + 1].gen_index;
                if (igen_start < 0 || igen_end > n_igen) continue;

                for (int g = igen_start; g < igen_end; g++) {
                    if (igens[g].oper == 0) {
                        start_offset += sf_gen_s16(igens[g].amount);
                    } else if (igens[g].oper == 1) {
                        end_offset += sf_gen_s16(igens[g].amount);
                    } else if (igens[g].oper == 2) {
                        loop_start_offset += sf_gen_s16(igens[g].amount);
                    } else if (igens[g].oper == 3) {
                        loop_end_offset += sf_gen_s16(igens[g].amount);
                    } else if (igens[g].oper == 4) {
                        start_offset += sf_gen_s16(igens[g].amount) * 32768;
                    } else if (igens[g].oper == 12) {
                        end_offset += sf_gen_s16(igens[g].amount) * 32768;
                    } else if (igens[g].oper == 43) {
                        int lo = sf_gen_range_lo(igens[g].amount);
                        int hi = sf_gen_range_hi(igens[g].amount);
                        if (lo > key_lo) key_lo = lo;
                        if (hi < key_hi) key_hi = hi;
                    } else if (igens[g].oper == 45) {
                        loop_start_offset += sf_gen_s16(igens[g].amount) * 32768;
                    } else if (igens[g].oper == 50) {
                        loop_end_offset += sf_gen_s16(igens[g].amount) * 32768;
                    } else if (igens[g].oper == 53) {
                        sample_id = igens[g].amount;
                    } else if (igens[g].oper == 54) {
                        sample_modes = igens[g].amount;
                    } else if (igens[g].oper == 58) {
                        root_override = igens[g].amount;
                    }
                }
                if (sample_id < 0 || sample_id >= n_shdr - 1 || key_lo > key_hi) continue;

                SfSample *s = &shdrs[sample_id];
                int start = (int)s->start + start_offset;
                int end = (int)s->end + end_offset;
                int loop_start = (int)s->loop_start + loop_start_offset;
                int loop_end = (int)s->loop_end + loop_end_offset;

                if (start < 0) start = 0;
                if (end < start) end = start;
                if ((uint32_t)end * 2 > smpl.size) end = (int)(smpl.size / 2);
                if (loop_start < start) loop_start = start;
                if (loop_end > end) loop_end = end;

                if (end <= start || s->sample_rate == 0)
                    continue;

                Zone *z = &out->zones[out->num_zones++];
                z->data = samples + start;
                z->len = end - start;
                z->sample_rate = (int)s->sample_rate;
                z->root_note = (root_override >= 0) ? root_override : s->root_key;
                z->lo_note = key_lo;
                z->hi_note = key_hi;
                z->lo_vel = 0;
                z->hi_vel = 127;
                z->loop_start = 0;
                z->loop_end = 0;
                if ((sample_modes & 1) && loop_start >= start &&
                    loop_start < loop_end && loop_end <= end) {
                    z->loop_start = loop_start - start;
                    z->loop_end = loop_end - start;
                }
            }
        }

        if (out->num_zones > 0) {
            zone_sort_and_fill(out);
            normalize_instrument(out);
            printf("[%3d] SF2: %-20s (%d zones) gain=%.2f %s\n",
                   num_instruments, out->name, out->num_zones, out->gain, path_basename(path));
            __sync_synchronize();  /* ensure instrument data visible before count */
            num_instruments++;
            added++;
        }
    }

    free(presets); free(pbags); free(pgens); free(insts);
    free(ibags); free(igens); free(shdrs); free(data);
    return added > 0 ? added : -1;
}

static int load_instrument_path(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        perror(path);
        return -1;
    }

    if (S_ISREG(st.st_mode)) {
        if (has_suffix(path, ".bin")) {
            if (load_rom_pcm(path) == 0) {
                load_rom_instruments();
                return 0;
            }
        } else if (has_suffix(path, ".sf2")) {
            return load_sf2_file(path);
        }
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) return -1;

    int before = num_instruments;
    DIR *d = opendir(path);
    if (!d) { perror(path); return -1; }
    char *sf2_names[256];
    int num_sf2_names = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!has_suffix(ent->d_name, ".sf2")) continue;
        if (num_sf2_names >= (int)(sizeof(sf2_names) / sizeof(sf2_names[0]))) break;
        sf2_names[num_sf2_names++] = strdup(ent->d_name);
    }
    closedir(d);

    qsort(sf2_names, num_sf2_names, sizeof(sf2_names[0]), string_cmp);

    /* Load first SF2 immediately, defer the rest for background loading */
    if (num_sf2_names > 0 && num_instruments < MAX_INSTRUMENTS) {
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, sf2_names[0]);
        load_sf2_file(child);
        free(sf2_names[0]);
        sf2_names[0] = NULL;
    }

    /* Store remaining SF2 filenames for lazy background loading */
    if (num_sf2_names > 1) {
        snprintf(g_pending_dir, sizeof(g_pending_dir), "%s", path);
        g_num_pending = 0;
        for (int i = 1; i < num_sf2_names; i++) {
            g_pending_sf2[g_num_pending++] = sf2_names[i];
            sf2_names[i] = NULL;
        }
    }

    if (num_instruments > before) return num_instruments - before;
    return load_wav_instrument(path);
}

/* ---- Load ROM PCM data ---- */
static int load_rom_pcm(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    rom_pcm = malloc(sz);
    if (!rom_pcm) { fclose(f); return -1; }
    fread(rom_pcm, 1, sz, f);
    fclose(f);
    rom_pcm_len = sz / 2;
    printf("ROM PCM: %s (%d samples, %.1fs at %d Hz)\n",
           path, rom_pcm_len, (double)rom_pcm_len / ROM_SAMPLE_RATE, ROM_SAMPLE_RATE);
    return 0;
}

/* ---- Build instruments from ROM wave table ---- */
static void load_rom_instruments(void)
{
    if (!rom_pcm) return;
    for (int w = 0; w < NUM_ROM_WAVES; w++) {
        if (num_instruments >= MAX_INSTRUMENTS) break;
        const RomWave *rw = &g_rom_waves[w];
        if (rw->num_zones == 0) continue;

        Instrument *inst = &instruments[num_instruments];
        memset(inst, 0, sizeof(*inst));
        snprintf(inst->name, sizeof(inst->name), "%s", rw->name);

        int prev_hi = -1;
        for (int z = 0; z < rw->num_zones && z < MAX_ZONES; z++) {
            const RomZone *rz = &rw->zones[z];
            if (rz->length == 0) break;
            if ((int)rz->wav_offset + (int)rz->length > rom_pcm_len) break;

            Zone *zone = &inst->zones[inst->num_zones];
            zone->data = &rom_pcm[rz->wav_offset];
            zone->len = rz->length;
            zone->sample_rate = ROM_SAMPLE_RATE;
            zone->root_note = rz->root_key;
            zone->lo_note = prev_hi + 1;
            zone->hi_note = rz->high_key;
            zone->lo_vel = 0;
            zone->hi_vel = 127;
            zone->loop_start = rz->loop_start;
            zone->loop_end = 0;
            prev_hi = rz->high_key;
            inst->num_zones++;
        }

        if (inst->num_zones > 0) {
            normalize_instrument(inst);
            printf("[%3d] ROM: %-20s (%d zones) gain=%.2f\n", num_instruments, inst->name, inst->num_zones, inst->gain);
            num_instruments++;
        }
    }
}

/* ---- Find zone for note ---- */
static Zone *find_zone(Instrument *inst, int note, int vel)
{
    Zone *fallback = NULL;
    for (int i = 0; i < inst->num_zones; i++)
        if (note >= inst->zones[i].lo_note && note <= inst->zones[i].hi_note &&
            vel >= inst->zones[i].lo_vel && vel <= inst->zones[i].hi_vel)
            return &inst->zones[i];
        else if (!fallback && note >= inst->zones[i].lo_note && note <= inst->zones[i].hi_note)
            fallback = &inst->zones[i];
    if (fallback) return fallback;
    return &inst->zones[0];
}

static int alloc_voice(void)
{
    /* Prefer idle voices, then oldest releasing, then oldest active */
    int best = -1;
    uint32_t oldest = UINT32_MAX;

    /* First pass: idle voice */
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].note < 0) return i;
    }
    /* Second pass: oldest releasing voice */
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].env.stage == ENV_RELEASE && voices[i].age < oldest) {
            oldest = voices[i].age;
            best = i;
        }
    }
    if (best >= 0) return best;
    /* Third pass: oldest active voice */
    oldest = UINT32_MAX;
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].age < oldest) {
            oldest = voices[i].age;
            best = i;
        }
    }
    return best >= 0 ? best : 0;
}

/* ---- Envelope helpers ---- */
static void env_note_on(AmpEnv *e, double attack_ms, double decay_ms,
                        double sustain, double release_ms)
{
    double att_samples = attack_ms * SAMPLE_RATE / 1000.0;
    double dec_samples = decay_ms * SAMPLE_RATE / 1000.0;
    double rel_samples = release_ms * SAMPLE_RATE / 1000.0;

    e->stage = ENV_ATTACK;
    e->value = 0.0;
    e->attack_rate  = (att_samples > 0) ? 1.0 / att_samples : 1.0;
    e->decay_rate   = (dec_samples > 0) ? (1.0 - sustain) / dec_samples : 1.0;
    e->sustain      = sustain;
    e->release_rate = (rel_samples > 0) ? 1.0 / rel_samples : 1.0;
}

static void env_note_off(AmpEnv *e)
{
    if (e->stage != ENV_IDLE)
        e->stage = ENV_RELEASE;
}

/* Returns envelope value 0.0-1.0, sets note=-1 via return -1 when done */
static double env_tick(AmpEnv *e)
{
    switch (e->stage) {
    case ENV_ATTACK:
        e->value += e->attack_rate;
        if (e->value >= 1.0) {
            e->value = 1.0;
            e->stage = ENV_DECAY;
        }
        break;
    case ENV_DECAY:
        e->value -= e->decay_rate;
        if (e->value <= e->sustain) {
            e->value = e->sustain;
            e->stage = ENV_SUSTAIN;
        }
        break;
    case ENV_SUSTAIN:
        break;
    case ENV_RELEASE:
        e->value -= e->release_rate;
        if (e->value <= 0.001) {
            e->value = 0.0;
            e->stage = ENV_IDLE;
        }
        break;
    default:
        return 0.0;
    }
    return e->value;
}

/* ---- MIDI CC handler ---- */
static void handle_cc(int cc, int val)
{
    switch (cc) {
    case 7:   /* Volume */
        g_volume = val / 127.0;
        printf("CC7  Volume: %.0f%%\n", g_volume * 100);
        break;
    case 71:  /* Resonance */
        /* 0→Q=0.5, 64→Q=2, 127→Q=15 (exponential) */
        g_resonance = 0.5 * pow(30.0, val / 127.0);
        printf("CC71 Resonance: Q=%.1f\n", g_resonance);
        break;
    case 70:  /* Sustain level */
        g_sustain = val / 127.0;
        printf("CC70 Sustain: %.0f%%\n", g_sustain * 100.0);
        break;
    case 72:  /* Release time */
        /* 0→10ms, 64→500ms, 127→15000ms */
        g_release_ms = 10.0 * pow(1500.0, val / 127.0);
        printf("CC72 Release: %.0fms\n", g_release_ms);
        break;
    case 73:  /* Attack time */
        /* 0→1ms, 64→50ms, 127→1000ms */
        g_attack_ms = 1.0 * pow(1000.0, val / 127.0);
        printf("CC73 Attack: %.0fms\n", g_attack_ms);
        break;
    case 75:  /* Decay time */
        g_decay_ms = 10.0 * pow(1000.0, val / 127.0);
        printf("CC75 Decay: %.0fms\n", g_decay_ms);
        break;
    case 74:  /* Filter cutoff (brightness) */
        /* 0→100Hz, 64→2500Hz, 127→16000Hz (exponential) */
        g_cutoff_hz = 100.0 * pow(160.0, val / 127.0);
        if (g_cutoff_hz > 16000.0) g_cutoff_hz = 16000.0;
        printf("CC74 Cutoff: %.0f Hz\n", g_cutoff_hz);
        break;
    case 76:  /* LFO rate */
        /* 0→0.1Hz, 64→3Hz, 127→20Hz */
        g_lfo_rate = 0.1 * pow(200.0, val / 127.0);
        printf("CC76 LFO Rate: %.1f Hz\n", g_lfo_rate);
        break;
    case 77:  /* LFO → filter depth */
        g_lfo_filt = val / 127.0;
        printf("CC77 LFO→Filter: %.0f%%\n", g_lfo_filt * 100);
        break;
    case 78:  /* LFO → pitch depth */
        g_lfo_pitch = val / 127.0;
        printf("CC78 LFO→Pitch: %.0f%%\n", g_lfo_pitch * 100);
        break;
    case 79:  /* LFO → amplitude depth */
        g_lfo_amp = val / 127.0;
        printf("CC79 LFO→Amp: %.0f%%\n", g_lfo_amp * 100);
        break;
    case 80:  /* LFO waveform */
        {
            int new_wave = (val * 4) / 128;
            if (new_wave != g_lfo_wave) {
                g_lfo_prev_wave = g_lfo_wave;
                g_lfo_xfade = 0.0;
                g_lfo_wave = new_wave;
            }
        }
        printf("CC80 LFO Wave: %s\n",
            g_lfo_wave == 0 ? "Triangle" :
            g_lfo_wave == 1 ? "Sine" :
            g_lfo_wave == 2 ? "Square" : "Saw");
        break;
    case 82:  /* Filter envelope amount */
        g_filt_env = val / 127.0;
        printf("CC82 FiltEnv: %.0f%%\n", g_filt_env * 100);
        break;
    case 5:   /* Portamento time */
        g_portamento = (val == 0) ? 0.0 : 0.005 * pow(400.0, val / 127.0);
        printf("CC5 Portamento: %.3fs\n", g_portamento);
        break;
    case 93:  /* Chorus mix */
        g_chorus_mix = val / 127.0;
        printf("CC93 Chorus: %.0f%%\n", g_chorus_mix * 100);
        break;
    case 91:  /* Reverb mix */
        g_reverb_mix = val / 127.0;
        printf("CC91 Reverb: %.0f%%\n", g_reverb_mix * 100);
        break;
    }
}

static void start_note(int note, int vel)
{
    Instrument *inst = &instruments[current_inst];
    int v = alloc_voice();
    Zone *z = find_zone(inst, note, vel);
    double ratio = pow(2.0, (note - z->root_note) / 12.0);
    ratio *= (double)z->sample_rate / SAMPLE_RATE;
    /* Per-voice random detune: +/- 0.3 cents */
    double detune_cents = ((rand() % 600) - 300) / 1000.0;
    ratio *= pow(2.0, detune_cents / 1200.0);
    voices[v].note = note;
    voices[v].vel = vel;
    voices[v].pos = 0.0;
    voices[v].target_rate = ratio;
    if (g_portamento > 0.001 && g_last_note >= 0 && g_last_note != note) {
        double lr = pow(2.0, (g_last_note - z->root_note) / 12.0);
        lr *= (double)z->sample_rate / SAMPLE_RATE;
        voices[v].rate = lr;
    } else {
        voices[v].rate = ratio;
    }
    g_last_note = note;
    voices[v].age = ++voice_age;
    voices[v].zone = z;
    voices[v].zone_gain = inst->gain;
    voices[v].filt.low = 0.0;
    voices[v].filt.band = 0.0;
    env_note_on(&voices[v].env, g_attack_ms, g_decay_ms, g_sustain, g_release_ms);
    printf("NOTE ON  note=%d vel=%d inst=%d:%s zone=%d-%d velzone=%d-%d root=%d rate=%d len=%d loop=%d-%d\n",
           note, vel, current_inst, inst->name, z->lo_note, z->hi_note,
           z->lo_vel, z->hi_vel, z->root_note, z->sample_rate, z->len,
           z->loop_start, z->loop_end);
}

static void stop_note(int note)
{
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].note == note &&
            voices[i].env.stage != ENV_RELEASE &&
            voices[i].env.stage != ENV_IDLE) {
            env_note_off(&voices[i].env);
        }
    }
    printf("NOTE OFF note=%d\n", note);
}

/* ---- MIDI thread ---- */
static void *midi_thread(void *arg)
{
    int fd = open((const char *)arg, O_RDONLY);
    if (fd < 0) { perror("MIDI"); return NULL; }
    printf("MIDI: %s\n", (const char *)arg);

    unsigned char buf[3];
    int pos = 0, expected = 0;

    while (1) {
        unsigned char byte;
        if (read(fd, &byte, 1) != 1) continue;

        if (byte & 0x80) {
            buf[0] = byte;
            pos = 1;
            switch (byte & 0xF0) {
                case 0x90: case 0x80: case 0xB0: expected = 3; break;
                case 0xC0: expected = 2; break;
                default: expected = 0; pos = 0; break;
            }
            continue;
        }

        if (pos > 0 && pos < expected) {
            buf[pos++] = byte;
            if (pos == expected) {
                unsigned char st = buf[0] & 0xF0;
                pthread_mutex_lock(&vlock);

                if (st == 0x90 && buf[2] > 0) {
                    start_note(buf[1], buf[2]);
                } else if (st == 0x80 || (st == 0x90 && buf[2] == 0)) {
                    stop_note(buf[1]);
                } else if (st == 0xB0) {
                    /* CC */
                    handle_cc(buf[1], buf[2]);
                } else if (st == 0xC0) {
                    /* Program change — reset to defaults */
                    int prog = buf[1] % num_instruments;
                    current_inst = prog;
                    for (int i = 0; i < NUM_VOICES; i++)
                        voices[i].note = -1;
                    /* Reset all CC parameters to init state */
                    g_cutoff_hz  = 6000.0;
                    g_resonance  = 1.0;
                    g_attack_ms  = 5.0;
                    g_decay_ms   = 200.0;
                    g_sustain    = 0.8;
                    g_release_ms = 80.0;
                    g_volume     = 1.0;
                    g_lfo_rate   = 2.0;
                    g_lfo_phase  = 0.0;
                    g_lfo_filt   = 0.0;
                    g_lfo_pitch  = 0.0;
                    g_lfo_amp    = 0.0;
                    g_lfo_wave   = 0;
                    g_filt_env   = 0.0;
                    g_portamento = 0.0;
                    g_chorus_mix = 0.0;
                    g_reverb_mix = 0.0;
                    g_last_note  = -1;
                    printf("PROG %d: %s\n", prog, instruments[prog].name);
                }

                pthread_mutex_unlock(&vlock);
                pos = 1;
            }
        }
    }
    close(fd);
    return NULL;
}

/* ---- Background SF2 loader thread ---- */
static void *loader_thread(void *arg)
{
    (void)arg;
    int total = num_instruments + g_num_pending;
    for (int i = 0; i < g_num_pending; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", g_pending_dir, g_pending_sf2[i]);
        load_sf2_file(path);
        free(g_pending_sf2[i]);
        g_pending_sf2[i] = NULL;
        if ((i + 1) % 10 == 0)
            printf("  ... %d/%d instruments loaded\n", num_instruments, total);
    }
    g_num_pending = 0;
    printf("\nAll %d instruments loaded.\n", num_instruments);
    return NULL;
}

static void *demo_thread(void *arg)
{
    (void)arg;
    const int notes[] = {48, 55, 60, 64, 67, 72};
    sleep(2);
    pthread_mutex_lock(&vlock);
    current_inst = 0;
    printf("DEMO starting on %d:%s\n", current_inst, instruments[current_inst].name);
    for (int i = 0; i < (int)(sizeof(notes) / sizeof(notes[0])); i++)
        start_note(notes[i], 110);
    pthread_mutex_unlock(&vlock);

    sleep(4);
    pthread_mutex_lock(&vlock);
    for (int i = 0; i < (int)(sizeof(notes) / sizeof(notes[0])); i++)
        stop_note(notes[i]);
    pthread_mutex_unlock(&vlock);
    return NULL;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("=== Rompler (SF2/WAV/ROM + envelope + filter) ===\n");

    const char *midi_dev = "/dev/snd/midiC2D0";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            midi_dev = argv[++i];
        } else if (strcmp(argv[i], "--demo") == 0) {
            g_demo_mode = 1;
        } else {
            load_instrument_path(argv[i]);
        }
    }

    if (num_instruments == 0) {
        fprintf(stderr, "No instruments loaded!\n");
        fprintf(stderr, "Usage: %s [jd800_pcm.bin] [sf2_files_or_dirs] [wav_dirs] [-m midi_dev]\n", argv[0]);
        return 1;
    }

    if (g_num_pending > 0)
        printf("\n%d instrument loaded (%d more loading in background). Program change to switch.\n",
               num_instruments, g_num_pending);
    else
        printf("\n%d instruments loaded. Program change to switch.\n", num_instruments);
    printf("Current: [0] %s\n", instruments[0].name);
    printf("\nMIDI CC: 74=cutoff, 71=reso, 73=atk, 75=dec, 70=sus, 72=rel, 82=filtenv, 5=porta, 93=chorus, 91=reverb\n");
    printf("Defaults: cutoff=%.0fHz Q=%.1f atk=%.0fms dec=%.0fms sus=%.0f%% rel=%.0fms\n\n",
           g_cutoff_hz, g_resonance, g_attack_ms, g_decay_ms, g_sustain * 100.0, g_release_ms);

    for (int i = 0; i < NUM_VOICES; i++) voices[i].note = -1;

    /* Audio setup */
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hw;
    int err;

    err = snd_pcm_open(&pcm, "hw:Headphones,0", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) { fprintf(stderr, "Audio: %s\n", snd_strerror(err)); return 1; }

    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, CHANNELS);
    unsigned int rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0);
    snd_pcm_uframes_t period = PERIOD_SIZE;
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, 0);
    unsigned int nperiods = PERIODS;
    snd_pcm_hw_params_set_periods_near(pcm, hw, &nperiods, 0);

    err = snd_pcm_hw_params(pcm, hw);
    if (err < 0) { fprintf(stderr, "HW: %s\n", snd_strerror(err)); return 1; }
    printf("Audio: rate=%u period=%lu voices=%d\n\n", rate, period, NUM_VOICES);

    pthread_t tid;
    if (g_demo_mode)
        pthread_create(&tid, NULL, demo_thread, NULL);
    else
        pthread_create(&tid, NULL, midi_thread, (void *)midi_dev);
    printf("Ready — play!\n");

    /* Background-load remaining SF2 instruments */
    pthread_t loader_tid;
    if (g_num_pending > 0)
        pthread_create(&loader_tid, NULL, loader_thread, NULL);

    /* Set audio thread to real-time FIFO priority */
    struct sched_param sp;
    sp.sched_priority = 80;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) == 0)
        printf("RT priority: SCHED_FIFO (80)\n");
    else
        printf("RT priority: failed (running as normal)\n");

    int16_t outbuf[MAX_PERIOD];
    unsigned peak_counter = 0;
    unsigned xrun_count = 0;
    double prev_scale = 1.0 / 32768.0;
    double smooth_fc = 6000.0;
    double smooth_q  = 1.0;
    double bypass_amt = 0.0;  /* 0 = filter active, 1 = fully bypassed */
    double smooth_lfo_filt  = 0.0;
    double smooth_lfo_pitch = 0.0;
    double smooth_lfo_amp   = 0.0;
    double smooth_filt_env  = 0.0;

    /* Pre-warm ALSA buffer with silence to prevent initial underrun */
    memset(outbuf, 0, sizeof(outbuf));
    for (unsigned p = 0; p < nperiods; p++)
        snd_pcm_writei(pcm, outbuf, period);

    while (1) {
        int32_t mix[MAX_PERIOD];
        memset(mix, 0, period * sizeof(int32_t));

        /* Smooth all CC parameters to prevent pops */
        double target_fc = g_cutoff_hz;
        double target_q  = 1.0 / g_resonance;
        double target_lf = g_lfo_filt;
        double target_lp = g_lfo_pitch;
        double target_la = g_lfo_amp;
        double target_fe = g_filt_env;
        double fc_step = (target_fc - smooth_fc) / (double)period;
        double q_step  = (target_q  - smooth_q)  / (double)period;
        double lf_step = (target_lf - smooth_lfo_filt)  / (double)period;
        double lp_step = (target_lp - smooth_lfo_pitch) / (double)period;
        double la_step = (target_la - smooth_lfo_amp)   / (double)period;
        double fe_step = (target_fe - smooth_filt_env)  / (double)period;
        double bypass_target = (target_fc >= 13000.0 && target_lf < 0.001 && g_filt_env < 0.001) ? 1.0 : 0.0;
        double bypass_step = (bypass_target - bypass_amt) / (double)period;

        /* Precompute LFO-derived values for this buffer */
        double lfo_svf_f[MAX_PERIOD];
        double lfo_svf_q[MAX_PERIOD];
        double lfo_bypass[MAX_PERIOD];
        double lfo_spd_mul[MAX_PERIOD];
        double lfo_amp_mul[MAX_PERIOD];
        double lfo_filt_env[MAX_PERIOD];
        double ramp_fc = smooth_fc;
        double ramp_q  = smooth_q;
        double ramp_bp = bypass_amt;
        double ramp_lf = smooth_lfo_filt;
        double ramp_lp = smooth_lfo_pitch;
        double ramp_la = smooth_lfo_amp;
        double ramp_fe = smooth_filt_env;
        for (int i = 0; i < (int)period; i++) {
            double lfo = lfo_tick();
            ramp_fc += fc_step;
            ramp_q  += q_step;
            ramp_bp += bypass_step;
            ramp_lf += lf_step;
            ramp_lp += lp_step;
            ramp_la += la_step;
            ramp_fe += fe_step;
            lfo_filt_env[i] = ramp_fe;
            /* Filter cutoff modulation */
            double fc = ramp_fc;
            if (ramp_lf > 0.001)
                fc *= fast_pow2(lfo * ramp_lf * 2.0);
            if (fc > 14000.0) fc = 14000.0;
            if (fc < 20.0) fc = 20.0;
            double f = fast_svf_coeff(fc, SAMPLE_RATE);
            lfo_svf_f[i] = (f > 1.8) ? 1.8 : f;
            lfo_svf_q[i] = ramp_q;
            lfo_bypass[i] = ramp_bp;
            /* Pitch modulation */
            lfo_spd_mul[i] = (ramp_lp > 0.001)
                ? fast_pow2(lfo * ramp_lp / 12.0) : 1.0;
            /* Amplitude modulation */
            lfo_amp_mul[i] = 1.0 - ramp_la * 0.5 * (1.0 - lfo);
        }
        smooth_fc = target_fc;
        smooth_q  = target_q;
        bypass_amt = bypass_target;
        smooth_lfo_filt  = target_lf;
        smooth_lfo_pitch = target_lp;
        smooth_lfo_amp   = target_la;
        smooth_filt_env  = target_fe;

        /* Copy-in: snapshot voice state under lock, then release */
        Voice local_voices[NUM_VOICES];
        double local_volume, local_portamento;
        pthread_mutex_lock(&vlock);
        memcpy(local_voices, voices, sizeof(voices));
        local_volume = g_volume;
        local_portamento = g_portamento;
        pthread_mutex_unlock(&vlock);

        for (int v = 0; v < NUM_VOICES; v++) {
            if (local_voices[v].note < 0) continue;
            Zone *z = local_voices[v].zone;
            double pos = local_voices[v].pos;
            double cur_rate = local_voices[v].rate;
            double tgt_rate = local_voices[v].target_rate;
            double base_amp = (local_voices[v].vel / 127.0) * local_volume * local_voices[v].zone_gain;
            double filt_low = local_voices[v].filt.low;
            double filt_band = local_voices[v].filt.band;
            AmpEnv env = local_voices[v].env;
            double glide_k = (local_portamento > 0.001)
                ? (1.0 - exp(-1.0 / (local_portamento * SAMPLE_RATE))) : 1.0;
            double fe = 0; /* set per-sample from lfo_filt_env[] */

            for (int i = 0; i < (int)period; i++) {
                double svf_f = lfo_svf_f[i];
                if (glide_k < 1.0)
                    cur_rate += (tgt_rate - cur_rate) * glide_k;
                double spd = cur_rate * lfo_spd_mul[i];
                double amp = base_amp * lfo_amp_mul[i];

                /* Envelope tick */
                double env_val = env_tick(&env);
                if (env.stage == ENV_IDLE) {
                    local_voices[v].note = -1;
                    break;
                }

                /* Wrap position if past the SF2 sustain loop, or finish one-shots. */
                int loop_len = z->loop_end - z->loop_start;
                int fade_len = 0;
                if (loop_len > 0) {
                    fade_len = LOOP_XFADE_SAMPLES;
                    if (fade_len > loop_len / 4)
                        fade_len = loop_len / 4;
                }
                int play_end = (loop_len > 0) ? z->loop_end : z->len;
                if ((int)pos >= play_end) {
                    if (loop_len > 0) {
                        double wrapped = fmod(pos - z->loop_start, loop_len);
                        pos = z->loop_start + wrapped;
                        if (fade_len > 0 && pos < z->loop_start + fade_len)
                            pos += fade_len;
                        if (pos >= z->loop_end)
                            pos = z->loop_start + fmod(pos - z->loop_start, loop_len);
                    } else {
                        local_voices[v].note = -1;
                        break;
                    }
                }

                int idx = (int)pos;
                double frac = pos - idx;

                /* 4-point Hermite interpolation */
                int in_loop = (loop_len > 0 && idx >= z->loop_start);
                double sm1 = fetch_sample(z, idx - 1, z->loop_start, play_end, loop_len, in_loop);
                double s0  = z->data[idx];
                double s1  = fetch_sample(z, idx + 1, z->loop_start, play_end, loop_len, in_loop);
                double s2  = fetch_sample(z, idx + 2, z->loop_start, play_end, loop_len, in_loop);
                double c0 = s0;
                double c1 = 0.5 * (s1 - sm1);
                double c2 = sm1 - 2.5 * s0 + 2.0 * s1 - 0.5 * s2;
                double c3 = 0.5 * (s2 - sm1) + 1.5 * (s0 - s1);
                double s = ((c3 * frac + c2) * frac + c1) * frac + c0;

                if (loop_len > 0 && fade_len > 0) {
                    int fade_start = z->loop_end - fade_len;
                    if (fade_start < z->loop_start)
                        fade_start = z->loop_start;
                    if (idx >= fade_start && idx < z->loop_end) {
                        int rel = idx - fade_start;
                        int wrap_idx = z->loop_start + rel;
                        if (wrap_idx >= z->loop_start && wrap_idx + 1 < z->loop_end) {
                            double ws0 = z->data[wrap_idx];
                            double ws1 = z->data[wrap_idx + 1];
                            double wrapped = ws0 + (ws1 - ws0) * frac;
                            double x = (double)rel / (double)fade_len;
                            double a = cos(x * M_PI * 0.5);
                            double b = sin(x * M_PI * 0.5);
                            s = s * a + wrapped * b;
                        }
                    }
                }

                /* Filter: always run SVF, crossfade to bypass when cutoff wide open */
                double cur_q = lfo_svf_q[i];
                /* Filter envelope modulation */
                fe = lfo_filt_env[i];
                if (fe > 0.001)
                    svf_f *= (1.0 + env_val * fe * 15.0);
                /* Clamp f for stability: must be < 2*damping (2/Q) */
                double f_max = 1.9 * cur_q;
                if (f_max > 1.8) f_max = 1.8;
                if (svf_f > f_max) svf_f = f_max;

                /* SVF lowpass filter */
                double high = s - filt_low - cur_q * filt_band;
                filt_band += svf_f * high;
                /* Saturate feedback path — adds analog warmth */
                double fb = filt_band / 32768.0;
                if (fb > 3.0) fb = 1.0;
                else if (fb < -3.0) fb = -1.0;
                else fb = fb * (27.0 + fb * fb) / (27.0 + 9.0 * fb * fb);
                filt_band = fb * 32768.0;
                filt_low  += svf_f * filt_band;
                if (filt_low > 65536.0) filt_low = 65536.0;
                else if (filt_low < -65536.0) filt_low = -65536.0;

                double bp = lfo_bypass[i];
                double out_s = filt_low * (1.0 - bp) + s * bp;

                /* Output: filtered signal * envelope * amplitude */
                mix[i] += (int32_t)(out_s * amp * env_val);
                pos += spd;
            }

            local_voices[v].pos = pos;
            local_voices[v].rate = cur_rate;
            local_voices[v].filt.low = filt_low;
            local_voices[v].filt.band = filt_band;
            local_voices[v].env = env;
        }

        /* Copy-out: write back rendered state under brief lock */
        pthread_mutex_lock(&vlock);
        for (int v = 0; v < NUM_VOICES; v++) {
            if (local_voices[v].note < 0 && voices[v].note >= 0 &&
                voices[v].age != local_voices[v].age) {
                /* MIDI thread started a genuinely NEW note during render — keep it */
                continue;
            }
            voices[v].pos = local_voices[v].pos;
            voices[v].rate = local_voices[v].rate;
            voices[v].filt = local_voices[v].filt;
            /* Merge envelope: if MIDI thread sent note-off during render,
               honor the release but use the rendered amplitude level */
            if (voices[v].env.stage == ENV_RELEASE &&
                local_voices[v].env.stage != ENV_RELEASE &&
                local_voices[v].env.stage != ENV_IDLE) {
                voices[v].env.value = local_voices[v].env.value;
            } else {
                voices[v].env = local_voices[v].env;
            }
            if (local_voices[v].note < 0)
                voices[v].note = -1;
        }
        pthread_mutex_unlock(&vlock);

        /* Count active voices for mix scaling — ramp smoothly to prevent clicks */
        int active = 0;
        for (int v = 0; v < NUM_VOICES; v++)
            if (voices[v].note >= 0) active++;
        double target_scale = 1.0 / (32768.0 * (active > 2 ? active * 0.5 : 1.0));
        double scale_step = (target_scale - prev_scale) / (double)period;
        double cur_scale = prev_scale;

        int32_t buf_peak = 0;
        for (int i = 0; i < (int)period; i++) {
            int32_t av = mix[i] < 0 ? -mix[i] : mix[i];
            if (av > buf_peak) buf_peak = av;
        }

        if (buf_peak < 32) {
            /* Noise gate: silence when output is negligible */
            memset(outbuf, 0, period * sizeof(int16_t));
        } else {
            for (int i = 0; i < (int)period; i++) {
                /* Soft-clip via tanh approximation */
                double s = mix[i] * cur_scale;
                cur_scale += scale_step;
                if (s > 3.0) s = 1.0;
                else if (s < -3.0) s = -1.0;
                else s = s * (27.0 + s * s) / (27.0 + 9.0 * s * s);
                outbuf[i] = (int16_t)(s * 32000.0);
            }
        }
        prev_scale = target_scale;

        /* ---- Chorus (triangle LFO, no sin()) ---- */
        if (g_chorus_mix > 0.001) {
            double cmix = g_chorus_mix * 0.5;
            double cdry = 1.0 - cmix;
            for (int i = 0; i < (int)period; i++) {
                float in = outbuf[i];
                chorus_buf[chorus_wpos] = in;
                chorus_phase1 += 0.5 / SAMPLE_RATE;
                if (chorus_phase1 >= 1.0) chorus_phase1 -= 1.0;
                chorus_phase2 += 0.7 / SAMPLE_RATE;
                if (chorus_phase2 >= 1.0) chorus_phase2 -= 1.0;
                double p1 = chorus_phase1, p2 = chorus_phase2;
                double cs1 = (p1 < 0.5) ? (4.0*p1 - 1.0) : (3.0 - 4.0*p1);
                double cs2 = (p2 < 0.5) ? (4.0*p2 - 1.0) : (3.0 - 4.0*p2);
                /* Fractional delay with linear interpolation */
                double fd1 = 600.0 + 400.0 * cs1;
                double fd2 = 800.0 + 500.0 * cs2;
                int id1 = (int)fd1; double fr1 = fd1 - id1;
                int id2 = (int)fd2; double fr2 = fd2 - id2;
                int pos1a = (chorus_wpos - id1 + CHORUS_BUF_SIZE) & (CHORUS_BUF_SIZE - 1);
                int pos1b = (pos1a - 1 + CHORUS_BUF_SIZE) & (CHORUS_BUF_SIZE - 1);
                int pos2a = (chorus_wpos - id2 + CHORUS_BUF_SIZE) & (CHORUS_BUF_SIZE - 1);
                int pos2b = (pos2a - 1 + CHORUS_BUF_SIZE) & (CHORUS_BUF_SIZE - 1);
                float t1 = chorus_buf[pos1a] + (chorus_buf[pos1b] - chorus_buf[pos1a]) * (float)fr1;
                float t2 = chorus_buf[pos2a] + (chorus_buf[pos2b] - chorus_buf[pos2a]) * (float)fr2;
                outbuf[i] = (int16_t)(in * cdry + (t1 + t2) * 0.5 * cmix);
                chorus_wpos = (chorus_wpos + 1) & (CHORUS_BUF_SIZE - 1);
            }
        }

        /* ---- Reverb (Schroeder) ---- */
        if (g_reverb_mix > 0.001) {
            double rmix = g_reverb_mix * 0.5;
            double rdry = 1.0 - rmix;
            for (int i = 0; i < (int)period; i++) {
                float in = outbuf[i] / 32000.0f;
                /* 4 parallel comb filters */
                float c1 = rv_c1[rv_c1p]; rv_c1[rv_c1p] = in + c1 * 0.84f;
                rv_c1p = (rv_c1p + 1 < RV_COMB1) ? rv_c1p + 1 : 0;
                float c2 = rv_c2[rv_c2p]; rv_c2[rv_c2p] = in + c2 * 0.84f;
                rv_c2p = (rv_c2p + 1 < RV_COMB2) ? rv_c2p + 1 : 0;
                float c3 = rv_c3[rv_c3p]; rv_c3[rv_c3p] = in + c3 * 0.84f;
                rv_c3p = (rv_c3p + 1 < RV_COMB3) ? rv_c3p + 1 : 0;
                float c4 = rv_c4[rv_c4p]; rv_c4[rv_c4p] = in + c4 * 0.84f;
                rv_c4p = (rv_c4p + 1 < RV_COMB4) ? rv_c4p + 1 : 0;
                float sum = (c1 + c2 + c3 + c4) * 0.25f;
                /* 2 series allpass filters */
                float a1d = rv_a1[rv_a1p];
                rv_a1[rv_a1p] = sum + a1d * 0.5f;
                rv_a1p = (rv_a1p + 1 < RV_AP1) ? rv_a1p + 1 : 0;
                sum = a1d - sum * 0.5f;
                float a2d = rv_a2[rv_a2p];
                rv_a2[rv_a2p] = sum + a2d * 0.5f;
                rv_a2p = (rv_a2p + 1 < RV_AP2) ? rv_a2p + 1 : 0;
                sum = a2d - sum * 0.5f;
                outbuf[i] = (int16_t)(outbuf[i] * rdry + sum * 32000.0f * rmix);
            }
        }

        if (++peak_counter >= 86) {
            if (xrun_count > 0) {
                printf("xruns=%u\n", xrun_count);
            }
            peak_counter = 0;
        }

        snd_pcm_sframes_t frames = snd_pcm_writei(pcm, outbuf, period);
        if (frames < 0) {
            if (frames == -EPIPE) xrun_count++;
            frames = snd_pcm_recover(pcm, frames, 0);
        }
        if (frames < 0) break;
    }

    snd_pcm_close(pcm);
    free(rom_pcm);
    for (int i = 0; i < num_sample_buffers; i++)
        free(sample_buffers[i]);
    return 0;
}
