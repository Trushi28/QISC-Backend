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
        if (f->type) {
            free(f->type->params);
            free(f->type);
        }
        qisc_ir_block* b = f->first_block;
        while(b) {
            qisc_ir_block* nb = b->next;
            free((void*)b->name);
            qisc_ir_inst* i = b->first_inst;
            while(i) {
                qisc_ir_inst* ni = i->next;
                for(size_t op=0; op<i->num_operands; op++) {
                    if (i->operands[op]->kind == QISC_VAL_CONST_STRING) {
                        free((void*)i->operands[op]->as.s_val);
                    }
                    free(i->operands[op]);
                }
                free(i->operands);
                free(i->phi_incoming_blocks);
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
    
    if (op == QISC_OP_DIV) {
        inst->requires_rax = true;
        inst->clobbers_rdx = true;
        inst->clobbers_rax = true;
    } else if (op == QISC_OP_RET) {
        inst->requires_rax = true;
    }
    
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

qisc_ir_inst* qisc_ir_emit_phi(qisc_ir_block* block, qisc_type* type, qisc_ir_inst** incoming_values, qisc_ir_block** incoming_blocks, size_t count) {
    qisc_ir_inst* inst = (qisc_ir_inst*)calloc(1, sizeof(qisc_ir_inst));
    inst->id = next_inst_id++;
    inst->opcode = QISC_OP_PHI;
    inst->type = type;
    inst->comp_state = QISC_STATE_DORMANT;
    inst->num_operands = count;
    inst->phi_num_incoming = count;
    
    if (count > 0) {
        inst->operands = (qisc_value**)malloc(sizeof(qisc_value*) * count);
        inst->phi_incoming_blocks = (qisc_ir_block**)malloc(sizeof(qisc_ir_block*) * count);
        for(size_t i = 0; i < count; ++i) {
            inst->operands[i] = qisc_value_inst(incoming_values[i]);
            inst->phi_incoming_blocks[i] = incoming_blocks[i];
        }
    }
    inst->parent_block = block;
    
    // PHI nodes always go at the very top of the block
    if (block->first_inst) {
        inst->next = block->first_inst;
        block->first_inst->prev = inst;
        block->first_inst = inst;
    } else {
        block->first_inst = block->last_inst = inst;
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
    qisc_ir_inst* inst = qisc_ir_emit_inst(block, QISC_OP_EMIT, qisc_type_int(), ops, 1);
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

qisc_value* qisc_value_float(double val) {
    qisc_value* v = (qisc_value*)calloc(1, sizeof(qisc_value));
    v->kind = QISC_VAL_CONST_FLOAT;
    v->type = qisc_type_float();
    v->as.f_val = val;
    return v;
}

qisc_value* qisc_value_bool(bool val) {
    qisc_value* v = (qisc_value*)calloc(1, sizeof(qisc_value));
    v->kind = QISC_VAL_CONST_BOOL;
    v->type = (qisc_type*)calloc(1, sizeof(qisc_type));
    v->type->kind = QISC_TYPE_BOOL;
    v->as.b_val = val;
    return v;
}

qisc_value* qisc_value_string(const char* str) {
    qisc_value* v = (qisc_value*)calloc(1, sizeof(qisc_value));
    v->kind = QISC_VAL_CONST_STRING;
    v->type = (qisc_type*)calloc(1, sizeof(qisc_type));
    v->type->kind = QISC_TYPE_STRING;
    v->as.s_val = strdup(str);
    return v;
}

qisc_value* qisc_value_inst(qisc_ir_inst* inst) {
    qisc_value* v = (qisc_value*)calloc(1, sizeof(qisc_value));
    v->kind = QISC_VAL_INST;
    v->type = inst ? inst->type : NULL;
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

qisc_type* qisc_type_float(void) {
    qisc_type* t = (qisc_type*)calloc(1, sizeof(qisc_type));
    t->kind = QISC_TYPE_FLOAT;
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
    uint64_t hash = 14695981039346656037ULL;
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

bool qisc_ir_validate_module(qisc_ir_module* mod, char* error_buf, size_t error_buf_len) {
    if(error_buf_len > 0) error_buf[0] = '\0';
    bool valid = true;
    char temp[512];
    
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        if (!f->first_block) {
            snprintf(temp, sizeof(temp), "Func %s has no entry block\n", f->name);
            strncat(error_buf, temp, error_buf_len - strlen(error_buf) - 1);
            valid = false;
        }
        
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            if (!b->first_inst) {
                snprintf(temp, sizeof(temp), "Block %s in Func %s has zero instructions\n", b->name, f->name);
                strncat(error_buf, temp, error_buf_len - strlen(error_buf) - 1);
                valid = false;
            }
            
            for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                if (i->type && i->type->kind != QISC_TYPE_VOID && !i->type) {
                    snprintf(temp, sizeof(temp), "Inst %u has invalid return type\n", i->id);
                    strncat(error_buf, temp, error_buf_len - strlen(error_buf) - 1);
                    valid = false;
                }
                
                if (i->opcode == QISC_OP_ADD || i->opcode == QISC_OP_SUB || i->opcode == QISC_OP_MUL || i->opcode == QISC_OP_DIV) {
                    if (i->num_operands != 2) {
                        snprintf(temp, sizeof(temp), "ALU Inst %u requires 2 ops, got %zu\n", i->id, i->num_operands);
                        strncat(error_buf, temp, error_buf_len - strlen(error_buf) - 1);
                        valid = false;
                    }
                }
                
                if (i->opcode == QISC_OP_RET) {
                    if (i->num_operands > 1) {
                        snprintf(temp, sizeof(temp), "RET Inst %u has >1 ops\n", i->id);
                        strncat(error_buf, temp, error_buf_len - strlen(error_buf) - 1);
                        valid = false;
                    }
                }
                
                if (i->opcode == QISC_OP_BR) {
                    if (b->num_successors != 1) {
                        snprintf(temp, sizeof(temp), "BR in block %s requires 1 successor, got %zu\n", b->name, b->num_successors);
                        strncat(error_buf, temp, error_buf_len - strlen(error_buf) - 1);
                        valid = false;
                    }
                }
                
                if (i->opcode == QISC_OP_BR_COND) {
                    if (b->num_successors != 2) {
                        snprintf(temp, sizeof(temp), "BR_COND in block %s requires 2 successors, got %zu\n", b->name, b->num_successors);
                        strncat(error_buf, temp, error_buf_len - strlen(error_buf) - 1);
                        valid = false;
                    }
                }
                
                for(size_t op_idx = 0; op_idx < i->num_operands; op_idx++) {
                    if (i->operands[op_idx]->kind == QISC_VAL_INST) {
                        qisc_ir_inst* ref = i->operands[op_idx]->as.inst;
                        if (ref && ref->parent_block && ref->parent_block->parent_func != f) {
                            snprintf(temp, sizeof(temp), "Inst %u references inst %u from diff func\n", i->id, ref->id);
                            strncat(error_buf, temp, error_buf_len - strlen(error_buf) - 1);
                            valid = false;
                        }
                    }
                }
            }
        }
    }
    return valid;
}

static const char* op_to_str(qisc_opcode op) {
    switch(op) {
        case QISC_OP_NOP: return "NOP";
        case QISC_OP_ADD: return "ADD";
        case QISC_OP_SUB: return "SUB";
        case QISC_OP_MUL: return "MUL";
        case QISC_OP_DIV: return "DIV";
        case QISC_OP_CMP_EQ: return "CMP_EQ";
        case QISC_OP_CMP_LT: return "CMP_LT";
        case QISC_OP_CMP_GT: return "CMP_GT";
        case QISC_OP_BR: return "BR";
        case QISC_OP_BR_COND: return "BR_COND";
        case QISC_OP_CALL: return "CALL";
        case QISC_OP_RET: return "RET";
        case QISC_OP_LOAD: return "LOAD";
        case QISC_OP_STORE: return "STORE";
        case QISC_OP_ALLOCA: return "ALLOCA";
        case QISC_OP_TRY: return "TRY";
        case QISC_OP_CATCH: return "CATCH";
        case QISC_OP_FAIL: return "FAIL";
        case QISC_OP_STREAM_RANGE: return "STREAM_RANGE";
        case QISC_OP_STREAM_MAP: return "STREAM_MAP";
        case QISC_OP_STREAM_REDUCE: return "STREAM_REDUCE";
        case QISC_OP_STREAM_FILTER: return "STREAM_FILTER";
        case QISC_OP_PIPELINE: return "PIPELINE";
        case QISC_OP_AWAIT_DATA: return "AWAIT_DATA";
        case QISC_OP_EMIT: return "EMIT";
        case QISC_OP_PHI: return "PHI";
        default: return "UNKNOWN";
    }
}

void qisc_ir_print_module(qisc_ir_module* mod) {
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        printf("func %s() [%s] {\n", f->name, f->profile.is_hot ? "hot" : "cold");
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            printf("  block %s [prob=%.2f]:\n", b->name, b->profile.branch_probability);
            for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                printf("    ");
                if (i->type && i->type->kind != QISC_TYPE_VOID) {
                    printf("%%%u = ", i->id);
                }
                printf("%s", op_to_str(i->opcode));
                for(size_t op=0; op<i->num_operands; op++) {
                    qisc_value* v = i->operands[op];
                    if (op > 0) printf(",");
                    if (v->kind == QISC_VAL_CONST_INT) printf(" CONST_INT %ld", (long)v->as.i_val);
                    else if (v->kind == QISC_VAL_CONST_FLOAT) printf(" CONST_FLOAT %f", v->as.f_val);
                    else if (v->kind == QISC_VAL_CONST_BOOL) printf(" CONST_BOOL %d", v->as.b_val);
                    else if (v->kind == QISC_VAL_CONST_STRING) printf(" CONST_STRING \"%s\"", v->as.s_val);
                    else if (v->kind == QISC_VAL_INST) printf(" %%%u", v->as.inst ? v->as.inst->id : 0);
                    else if (v->kind == QISC_VAL_PARAM) printf(" PARAM %u", v->as.param_idx);
                }
                
                if (i->opcode == QISC_OP_BR && b->num_successors > 0) {
                    printf(" -> [%s]", b->successors[0]->name);
                } else if (i->opcode == QISC_OP_BR_COND && b->num_successors > 1) {
                    printf(" -> [%s, %s]", b->successors[0]->name, b->successors[1]->name);
                }
                printf("\n");
            }
        }
        printf("}\n");
    }
}

bool qisc_ir_serialize(qisc_ir_module* mod, const char* filepath) {
    FILE* f = fopen(filepath, "wb");
    if (!f) return false;
    
    uint32_t magic = QISC_MAGIC_HEADER;
    uint32_t version = 2;
    
    uint32_t num_funcs = 0;
    for (qisc_ir_function* func = mod->first_func; func; func = func->next) num_funcs++;
    
    uint64_t ir_hash = qisc_ir_compute_hash(mod);
    
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&num_funcs, sizeof(num_funcs), 1, f);
    fwrite(&ir_hash, sizeof(ir_hash), 1, f);
    
    for (qisc_ir_function* func = mod->first_func; func; func = func->next) {
        fwrite(&func->id, sizeof(uint32_t), 1, f);
        uint32_t name_len = strlen(func->name);
        fwrite(&name_len, sizeof(name_len), 1, f);
        fwrite(func->name, 1, name_len, f);
        
        uint8_t is_hot = func->profile.is_hot ? 1 : 0;
        fwrite(&is_hot, sizeof(is_hot), 1, f);
        fwrite(&func->profile.execution_count, sizeof(uint64_t), 1, f);
        
        uint32_t num_params = func->type ? func->type->num_params : 0;
        fwrite(&num_params, sizeof(num_params), 1, f);
        for (uint32_t p = 0; p < num_params; p++) {
            uint32_t type_kind = func->type->params[p]->kind;
            fwrite(&type_kind, sizeof(type_kind), 1, f);
        }
        
        uint32_t num_blocks = 0;
        for (qisc_ir_block* b = func->first_block; b; b = b->next) num_blocks++;
        fwrite(&num_blocks, sizeof(num_blocks), 1, f);
        
        for (qisc_ir_block* b = func->first_block; b; b = b->next) {
            fwrite(&b->id, sizeof(uint32_t), 1, f);
            uint32_t b_name_len = strlen(b->name);
            fwrite(&b_name_len, sizeof(b_name_len), 1, f);
            fwrite(b->name, 1, b_name_len, f);
            fwrite(&b->profile.branch_probability, sizeof(double), 1, f);
            
            uint32_t num_successors = b->num_successors;
            fwrite(&num_successors, sizeof(num_successors), 1, f);
            for(uint32_t s = 0; s < num_successors; s++) {
                uint32_t s_id = b->successors[s]->id;
                fwrite(&s_id, sizeof(s_id), 1, f);
            }
            
            uint32_t num_insts = 0;
            for(qisc_ir_inst* i = b->first_inst; i; i = i->next) num_insts++;
            fwrite(&num_insts, sizeof(num_insts), 1, f);
            
            for(qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                fwrite(&i->id, sizeof(uint32_t), 1, f);
                uint32_t opc = i->opcode;
                fwrite(&opc, sizeof(opc), 1, f);
                uint32_t num_ops = i->num_operands;
                fwrite(&num_ops, sizeof(num_ops), 1, f);
                
                for(uint32_t op = 0; op < num_ops; op++) {
                    qisc_value* v = i->operands[op];
                    uint32_t kind = v->kind;
                    fwrite(&kind, sizeof(kind), 1, f);
                    
                    if (kind == QISC_VAL_CONST_INT) {
                        fwrite(&v->as.i_val, sizeof(int64_t), 1, f);
                    } else if (kind == QISC_VAL_CONST_FLOAT) {
                        fwrite(&v->as.f_val, sizeof(double), 1, f);
                    } else if (kind == QISC_VAL_CONST_BOOL) {
                        uint8_t bv = v->as.b_val ? 1 : 0;
                        fwrite(&bv, sizeof(uint8_t), 1, f);
                    } else if (kind == QISC_VAL_CONST_STRING) {
                        uint32_t slen = strlen(v->as.s_val);
                        fwrite(&slen, sizeof(slen), 1, f);
                        fwrite(v->as.s_val, 1, slen, f);
                    } else if (kind == QISC_VAL_INST) {
                        uint32_t ref_id = v->as.inst ? v->as.inst->id : 0;
                        fwrite(&ref_id, sizeof(ref_id), 1, f);
                    } else if (kind == QISC_VAL_PARAM) {
                        fwrite(&v->as.param_idx, sizeof(uint32_t), 1, f);
                    }
                }
            }
        }
    }
    
    fclose(f);
    return true;
}

qisc_ir_module* qisc_ir_deserialize(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    
    uint32_t magic = 0, version = 0, num_funcs = 0;
    uint64_t expected_hash = 0;
    
    if (fread(&magic, sizeof(magic), 1, f) != 1) { fclose(f); return NULL; }
    if (fread(&version, sizeof(version), 1, f) != 1) { fclose(f); return NULL; }
    
    if (magic != QISC_MAGIC_HEADER || (version != 1 && version != 2)) {
        fclose(f);
        return NULL;
    }
    
    if (version == 2) {
        if (fread(&num_funcs, sizeof(num_funcs), 1, f) != 1) { fclose(f); return NULL; }
        if (fread(&expected_hash, sizeof(expected_hash), 1, f) != 1) { fclose(f); return NULL; }
    }
    
    qisc_ir_module* mod = qisc_ir_create_module();
    qisc_ir_inst* inst_lookup[10000] = {0}; // Simplified flat lookup
    qisc_ir_block* block_lookup[10000] = {0};
    
    for (uint32_t fn = 0; fn < num_funcs; fn++) {
        uint32_t f_id = 0;
        if (version == 2) {
            if (fread(&f_id, sizeof(f_id), 1, f) != 1) { fclose(f); return NULL; }
        }
        uint32_t name_len = 0;
        if (fread(&name_len, sizeof(name_len), 1, f) != 1) { fclose(f); return NULL; }
        char name[256] = {0};
        if (name_len > 255) name_len = 255;
        if (fread(name, 1, name_len, f) != name_len) { fclose(f); return NULL; }
        
        uint8_t is_hot = 0;
        if (fread(&is_hot, sizeof(is_hot), 1, f) != 1) { fclose(f); return NULL; }
        uint64_t exec_count = 0;
        if (fread(&exec_count, sizeof(exec_count), 1, f) != 1) { fclose(f); return NULL; }
        
        uint32_t num_params = 0;
        if (fread(&num_params, sizeof(num_params), 1, f) != 1) { fclose(f); return NULL; }
        
        qisc_type** params = NULL;
        if (num_params > 0) {
            params = malloc(num_params * sizeof(qisc_type*));
            for(uint32_t p=0; p<num_params; p++) {
                uint32_t tk;
                if (fread(&tk, sizeof(tk), 1, f) != 1) { fclose(f); return NULL; }
                params[p] = calloc(1, sizeof(qisc_type));
                params[p]->kind = (qisc_type_kind)tk;
            }
        }
        
        qisc_ir_function* func = qisc_ir_create_function(mod, name, qisc_type_proc(qisc_type_int(), params, num_params), is_hot != 0);
        if (version == 2) func->id = f_id;
        func->profile.execution_count = exec_count;
        if (params) free(params);
        
        uint32_t num_blocks = 0;
        if (fread(&num_blocks, sizeof(num_blocks), 1, f) != 1) { fclose(f); return NULL; }
        
        for (uint32_t bk = 0; bk < num_blocks; bk++) {
            uint32_t b_id;
            if (fread(&b_id, sizeof(b_id), 1, f) != 1) { fclose(f); return NULL; }
            uint32_t b_name_len;
            if (fread(&b_name_len, sizeof(b_name_len), 1, f) != 1) { fclose(f); return NULL; }
            char bname[256] = {0};
            if (b_name_len > 255) b_name_len = 255;
            if (fread(bname, 1, b_name_len, f) != b_name_len) { fclose(f); return NULL; }
            
            double bprob;
            if (fread(&bprob, sizeof(bprob), 1, f) != 1) { fclose(f); return NULL; }
            
            qisc_ir_block* block = qisc_ir_create_block(func, bname, bprob);
            block->id = b_id;
            if (b_id < 10000) block_lookup[b_id] = block;
            
            uint32_t num_succ = 0;
            if (fread(&num_succ, sizeof(num_succ), 1, f) != 1) { fclose(f); return NULL; }
            
            uint32_t* succ_ids = NULL;
            if (num_succ > 0) {
                succ_ids = malloc(num_succ * sizeof(uint32_t));
                for(uint32_t s=0; s<num_succ; s++) {
                    if (fread(&succ_ids[s], sizeof(uint32_t), 1, f) != 1) { fclose(f); return NULL; }
                }
                // We stash these temp ids in successors to relink later
                block->successors = (qisc_ir_block**)succ_ids;
                block->num_successors = num_succ;
            }
            
            uint32_t num_insts = 0;
            if (fread(&num_insts, sizeof(num_insts), 1, f) != 1) { fclose(f); return NULL; }
            
            for (uint32_t in = 0; in < num_insts; in++) {
                uint32_t i_id, opc, num_ops;
                if (fread(&i_id, sizeof(i_id), 1, f) != 1) { fclose(f); return NULL; }
                if (fread(&opc, sizeof(opc), 1, f) != 1) { fclose(f); return NULL; }
                if (fread(&num_ops, sizeof(num_ops), 1, f) != 1) { fclose(f); return NULL; }
                
                qisc_value** ops = NULL;
                if (num_ops > 0) ops = malloc(num_ops * sizeof(qisc_value*));
                
                for(uint32_t op = 0; op < num_ops; op++) {
                    uint32_t kind;
                    if (fread(&kind, sizeof(kind), 1, f) != 1) { fclose(f); return NULL; }
                    
                    if (kind == QISC_VAL_CONST_INT) {
                        int64_t val;
                        if (fread(&val, sizeof(val), 1, f) != 1) { fclose(f); return NULL; }
                        ops[op] = qisc_value_int(val);
                    } else if (kind == QISC_VAL_CONST_FLOAT) {
                        double val;
                        if (fread(&val, sizeof(val), 1, f) != 1) { fclose(f); return NULL; }
                        ops[op] = qisc_value_float(val);
                    } else if (kind == QISC_VAL_CONST_BOOL) {
                        uint8_t val;
                        if (fread(&val, sizeof(val), 1, f) != 1) { fclose(f); return NULL; }
                        ops[op] = qisc_value_bool(val != 0);
                    } else if (kind == QISC_VAL_CONST_STRING) {
                        uint32_t slen;
                        if (fread(&slen, sizeof(slen), 1, f) != 1) { fclose(f); return NULL; }
                        char sbuf[256] = {0};
                        if (slen > 255) slen = 255;
                        if (fread(sbuf, 1, slen, f) != slen) { fclose(f); return NULL; }
                        ops[op] = qisc_value_string(sbuf);
                    } else if (kind == QISC_VAL_INST) {
                        uint32_t ref_id;
                        if (fread(&ref_id, sizeof(ref_id), 1, f) != 1) { fclose(f); return NULL; }
                        qisc_value* v = calloc(1, sizeof(qisc_value));
                        v->kind = QISC_VAL_INST;
                        v->as.inst = (qisc_ir_inst*)(uintptr_t)ref_id; // Stash id
                        ops[op] = v;
                    } else if (kind == QISC_VAL_PARAM) {
                        uint32_t pidx;
                        if (fread(&pidx, sizeof(pidx), 1, f) != 1) { fclose(f); return NULL; }
                        ops[op] = qisc_value_param(qisc_type_int(), pidx);
                    }
                }
                
                qisc_ir_inst* inst = qisc_ir_emit_inst(block, (qisc_opcode)opc, qisc_type_int(), ops, num_ops);
                inst->id = i_id;
                if (i_id < 10000) inst_lookup[i_id] = inst;
                if (ops) free(ops);
            }
        }
    }
    
    // Relink block successors
    for (qisc_ir_function* f_ptr = mod->first_func; f_ptr; f_ptr = f_ptr->next) {
        for (qisc_ir_block* b_ptr = f_ptr->first_block; b_ptr; b_ptr = b_ptr->next) {
            if (b_ptr->num_successors > 0) {
                uint32_t* stored_ids = (uint32_t*)b_ptr->successors;
                qisc_ir_block** real_succs = malloc(b_ptr->num_successors * sizeof(qisc_ir_block*));
                for(size_t s=0; s<b_ptr->num_successors; s++) {
                    uint32_t sid = stored_ids[s];
                    real_succs[s] = sid < 10000 ? block_lookup[sid] : NULL;
                }
                free(stored_ids);
                b_ptr->successors = real_succs;
            }
            
            // Relink operands
            for (qisc_ir_inst* i_ptr = b_ptr->first_inst; i_ptr; i_ptr = i_ptr->next) {
                for(size_t op = 0; op < i_ptr->num_operands; op++) {
                    if (i_ptr->operands[op]->kind == QISC_VAL_INST) {
                        uint32_t ref_id = (uint32_t)(uintptr_t)i_ptr->operands[op]->as.inst;
                        i_ptr->operands[op]->as.inst = ref_id < 10000 ? inst_lookup[ref_id] : NULL;
                        if (i_ptr->operands[op]->as.inst) i_ptr->operands[op]->type = i_ptr->operands[op]->as.inst->type;
                    }
                }
            }
        }
    }
    
    fclose(f);
    
    uint64_t actual_hash = qisc_ir_compute_hash(mod);
    if (actual_hash != expected_hash) {
        printf("ERROR: Hash mismatch during deserialization! Expected %016llx, got %016llx\n", 
               (unsigned long long)expected_hash, (unsigned long long)actual_hash);
        qisc_ir_destroy_module(mod);
        return NULL;
    }
    
    return mod;
}