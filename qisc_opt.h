#ifndef QISC_OPT_H
#define QISC_OPT_H

#include "qisc_ir.h"

// Optimization Passes
// Returns true if the module was modified (to support fixed-point iteration)

bool qisc_opt_inline(qisc_ir_module* mod, uint64_t cycle);
bool qisc_opt_outline_cold_blocks(qisc_ir_module* mod, uint64_t cycle);
bool qisc_opt_specialize_constants(qisc_ir_module* mod, uint64_t cycle);
bool qisc_opt_dead_code_elimination(qisc_ir_module* mod, uint64_t cycle);

// Run all optimizations until convergence (IR hash stabilizes)
void qisc_opt_run_pipeline(qisc_ir_module* mod);

#endif // QISC_OPT_H
