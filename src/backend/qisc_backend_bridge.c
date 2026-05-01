#define _POSIX_C_SOURCE 200809L
#include "qisc_backend_bridge.h"
#include <stdlib.h>
#include <string.h>

struct qisc_bridge {
    char* module_name;
    qisc_ir_module* module;
    qisc_backend_options options;
};

static qisc_type* bridge_type_from_kind(int kind) {
    qisc_type* type = (qisc_type*)calloc(1, sizeof(qisc_type));
    if (!type) return NULL;
    type->kind = (qisc_type_kind)kind;
    return type;
}

qisc_bridge* qisc_bridge_create(const char* module_name, const qisc_backend_options* options) {
    qisc_bridge* b = (qisc_bridge*)calloc(1, sizeof(qisc_bridge));
    if (!b) return NULL;
    b->module_name = strdup(module_name ? module_name : "qisc_module");
    b->module = qisc_ir_create_module();
    b->options = options ? *options : qisc_backend_default_options();
    if (!b->module || !b->module_name) {
        qisc_bridge_destroy(b);
        return NULL;
    }
    return b;
}

void qisc_bridge_destroy(qisc_bridge* b) {
    if (!b) return;
    qisc_ir_destroy_module(b->module);
    free(b->module_name);
    free(b);
}

qisc_ir_function* qisc_bridge_begin_function(qisc_bridge* b, const char* name, bool is_hot, size_t num_params, int* param_types) {
    if (!b || !name) return NULL;
    qisc_type** params = NULL;
    if (num_params > 0) {
        params = (qisc_type**)calloc(num_params, sizeof(qisc_type*));
        if (!params) return NULL;
        for (size_t i = 0; i < num_params; i++) {
            params[i] = bridge_type_from_kind(param_types ? param_types[i] : QISC_TYPE_INT);
        }
    }
    qisc_ir_function* func = qisc_ir_create_function(b->module, name, qisc_type_proc(qisc_type_int(), params, num_params), is_hot);
    free(params);
    return func;
}

qisc_ir_block* qisc_bridge_create_block(qisc_bridge* b, qisc_ir_function* func, const char* name, double branch_probability) {
    (void)b;
    if (!func || !name) return NULL;
    return qisc_ir_create_block(func, name, branch_probability);
}

qisc_ir_inst* qisc_bridge_emit_int_const(qisc_ir_block* block, int64_t value) {
    qisc_value* ops[] = { qisc_value_int(value) };
    return qisc_ir_emit_inst(block, QISC_OP_NOP, qisc_type_int(), ops, 1);
}

qisc_ir_inst* qisc_bridge_emit_float_const(qisc_ir_block* block, double value) {
    qisc_value* ops[] = { qisc_value_float(value) };
    return qisc_ir_emit_inst(block, QISC_OP_NOP, qisc_type_float(), ops, 1);
}

qisc_ir_inst* qisc_bridge_emit_string_const(qisc_ir_block* block, const char* str) {
    qisc_value* ops[] = { qisc_value_string(str ? str : "") };
    qisc_type* type = (qisc_type*)calloc(1, sizeof(qisc_type));
    if (type) type->kind = QISC_TYPE_STRING;
    return qisc_ir_emit_inst(block, QISC_OP_NOP, type, ops, 1);
}

qisc_ir_inst* qisc_bridge_emit_param(qisc_ir_block* block, uint32_t index) {
    qisc_value* ops[] = { qisc_value_param(qisc_type_int(), index) };
    return qisc_ir_emit_inst(block, QISC_OP_NOP, qisc_type_int(), ops, 1);
}

qisc_ir_inst* qisc_bridge_emit_binary(qisc_ir_block* block, qisc_opcode op, qisc_ir_inst* lhs, qisc_ir_inst* rhs) {
    qisc_value* ops[] = { qisc_value_inst(lhs), qisc_value_inst(rhs) };
    qisc_type* type = lhs && lhs->type ? lhs->type : qisc_type_int();
    return qisc_ir_emit_inst(block, op, type, ops, 2);
}

qisc_ir_inst* qisc_bridge_emit_compare(qisc_ir_block* block, qisc_opcode op, qisc_ir_inst* lhs, qisc_ir_inst* rhs) {
    qisc_value* ops[] = { qisc_value_inst(lhs), qisc_value_inst(rhs) };
    return qisc_ir_emit_inst(block, op, qisc_type_int(), ops, 2);
}

qisc_ir_inst* qisc_bridge_emit_call(qisc_ir_block* block, const char* callee, qisc_ir_inst** args, size_t num_args) {
    qisc_value** ops = (qisc_value**)calloc(num_args + 1, sizeof(qisc_value*));
    if (!ops) return NULL;
    ops[0] = qisc_value_string(callee ? callee : "");
    for (size_t i = 0; i < num_args; i++) ops[i + 1] = qisc_value_inst(args[i]);
    qisc_ir_inst* inst = qisc_ir_emit_inst(block, QISC_OP_CALL, qisc_type_int(), ops, num_args + 1);
    free(ops);
    return inst;
}

qisc_ir_inst* qisc_bridge_emit_ret(qisc_ir_block* block, qisc_ir_inst* value) {
    if (!value) return qisc_ir_emit_inst(block, QISC_OP_RET, qisc_type_int(), NULL, 0);
    qisc_value* ops[] = { qisc_value_inst(value) };
    return qisc_ir_emit_inst(block, QISC_OP_RET, value->type ? value->type : qisc_type_int(), ops, 1);
}

qisc_ir_inst* qisc_bridge_emit_br(qisc_ir_block* block, qisc_ir_block* target) {
    if (!block || !target) return NULL;
    free(block->successors);
    block->successors = (qisc_ir_block**)malloc(sizeof(qisc_ir_block*));
    if (!block->successors) return NULL;
    block->successors[0] = target;
    block->num_successors = 1;
    return qisc_ir_emit_inst(block, QISC_OP_BR, qisc_type_int(), NULL, 0);
}

qisc_ir_inst* qisc_bridge_emit_br_cond(qisc_ir_block* block, qisc_ir_inst* cond, qisc_ir_block* true_block, qisc_ir_block* false_block) {
    if (!block || !cond || !true_block || !false_block) return NULL;
    free(block->successors);
    block->successors = (qisc_ir_block**)malloc(2 * sizeof(qisc_ir_block*));
    if (!block->successors) return NULL;
    block->successors[0] = true_block;
    block->successors[1] = false_block;
    block->num_successors = 2;
    qisc_value* ops[] = { qisc_value_inst(cond) };
    return qisc_ir_emit_inst(block, QISC_OP_BR_COND, qisc_type_int(), ops, 1);
}

qisc_ir_inst* qisc_bridge_emit_pipeline(qisc_ir_block* block, qisc_ir_inst* stream, qisc_ir_inst* fn) {
    qisc_value* ops[] = { qisc_value_inst(stream), qisc_value_inst(fn) };
    return qisc_ir_emit_inst(block, QISC_OP_PIPELINE, qisc_type_int(), ops, 2);
}

qisc_ir_inst* qisc_bridge_emit_await_data(qisc_ir_block* block) {
    return qisc_ir_emit_await_data(block, qisc_type_int());
}

qisc_ir_inst* qisc_bridge_emit_emit_value(qisc_ir_block* block, qisc_ir_inst* value) {
    return qisc_ir_emit_emit(block, qisc_value_inst(value));
}

void qisc_bridge_set_profile(qisc_bridge* b, qisc_ir_function* func, uint64_t execution_count, bool is_hot) {
    (void)b;
    if (!func) return;
    func->profile.execution_count = execution_count;
    func->profile.is_hot = is_hot;
}

bool qisc_bridge_compile(qisc_bridge* b, const char* output_path) {
    if (!b || !b->module) return false;
    qisc_backend_options options = b->options;
    if (output_path) options.object_path = output_path;
    return qisc_backend_compile_module(b->module, &options);
}
