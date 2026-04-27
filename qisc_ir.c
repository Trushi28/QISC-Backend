#define _POSIX_C_SOURCE 200809L
#include "qisc_ir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t next_func_id = 1;
static uint32_t next_block_id = 1;
static uint32_t next_inst_id = 1;

qisc_ir_module* qisc_ir_create_module(void) {
    qisc_ir_module* mod = (qisc_ir_module*)calloc(1, sizeof(qisc_ir_module));
    mod->mutation_log = (qisc_mutation_log*)calloc(1, sizeof(qisc_mutation_log));
    return mod;
}

void qisc_ir_set_observer(qisc_ir_module* mod, qisc_ir_observe_fn observer) {
    if (mod) mod->observer = observer;
}

void qisc_ir_destroy_module(qisc_ir_module* mod) {
    if (!mod) return;
    // For a production compiler, we'd recursively free functions, blocks, instructions, values, types.
    // Simplifying the teardown for this sample implementation.
    qisc_mutation_entry* curr = mod->mutation_log->head;
    while (curr) {
        qisc_mutation_entry* next = curr->next;
        free((void*)curr->reason);
        free((void*)curr->entity_name);
        free(curr);
        curr = next;
    }
    free(mod->mutation_log);
    
    qisc_ir_function* f = mod->first_func;
    while(f) {
        qisc_ir_function* nf = f->next;
        free((void*)f->name);
        // blocks
        qisc_ir_block* b = f->first_block;
        while(b) {
            qisc_ir_block* nb = b->next;
            free((void*)b->name);
            // instructions
            qisc_ir_inst* i = b->first_inst;
            while(i) {
                qisc_ir_inst* ni = i->next;
                free(i->operands);
                free(i);
                i = ni;
            }
            free(b->successors);
            free(b->predecessors);
            free(b);
            b = nb;
        }
        free(f);
        f = nf;
    }
    free(mod);
}

qisc_ir_function* qisc_ir_create_function(qisc_ir_module* mod, const char* name, qisc_type* type, bool is_hot) {
    qisc_ir_function* func = (qisc_ir_function*)calloc(1, sizeof(qisc_ir_function));
    func->id = next_func_id++;
    func->name = strdup(name);
    func->type = type;
    func->profile.is_hot = is_hot;
    func->profile.execution_count = 0;
    func->parent_module = mod;
    
    if (mod->last_func) {
        mod->last_func->next = func;
        func->prev = mod->last_func;
        mod->last_func = func;
    } else {
        mod->first_func = mod->last_func = func;
    }
    return func;
}

qisc_ir_block* qisc_ir_create_block(qisc_ir_function* func, const char* name, double branch_probability) {
    qisc_ir_block* block = (qisc_ir_block*)calloc(1, sizeof(qisc_ir_block));
    block->id = next_block_id++;
    block->name = strdup(name);
    block->profile.branch_probability = branch_probability;
    block->parent_func = func;
    
    if (func->last_block) {
        func->last_block->next = block;
        block->prev = func->last_block;
        func->last_block = block;
    } else {
        func->first_block = func->last_block = block;
    }
    return block;
}

qisc_ir_inst* qisc_ir_emit_inst(qisc_ir_block* block, qisc_opcode op, qisc_type* type, qisc_value** operands, size_t num_operands) {
    qisc_ir_inst* inst = (qisc_ir_inst*)calloc(1, sizeof(qisc_ir_inst));
    inst->id = next_inst_id++;
    inst->opcode = op;
    inst->type = type;
    inst->comp_state = QISC_STATE_DORMANT; // Default
    
    if (num_operands > 0) {
        inst->operands = (qisc_value**)malloc(sizeof(qisc_value*) * num_operands);
        memcpy(inst->operands, operands, sizeof(qisc_value*) * num_operands);
    }
    inst->num_operands = num_operands;
    inst->parent_block = block;
    
    if (block->last_inst) {
        block->last_inst->next = inst;
        inst->prev = block->last_inst;
        block->last_inst = inst;
    } else {
        block->first_inst = block->last_inst = inst;
    }
    
    if (block->parent_func && block->parent_func->parent_module && block->parent_func->parent_module->observer) {
        block->parent_func->parent_module->observer(inst);
    }
    return inst;
}

qisc_ir_inst* qisc_ir_emit_await_data(qisc_ir_block* block, qisc_type* data_type) {
    qisc_ir_inst* inst = qisc_ir_emit_inst(block, QISC_OP_AWAIT_DATA, data_type, NULL, 0);
    inst->comp_state = QISC_STATE_TRIGGERED;
    return inst;
}

qisc_ir_inst* qisc_ir_emit_emit(qisc_ir_block* block, qisc_value* val) {
    qisc_value* ops[] = { val };
    qisc_ir_inst* inst = qisc_ir_emit_inst(block, QISC_OP_EMIT, qisc_type_int(), ops, 1); // Emit is void/int type
    inst->comp_state = QISC_STATE_RETURNING;
    return inst;
}

qisc_value* qisc_value_int(int64_t val) {
    qisc_value* v = (qisc_value*)calloc(1, sizeof(qisc_value));
    v->kind = QISC_VAL_CONST_INT;
    v->type = qisc_type_int();
    v->as.i_val = val;
    return v;
}

qisc_value* qisc_value_inst(qisc_ir_inst* inst) {
    qisc_value* v = (qisc_value*)calloc(1, sizeof(qisc_value));
    v->kind = QISC_VAL_INST;
    v->type = inst->type;
    v->as.inst = inst;
    return v;
}

qisc_value* qisc_value_param(qisc_type* type, uint32_t idx) {
    qisc_value* v = (qisc_value*)calloc(1, sizeof(qisc_value));
    v->kind = QISC_VAL_PARAM;
    v->type = type;
    v->as.param_idx = idx;
    return v;
}

qisc_type* qisc_type_int(void) {
    qisc_type* t = (qisc_type*)calloc(1, sizeof(qisc_type));
    t->kind = QISC_TYPE_INT;
    return t;
}

qisc_type* qisc_type_proc(qisc_type* ret_type, qisc_type** params, size_t num_params) {
    qisc_type* t = (qisc_type*)calloc(1, sizeof(qisc_type));
    t->kind = QISC_TYPE_PROC;
    t->ret_type = ret_type;
    t->num_params = num_params;
    if (num_params > 0) {
        t->params = (qisc_type**)malloc(sizeof(qisc_type*) * num_params);
        memcpy(t->params, params, sizeof(qisc_type*) * num_params);
    }
    return t;
}

void qisc_ir_record_mutation(qisc_ir_module* mod, const char* reason, uint64_t cycle, const char* entity_name, uint32_t entity_id) {
    if (!mod || !mod->mutation_log) return;
    qisc_mutation_entry* entry = (qisc_mutation_entry*)calloc(1, sizeof(qisc_mutation_entry));
    entry->cycle = cycle;
    entry->reason = strdup(reason);
    entry->entity_name = strdup(entity_name);
    entry->entity_id = entity_id;
    
    if (mod->mutation_log->tail) {
        mod->mutation_log->tail->next = entry;
        mod->mutation_log->tail = entry;
    } else {
        mod->mutation_log->head = mod->mutation_log->tail = entry;
    }
}

// Simple FNV-1a hash
static uint64_t hash_uint64(uint64_t hash, uint64_t val) {
    const uint8_t* bytes = (const uint8_t*)&val;
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t hash_double(uint64_t hash, double val) {
    uint64_t ival;
    memcpy(&ival, &val, sizeof(double));
    return hash_uint64(hash, ival);
}

uint64_t qisc_ir_compute_hash(qisc_ir_module* mod) {
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        hash = hash_uint64(hash, f->id);
        hash = hash_uint64(hash, f->profile.execution_count);
        hash = hash_uint64(hash, f->profile.is_hot);
        
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            hash = hash_uint64(hash, b->id);
            hash = hash_double(hash, b->profile.branch_probability);
            
            for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                hash = hash_uint64(hash, i->id);
                hash = hash_uint64(hash, i->opcode);
                hash = hash_uint64(hash, i->comp_state);
            }
        }
    }
    return hash;
}

// Simple naive serialization for the plan
bool qisc_ir_serialize(qisc_ir_module* mod, const char* filepath) {
    FILE* f = fopen(filepath, "wb");
    if (!f) return false;
    
    uint32_t magic = QISC_MAGIC_HEADER;
    uint32_t version = QISC_IR_VERSION;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    
    // In a full implementation, we would recursively write modules, functions, blocks, instructions, values, types.
    // For now we just write a dummy count to illustrate the header
    uint32_t func_count = 0;
    for (qisc_ir_function* func = mod->first_func; func; func = func->next) func_count++;
    fwrite(&func_count, sizeof(func_count), 1, f);
    
    fclose(f);
    return true;
}

qisc_ir_module* qisc_ir_deserialize(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    
    uint32_t magic = 0;
    uint32_t version = 0;
    fread(&magic, sizeof(magic), 1, f);
    fread(&version, sizeof(version), 1, f);
    
    if (magic != QISC_MAGIC_HEADER || version != QISC_IR_VERSION) {
        fclose(f);
        return NULL;
    }
    
    qisc_ir_module* mod = qisc_ir_create_module();
    // In a full implementation, read back everything here.
    
    fclose(f);
    return mod;
}
