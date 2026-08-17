/**
 * fs.c — Flash Filesystem (LittleFS on RP2040 internal flash)
 *
 * Akhlut CFW
 *
 * Flash layout (16 MB total):
 *   0x000000 - 0x0FFFFF : Firmware (1 MB reserved)
 *   0x100000 - 0xFFEFFF : LittleFS partition (~15 MB)
 *   0xFFF000 - 0xFFFFFF : Settings (last sector, untouched)
 *
 * All erase/program operations go through flash_safe_execute()
 * which coordinates both cores and disables interrupts so XIP
 * doesn't stall during flash writes.
 */

#include "fs.h"
#include "lfs.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <string.h>

static lfs_t lfs;
static bool  lfs_mounted = false;

/* ──────────────────────────────────────────────────────────
 * Flash HAL — read/prog/erase via flash_safe_execute
 * ────────────────────────────────────────────────────────── */

static int lfs_flash_read(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, void *buffer, lfs_size_t size) {
    (void)c;
    uint32_t addr = FS_FLASH_OFFSET + block * FLASH_SECTOR_SIZE + off;
    memcpy(buffer, (const void *)(XIP_BASE + addr), size);
    return LFS_ERR_OK;
}

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    uint32_t size;
} prog_ctx_t;

static void flash_prog_cb(void *param) {
    prog_ctx_t *ctx = (prog_ctx_t *)param;
    flash_range_program(ctx->offset, ctx->data, ctx->size);
}

static int lfs_flash_prog(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, const void *buffer, lfs_size_t size) {
    (void)c;
    prog_ctx_t ctx = {
        .offset = FS_FLASH_OFFSET + block * FLASH_SECTOR_SIZE + off,
        .data = (const uint8_t *)buffer,
        .size = size,
    };
    int rc = flash_safe_execute(flash_prog_cb, &ctx, 500);
    return (rc == PICO_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

typedef struct {
    uint32_t offset;
    uint32_t size;
} erase_ctx_t;

static void flash_erase_cb(void *param) {
    erase_ctx_t *ctx = (erase_ctx_t *)param;
    flash_range_erase(ctx->offset, ctx->size);
}

static int lfs_flash_erase(const struct lfs_config *c, lfs_block_t block) {
    (void)c;
    erase_ctx_t ctx = {
        .offset = FS_FLASH_OFFSET + block * FLASH_SECTOR_SIZE,
        .size = FLASH_SECTOR_SIZE,
    };
    int rc = flash_safe_execute(flash_erase_cb, &ctx, 500);
    return (rc == PICO_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int lfs_flash_sync(const struct lfs_config *c) {
    (void)c;
    return LFS_ERR_OK;
}

/* ──────────────────────────────────────────────────────────
 * LittleFS Configuration
 * ────────────────────────────────────────────────────────── */

static uint8_t lfs_read_buf[FLASH_PAGE_SIZE];
static uint8_t lfs_prog_buf[FLASH_PAGE_SIZE];
static uint8_t lfs_lookahead_buf[32];

static const struct lfs_config lfs_cfg = {
    .read  = lfs_flash_read,
    .prog  = lfs_flash_prog,
    .erase = lfs_flash_erase,
    .sync  = lfs_flash_sync,

    .read_size      = 1,
    .prog_size      = FLASH_PAGE_SIZE,
    .block_size     = FLASH_SECTOR_SIZE,
    .block_count    = FS_FLASH_SIZE / FLASH_SECTOR_SIZE,
    .cache_size     = FLASH_PAGE_SIZE,
    .lookahead_size = sizeof(lfs_lookahead_buf),
    .block_cycles   = 500,

    .read_buffer      = lfs_read_buf,
    .prog_buffer      = lfs_prog_buf,
    .lookahead_buffer  = lfs_lookahead_buf,
};

/* ──────────────────────────────────────────────────────────
 * Public API
 * ────────────────────────────────────────────────────────── */

int fs_init(void) {
    int err = lfs_mount(&lfs, &lfs_cfg);
    if (err) {
        printf("[FS] Mount failed (%d), formatting...\n", err);
        err = lfs_format(&lfs, &lfs_cfg);
        if (err) {
            printf("[FS] Format failed: %d\n", err);
            return err;
        }
        err = lfs_mount(&lfs, &lfs_cfg);
        if (err) {
            printf("[FS] Mount after format failed: %d\n", err);
            return err;
        }

        lfs_mkdir(&lfs, "/captures");
        lfs_mkdir(&lfs, "/captures/subghz");
        lfs_mkdir(&lfs, "/captures/ir");
        lfs_mkdir(&lfs, "/apps");
        printf("[FS] Formatted and created directories\n");
    }

    lfs_mounted = true;
    struct lfs_fsinfo info;
    if (lfs_fs_stat(&lfs, &info) == LFS_ERR_OK) {
        printf("[FS] Mounted: %u blocks, %u bytes/block\n",
               (unsigned)info.block_count, (unsigned)info.block_size);
    } else {
        printf("[FS] Mounted: %u blocks\n",
               (unsigned)lfs_cfg.block_count);
    }
    return 0;
}

int fs_write(const char *path, const void *data, uint32_t len) {
    if (!lfs_mounted) return -1;

    lfs_file_t f;
    int err = lfs_file_open(&lfs, &f, path,
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err) return err;

    lfs_ssize_t written = lfs_file_write(&lfs, &f, data, len);
    lfs_file_close(&lfs, &f);

    return (written == (lfs_ssize_t)len) ? 0 : -1;
}

int fs_read(const char *path, void *buf, uint32_t max_len) {
    if (!lfs_mounted) return -1;

    lfs_file_t f;
    int err = lfs_file_open(&lfs, &f, path, LFS_O_RDONLY);
    if (err) return err;

    lfs_ssize_t rd = lfs_file_read(&lfs, &f, buf, max_len);
    lfs_file_close(&lfs, &f);

    return (int)rd;
}

int fs_list(const char *dir, fs_list_cb_t cb, void *user) {
    if (!lfs_mounted) return -1;

    lfs_dir_t d;
    int err = lfs_dir_open(&lfs, &d, dir);
    if (err) return err;

    struct lfs_info info;
    int count = 0;
    while (lfs_dir_read(&lfs, &d, &info) > 0) {
        if (info.name[0] == '.') continue;
        if (info.type == LFS_TYPE_REG) {
            cb(info.name, info.size, user);
            count++;
        }
    }
    lfs_dir_close(&lfs, &d);
    return count;
}

int fs_delete(const char *path) {
    if (!lfs_mounted) return -1;
    return lfs_remove(&lfs, path);
}

int fs_free_space(void) {
    if (!lfs_mounted) return -1;
    lfs_ssize_t used = lfs_fs_size(&lfs);
    if (used < 0) return -1;
    int free_blocks = (int)lfs_cfg.block_count - (int)used;
    return free_blocks * (int)FLASH_SECTOR_SIZE;
}

int fs_mkdir(const char *path) {
    if (!lfs_mounted) return -1;
    int err = lfs_mkdir(&lfs, path);
    if (err == LFS_ERR_EXIST) return 0;
    return err;
}

bool fs_exists(const char *path) {
    if (!lfs_mounted) return false;
    struct lfs_info info;
    return lfs_stat(&lfs, path, &info) == LFS_ERR_OK;
}

int fs_write2(const char *path,
              const void *a, uint32_t a_len,
              const void *b, uint32_t b_len) {
    if (!lfs_mounted) return -1;

    lfs_file_t f;
    int err = lfs_file_open(&lfs, &f, path,
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err) return err;

    lfs_ssize_t w1 = lfs_file_write(&lfs, &f, a, a_len);
    if (w1 != (lfs_ssize_t)a_len) { lfs_file_close(&lfs, &f); return -1; }

    lfs_ssize_t w2 = lfs_file_write(&lfs, &f, b, b_len);
    lfs_file_close(&lfs, &f);

    return (w2 == (lfs_ssize_t)b_len) ? 0 : -1;
}
