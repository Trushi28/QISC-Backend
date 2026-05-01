#ifndef QISC_CONVERGENCE_H
#define QISC_CONVERGENCE_H

#include "qisc_ir.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint64_t  cycle;
    uint64_t  hash_before;
    uint64_t  hash_after;
    uint32_t  mutations_applied;
    double    elapsed_seconds;
    bool      converged;
} qisc_convergence_cycle_result;

typedef struct {
    qisc_convergence_cycle_result* cycles;
    size_t                         num_cycles;
    uint64_t                       final_hash;
    bool                           has_converged;
    uint32_t                       max_cycles;    
    const char*                    profile_path;
    const char*                    ir_cache_path;
} qisc_convergence_state;

qisc_convergence_state* qisc_convergence_create(
    uint32_t max_cycles,
    const char* profile_path,
    const char* ir_cache_path
);

void qisc_convergence_destroy(qisc_convergence_state* s);

qisc_convergence_cycle_result qisc_convergence_run_cycle(
    qisc_convergence_state* state,
    qisc_ir_module* mod
);

bool qisc_convergence_run_to_completion(
    qisc_convergence_state* state,
    qisc_ir_module* mod
);

qisc_ir_module* qisc_convergence_load_cached(
    qisc_convergence_state* state
);

void qisc_convergence_print_report(
    qisc_convergence_state* state
);

#endif
