/**
 * fs.h — Flash Filesystem API
 *
 * Akhlut CFW
 *
 * LittleFS on internal flash. Partition starts at FS_FLASH_OFFSET,
 * well past firmware, and ends before the settings sector.
 * All flash writes go through flash_safe_execute().
 */

#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hardware/flash.h"

#define FS_FLASH_OFFSET   0x100000
#define FS_FLASH_SIZE     (PICO_FLASH_SIZE_BYTES - FS_FLASH_OFFSET - FLASH_SECTOR_SIZE)

typedef void (*fs_list_cb_t)(const char *name, uint32_t size, void *user);

int  fs_init(void);
int  fs_write(const char *path, const void *data, uint32_t len);
int  fs_read(const char *path, void *buf, uint32_t max_len);
int  fs_list(const char *dir, fs_list_cb_t cb, void *user);
int  fs_delete(const char *path);
int  fs_free_space(void);
int  fs_mkdir(const char *path);
bool fs_exists(const char *path);
int  fs_write2(const char *path,
               const void *a, uint32_t a_len,
               const void *b, uint32_t b_len);

#endif
