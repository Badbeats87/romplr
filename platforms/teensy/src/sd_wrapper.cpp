/*
 * SD Card Wrapper — maps FatFs API to SdFat for Teensy 4.1
 *
 * sample_bank.c calls FatFs functions (f_open, f_read, etc.).
 * This file implements them using SdFat over the Teensy 4.1's
 * built-in SDIO slot, so the synth engine compiles unchanged.
 */
#include <SdFat.h>
#include "fatfs/ff.h"
#include <string.h>

/* ── SdFat instance (SDIO, Teensy 4.1 built-in slot) ────────── */
static SdFat g_sd;
static bool  g_sd_ok = false;

/* ── File handle pool ────────────────────────────────────────── */
#define MAX_OPEN_FILES 12
static FsFile g_files[MAX_OPEN_FILES];
static bool   g_file_used[MAX_OPEN_FILES];

/* ── Directory handle pool ───────────────────────────────────── */
#define MAX_OPEN_DIRS 2
static FsFile g_dirs[MAX_OPEN_DIRS];
static bool   g_dir_used[MAX_OPEN_DIRS];

/* ── SD initialisation (call from setup() before rompler_init) ─ */
bool sd_wrapper_init(void)
{
    if (g_sd_ok) return true;

    /* FIFO_SDIO = 4-bit SDIO via Teensy 4.1's built-in slot */
    if (!g_sd.begin(SdioConfig(FIFO_SDIO))) {
        Serial.println("SD: init FAILED");
        return false;
    }

    g_sd_ok = true;
    memset(g_file_used, 0, sizeof(g_file_used));
    memset(g_dir_used,  0, sizeof(g_dir_used));

    Serial.println("SD: init OK");
    return true;
}

/* ── Helpers ─────────────────────────────────────────────────── */

/* Strip "SD:" prefix that the bare-metal code prepends to all paths */
static const char *strip_drive(const char *path)
{
    if (path[0] == 'S' && path[1] == 'D' && path[2] == ':')
        return path + 3;
    return path;
}

static int alloc_file(void)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!g_file_used[i]) { g_file_used[i] = true; return i; }
    }
    return -1;
}

static int alloc_dir(void)
{
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (!g_dir_used[i]) { g_dir_used[i] = true; return i; }
    }
    return -1;
}

/* ── FatFs file operations ───────────────────────────────────── */

extern "C" {

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode)
{
    (void)mode; /* only FA_READ used */

    if (!g_sd_ok) return FR_NOT_READY;

    int idx = alloc_file();
    if (idx < 0) return FR_TOO_MANY_OPEN_FILES;

    const char *real_path = strip_drive(path);
    if (!g_files[idx].open(real_path, O_RDONLY)) {
        g_file_used[idx] = false;
        return FR_NO_FILE;
    }

    fp->_handle = idx;
    fp->fptr    = 0;
    return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
    int idx = fp->_handle;
    if (idx < 0 || idx >= MAX_OPEN_FILES || !g_file_used[idx])
        return FR_INVALID_OBJECT;

    int n = g_files[idx].read(buff, btr);
    if (n < 0) { *br = 0; return FR_DISK_ERR; }

    *br = (UINT)n;
    fp->fptr += (uint32_t)n;
    return FR_OK;
}

FRESULT f_close(FIL *fp)
{
    int idx = fp->_handle;
    if (idx < 0 || idx >= MAX_OPEN_FILES || !g_file_used[idx])
        return FR_INVALID_OBJECT;

    g_files[idx].close();
    g_file_used[idx] = false;
    fp->_handle = -1;
    return FR_OK;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs)
{
    int idx = fp->_handle;
    if (idx < 0 || idx >= MAX_OPEN_FILES || !g_file_used[idx])
        return FR_INVALID_OBJECT;

    if (!g_files[idx].seek(ofs))
        return FR_DISK_ERR;

    fp->fptr = ofs;
    return FR_OK;
}

/* ── FatFs directory operations ──────────────────────────────── */

FRESULT f_opendir(DIR *dp, const TCHAR *path)
{
    if (!g_sd_ok) return FR_NOT_READY;

    int idx = alloc_dir();
    if (idx < 0) return FR_TOO_MANY_OPEN_FILES;

    const char *real_path = strip_drive(path);
    if (!g_dirs[idx].open(real_path)) {
        g_dir_used[idx] = false;
        return FR_NO_PATH;
    }
    if (!g_dirs[idx].isDir()) {
        g_dirs[idx].close();
        g_dir_used[idx] = false;
        return FR_NO_PATH;
    }

    dp->_handle = idx;
    return FR_OK;
}

FRESULT f_readdir(DIR *dp, FILINFO *fno)
{
    int idx = dp->_handle;
    if (idx < 0 || idx >= MAX_OPEN_DIRS || !g_dir_used[idx])
        return FR_INVALID_OBJECT;

    FsFile entry;
    if (!entry.openNext(&g_dirs[idx], O_RDONLY)) {
        /* End of directory */
        fno->fname[0] = '\0';
        return FR_OK;
    }

    entry.getName(fno->fname, sizeof(fno->fname));
    fno->fsize   = (DWORD)entry.fileSize();
    fno->fattrib = entry.isDir() ? AM_DIR : 0;
    entry.close();

    return FR_OK;
}

FRESULT f_closedir(DIR *dp)
{
    int idx = dp->_handle;
    if (idx < 0 || idx >= MAX_OPEN_DIRS || !g_dir_used[idx])
        return FR_INVALID_OBJECT;

    g_dirs[idx].close();
    g_dir_used[idx] = false;
    dp->_handle = -1;
    return FR_OK;
}

} /* extern "C" */
