/*
 * FatFs API shim for Teensy 4.1
 *
 * Provides the subset of FatFs types and functions used by sample_bank.c,
 * implemented in sd_wrapper.cpp using SdFat (Teensy's built-in SD library).
 * This lets the synth engine compile unchanged against the FatFs API.
 */
#ifndef FF_H
#define FF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Basic types matching FatFs conventions ─────────────────── */
typedef unsigned int  UINT;
typedef unsigned char BYTE;
typedef char          TCHAR;
typedef uint32_t      FSIZE_t;
typedef uint32_t      DWORD;
typedef uint16_t      WORD;

/* ── Result codes ───────────────────────────────────────────── */
typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED,
    FR_TIMEOUT,
    FR_LOCKED,
    FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES
} FRESULT;

/* ── File access mode flags ─────────────────────────────────── */
#define FA_READ     0x01

/* ── File attribute flags ───────────────────────────────────── */
#define AM_DIR      0x10

/* ── File object (thin handle — actual File lives in sd_wrapper pool) ── */
typedef struct {
    int      _handle;   /* index into sd_wrapper file pool */
    uint32_t fptr;      /* current read position (mirrors SdFat) */
} FIL;

/* ── Directory object ───────────────────────────────────────── */
typedef struct {
    int _handle;        /* index into sd_wrapper dir pool */
} DIR;

/* ── File information (returned by f_readdir) ───────────────── */
typedef struct {
    DWORD fsize;        /* file size in bytes */
    BYTE  fattrib;      /* attribute flags (AM_DIR etc.) */
    TCHAR fname[256];   /* entry name (null-terminated) */
} FILINFO;

/* ── File operations ────────────────────────────────────────── */
FRESULT f_open   (FIL *fp, const TCHAR *path, BYTE mode);
FRESULT f_read   (FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_close  (FIL *fp);
FRESULT f_lseek  (FIL *fp, FSIZE_t ofs);

/* f_tell — return current file read/write pointer */
static inline FSIZE_t f_tell(FIL *fp) { return fp->fptr; }

/* ── Directory operations ───────────────────────────────────── */
FRESULT f_opendir  (DIR *dp, const TCHAR *path);
FRESULT f_readdir  (DIR *dp, FILINFO *fno);
FRESULT f_closedir (DIR *dp);

#ifdef __cplusplus
}
#endif

#endif /* FF_H */
