/**
 * wasm_api.h — WASM App Runtime API
 *
 * Akhlut CFW
 *
 * Host functions exposed to WASM apps via wasm3 linking.
 * Apps import these from module "env".
 */

#ifndef WASM_API_H
#define WASM_API_H

#include <stdint.h>
#include <stdbool.h>
#include "wasm3.h"

#define WASM_STACK_SIZE     4096
#define WASM_MAX_FILE_SIZE  (32 * 1024)

typedef struct {
    IM3Environment env;
    IM3Runtime     runtime;
    IM3Module      module;
    uint8_t       *wasm_data;
    uint32_t       wasm_size;
    bool           running;
    bool           exit_requested;
} wasm_ctx_t;

int  wasm_load(wasm_ctx_t *ctx, const char *path);
int  wasm_run(wasm_ctx_t *ctx);
void wasm_stop(wasm_ctx_t *ctx);

M3Result wasm_link_all(IM3Module module);

#endif
