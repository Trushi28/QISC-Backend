#ifndef QISC_BACKEND_BRIDGE_H
#define QISC_BACKEND_BRIDGE_H

#include "qisc_ir.h"
#include "qisc_backend.h"

typedef struct qisc_bridge qisc_bridge;

qisc_bridge* qisc_bridge_create(
    const char* module_name,
    const qisc_backend_options* options);

void qisc_bridge_destroy(qisc_bridge* b);

qisc_ir_function* qisc_bridge_begin_function(
    qisc_bridge* b, const char* name,
    bool is_hot, size_t num_params,
    int* param_types);

qisc_ir_block* qisc_bridge_create_block(
    qisc_bridge* b, qisc_ir_function* func,
    const char* name, double branch_probability);

qisc_ir_inst* qisc_bridge_emit_int_const(
    qisc_ir_block* block, int64_t value);

qisc_ir_inst* qisc_bridge_emit_float_const(
    qisc_ir_block* block, double value);

qisc_ir_inst* qisc_bridge_emit_string_const(
    qisc_ir_block* block, const char* str);

qisc_ir_inst* qisc_bridge_emit_param(
    qisc_ir_block* block, uint32_t index);

qisc_ir_inst* qisc_bridge_emit_binary(
    qisc_ir_block* block, qisc_opcode op,
    qisc_ir_inst* lhs, qisc_ir_inst* rhs);

qisc_ir_inst* qisc_bridge_emit_compare(
    qisc_ir_block* block, qisc_opcode op,
    qisc_ir_inst* lhs, qisc_ir_inst* rhs);

qisc_ir_inst* qisc_bridge_emit_call(
    qisc_ir_block* block, const char* callee,
    qisc_ir_inst** args, size_t num_args);

qisc_ir_inst* qisc_bridge_emit_ret(
    qisc_ir_block* block, qisc_ir_inst* value);

qisc_ir_inst* qisc_bridge_emit_br(
    qisc_ir_block* block, qisc_ir_block* target);

qisc_ir_inst* qisc_bridge_emit_br_cond(
    qisc_ir_block* block, qisc_ir_inst* cond,
    qisc_ir_block* true_block,
    qisc_ir_block* false_block);

qisc_ir_inst* qisc_bridge_emit_pipeline(
    qisc_ir_block* block,
    qisc_ir_inst* stream, qisc_ir_inst* fn);

qisc_ir_inst* qisc_bridge_emit_await_data(
    qisc_ir_block* block);

qisc_ir_inst* qisc_bridge_emit_emit_value(
    qisc_ir_block* block, qisc_ir_inst* value);

void qisc_bridge_set_profile(
    qisc_bridge* b, qisc_ir_function* func,
    uint64_t execution_count, bool is_hot);

bool qisc_bridge_compile(
    qisc_bridge* b, const char* output_path);

#endif
