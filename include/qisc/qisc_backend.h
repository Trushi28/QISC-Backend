#ifndef QISC_BACKEND_H
#define QISC_BACKEND_H

#include "qisc_ir.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    QISC_BACKEND_CONTEXT_SERVER,
    QISC_BACKEND_CONTEXT_CLI,
    QISC_BACKEND_CONTEXT_EMBEDDED,
    QISC_BACKEND_CONTEXT_NOTEBOOK,
    QISC_BACKEND_CONTEXT_WEB
} qisc_backend_context;

typedef struct {
    qisc_backend_context context;
    uint32_t max_optimization_cycles;
    bool emit_object;
    const char* object_path;
} qisc_backend_options;

qisc_backend_options qisc_backend_default_options(void);
bool qisc_backend_compile_module(qisc_ir_module* mod, const qisc_backend_options* options);

#endif
