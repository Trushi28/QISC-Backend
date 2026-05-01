#ifndef QISC_SANDBOX_H
#define QISC_SANDBOX_H

#include "qisc_ir.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*qisc_sandbox_observe_fn)(
    qisc_ir_inst* inst,
    qisc_value* result,
    uint64_t cycle
);

typedef struct {
    qisc_sandbox_observe_fn observer;
    qisc_ir_module* observed_module;
    uint64_t observation_count;
    bool replication_ready;
    uint64_t readiness_threshold;
} qisc_sandbox;

qisc_sandbox* qisc_sandbox_create(qisc_ir_module* observed_module, qisc_sandbox_observe_fn observer);
void qisc_sandbox_destroy(qisc_sandbox* sandbox);
void qisc_sandbox_observe(qisc_sandbox* sandbox, qisc_ir_inst* inst, qisc_value* result, uint64_t cycle);
void qisc_sandbox_set_readiness_threshold(qisc_sandbox* sandbox, uint64_t threshold);

#endif
