#ifndef QISC_SSA_H
#define QISC_SSA_H

#include "qisc_ir.h"
#include "qisc_cfg.h"

typedef struct {
    qisc_cfg* cfg;
    qisc_ir_inst** current_def;
    size_t max_vars;
    size_t max_blocks;
} qisc_ssa_builder;

qisc_ssa_builder* qisc_ssa_create(qisc_cfg* cfg);
void qisc_ssa_destroy(qisc_ssa_builder* b);

void qisc_ssa_write_variable(qisc_ssa_builder* b, uint32_t var_id, qisc_ir_block* block, qisc_ir_inst* value);
qisc_ir_inst* qisc_ssa_read_variable(qisc_ssa_builder* b, uint32_t var_id, qisc_ir_block* block);
qisc_ir_inst* qisc_ssa_read_variable_recursive(qisc_ssa_builder* b, uint32_t var_id, qisc_ir_block* block);

bool qisc_ssa_construct(qisc_ir_module* mod);
bool qisc_ssa_destruct(qisc_ir_module* mod);

#endif // QISC_SSA_H
