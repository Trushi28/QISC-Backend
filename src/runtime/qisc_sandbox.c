#define _POSIX_C_SOURCE 200809L
#include "qisc_sandbox.h"
#include <stdlib.h>

qisc_sandbox* qisc_sandbox_create(qisc_ir_module* observed_module, qisc_sandbox_observe_fn observer) {
    qisc_sandbox* sandbox = (qisc_sandbox*)calloc(1, sizeof(qisc_sandbox));
    if (!sandbox) return NULL;
    sandbox->observer = observer;
    sandbox->observed_module = observed_module;
    sandbox->readiness_threshold = 1024;
    return sandbox;
}

void qisc_sandbox_destroy(qisc_sandbox* sandbox) {
    free(sandbox);
}

void qisc_sandbox_set_readiness_threshold(qisc_sandbox* sandbox, uint64_t threshold) {
    if (!sandbox) return;
    sandbox->readiness_threshold = threshold ? threshold : 1;
    sandbox->replication_ready = sandbox->observation_count >= sandbox->readiness_threshold;
}

void qisc_sandbox_observe(qisc_sandbox* sandbox, qisc_ir_inst* inst, qisc_value* result, uint64_t cycle) {
    if (!sandbox || !inst) return;
    sandbox->observation_count++;
    sandbox->replication_ready = sandbox->observation_count >= sandbox->readiness_threshold;
    if (sandbox->observer) sandbox->observer(inst, result, cycle);
    if (sandbox->observed_module && inst->parent_block && inst->parent_block->parent_func) {
        qisc_ir_record_mutation(
            sandbox->observed_module,
            "sandbox observation",
            cycle,
            inst->parent_block->parent_func->name,
            inst->id
        );
    }
}
