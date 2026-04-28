#ifndef QISC_OPT_H
#define QISC_OPT_H

#include "qisc_ir.h"

typedef enum {
    QISC_PASS_CONSTANT_FOLD,
    QISC_PASS_DEAD_CODE_ELIM,
    QISC_PASS_COPY_PROPAGATION,
    QISC_PASS_INLINE,
    QISC_PASS_COLD_OUTLINE,
    QISC_PASS_CONST_SPECIALIZE,
    QISC_PASS_SSA_CONSTRUCT,
    QISC_PASS_SSA_DESTRUCT,
} qisc_pass_id;

typedef struct {
    qisc_pass_id    id;
    const char*     name;
    bool (*run)(qisc_ir_module* mod, uint64_t cycle);
    uint64_t        times_run;
    uint64_t        total_mutations;
    double          total_time_seconds;
} qisc_pass;

typedef struct {
    qisc_pass*  passes;
    size_t      num_passes;
    size_t      capacity;
} qisc_pass_pipeline;

qisc_pass_pipeline* qisc_pipeline_create(void);
void qisc_pipeline_add_pass(qisc_pass_pipeline* p, qisc_pass_id id);
bool qisc_pipeline_run_once(qisc_pass_pipeline* p, qisc_ir_module* mod, uint64_t cycle);
void qisc_pipeline_print_stats(qisc_pass_pipeline* p);
void qisc_pipeline_destroy(qisc_pass_pipeline* p);

// Individual passes
bool qisc_pass_constant_fold(qisc_ir_module* mod, uint64_t cycle);
bool qisc_pass_copy_propagation(qisc_ir_module* mod, uint64_t cycle);
bool qisc_pass_inline(qisc_ir_module* mod, uint64_t cycle);
bool qisc_pass_cold_outline(qisc_ir_module* mod, uint64_t cycle);
bool qisc_pass_specialize_constants(qisc_ir_module* mod, uint64_t cycle);
bool qisc_pass_dead_code_elimination(qisc_ir_module* mod, uint64_t cycle);
bool qisc_pass_ssa_construct_wrapper(qisc_ir_module* mod, uint64_t cycle);
bool qisc_pass_ssa_destruct_wrapper(qisc_ir_module* mod, uint64_t cycle);

// Kept for backward compatibility if needed by convergence
void qisc_opt_run_pipeline(qisc_ir_module* mod);

#endif // QISC_OPT_H
