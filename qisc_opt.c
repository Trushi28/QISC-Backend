#define _POSIX_C_SOURCE 200809L
#include "qisc_opt.h"
#include "qisc_cfg.h"
#include "qisc_ssa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double get_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

qisc_value* copy_value(qisc_value* v) {
    qisc_value* nv = calloc(1, sizeof(qisc_value));
    *nv = *v;
    if (v->kind == QISC_VAL_CONST_STRING) nv->as.s_val = strdup(v->as.s_val);
    return nv;
}

void replace_uses(qisc_ir_function* f, qisc_ir_inst* old_i, qisc_value* new_v) {
    for(qisc_ir_block* b = f->first_block; b; b = b->next) {
        for(qisc_ir_inst* i = b->first_inst; i; i = i->next) {
            for(size_t o=0; o<i->num_operands; o++) {
                if (i->operands[o]->kind == QISC_VAL_INST && i->operands[o]->as.inst == old_i) {
                    // Update use
                    if (new_v->kind == QISC_VAL_INST) i->operands[o]->as.inst = new_v->as.inst;
                    else {
                        free(i->operands[o]);
                        i->operands[o] = copy_value(new_v);
                    }
                }
            }
        }
    }
}

bool qisc_pass_ssa_construct_wrapper(qisc_ir_module* mod, uint64_t cycle) {
    (void)cycle;
    return qisc_ssa_construct(mod);
}

bool qisc_pass_ssa_destruct_wrapper(qisc_ir_module* mod, uint64_t cycle) {
    (void)cycle;
    return qisc_ssa_destruct(mod);
}

bool qisc_pass_constant_fold(qisc_ir_module* mod, uint64_t cycle) {
    bool changed = false;
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                if ((i->opcode == QISC_OP_ADD || i->opcode == QISC_OP_SUB || 
                     i->opcode == QISC_OP_MUL || i->opcode == QISC_OP_DIV) && i->num_operands == 2) {
                    if (i->operands[0]->kind == QISC_VAL_CONST_INT && i->operands[1]->kind == QISC_VAL_CONST_INT) {
                        int64_t v1 = i->operands[0]->as.i_val;
                        int64_t v2 = i->operands[1]->as.i_val;
                        int64_t res = 0;
                        if (i->opcode == QISC_OP_ADD) res = v1 + v2;
                        else if (i->opcode == QISC_OP_SUB) res = v1 - v2;
                        else if (i->opcode == QISC_OP_MUL) res = v1 * v2;
                        else if (i->opcode == QISC_OP_DIV) res = v2 != 0 ? v1 / v2 : 0;
                        
                        i->opcode = QISC_OP_NOP;
                        free(i->operands[0]); free(i->operands[1]); free(i->operands);
                        i->num_operands = 0;
                        i->operands = NULL;
                        
                        qisc_value* new_val = qisc_value_int(res);
                        replace_uses(f, i, new_val);
                        free(new_val->type); free(new_val);
                        
                        qisc_ir_record_mutation(mod, "Constant fold ALU", cycle, f->name, i->id);
                        changed = true;
                    }
                } else if ((i->opcode == QISC_OP_CMP_EQ || i->opcode == QISC_OP_CMP_LT || i->opcode == QISC_OP_CMP_GT) && i->num_operands == 2) {
                    if (i->operands[0]->kind == QISC_VAL_CONST_INT && i->operands[1]->kind == QISC_VAL_CONST_INT) {
                        int64_t v1 = i->operands[0]->as.i_val;
                        int64_t v2 = i->operands[1]->as.i_val;
                        int64_t res = 0;
                        if (i->opcode == QISC_OP_CMP_EQ) res = (v1 == v2);
                        else if (i->opcode == QISC_OP_CMP_LT) res = (v1 < v2);
                        else if (i->opcode == QISC_OP_CMP_GT) res = (v1 > v2);
                        
                        i->opcode = QISC_OP_NOP;
                        free(i->operands[0]); free(i->operands[1]); free(i->operands);
                        i->num_operands = 0;
                        i->operands = NULL;
                        
                        qisc_value* new_val = qisc_value_int(res);
                        replace_uses(f, i, new_val);
                        free(new_val->type); free(new_val);
                        
                        qisc_ir_record_mutation(mod, "Constant fold CMP", cycle, f->name, i->id);
                        changed = true;
                    }
                } else if (i->opcode == QISC_OP_BR_COND && i->num_operands == 1) {
                    if (i->operands[0]->kind == QISC_VAL_CONST_INT) {
                        int64_t cond = i->operands[0]->as.i_val;
                        i->opcode = QISC_OP_BR;
                        free(i->operands[0]); free(i->operands);
                        i->num_operands = 0;
                        i->operands = NULL;
                        
                        if (cond != 0) {
                            if (b->num_successors > 1) {
                                // True block
                                qisc_ir_block* tblock = b->successors[0];
                                qisc_ir_block* fblock = b->successors[1];
                                free(b->successors);
                                b->successors = malloc(sizeof(qisc_ir_block*));
                                b->successors[0] = tblock;
                                b->num_successors = 1;
                                fblock->profile.branch_probability = 0.0;
                            }
                        } else {
                            if (b->num_successors > 1) {
                                // False block
                                qisc_ir_block* tblock = b->successors[0];
                                qisc_ir_block* fblock = b->successors[1];
                                free(b->successors);
                                b->successors = malloc(sizeof(qisc_ir_block*));
                                b->successors[0] = fblock;
                                b->num_successors = 1;
                                tblock->profile.branch_probability = 0.0;
                            }
                        }
                        
                        qisc_ir_record_mutation(mod, "Constant fold BR_COND", cycle, f->name, i->id);
                        changed = true;
                    }
                }
            }
        }
    }
    return changed;
}

bool qisc_pass_copy_propagation(qisc_ir_module* mod, uint64_t cycle) {
    bool changed = false;
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                for (size_t o=0; o<i->num_operands; o++) {
                    if (i->operands[o]->kind == QISC_VAL_INST) {
                        qisc_ir_inst* ref = i->operands[o]->as.inst;
                        qisc_ir_inst* orig_ref = ref;
                        while (ref && ref->opcode == QISC_OP_NOP && ref->num_operands == 1 && ref->operands[0]->kind == QISC_VAL_INST) {
                            ref = ref->operands[0]->as.inst;
                        }
                        if (ref && ref->opcode == QISC_OP_NOP && ref->num_operands == 1 && ref->operands[0]->kind != QISC_VAL_INST) {
                            if (ref->operands[0]->kind != QISC_VAL_PARAM) {
                                free(i->operands[o]);
                                i->operands[o] = copy_value(ref->operands[0]);
                                qisc_ir_record_mutation(mod, "Copy prop to literal", cycle, f->name, i->id);
                                changed = true;
                            }
                        } else if (ref && ref != orig_ref) {
                            i->operands[o]->as.inst = ref;
                            qisc_ir_record_mutation(mod, "Copy prop", cycle, f->name, i->id);
                            changed = true;
                        }
                    }
                }
            }
        }
    }
    return changed;
}

bool qisc_pass_inline(qisc_ir_module* mod, uint64_t cycle) {
    bool changed = false;
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        if (!f->profile.is_hot) continue;
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                if (i->opcode == QISC_OP_CALL) {
                    const char* callee_name = i->operands[0]->as.s_val;
                    if (strcmp(f->name, callee_name) == 0) continue;
                    
                    qisc_ir_function* callee = NULL;
                    for (qisc_ir_function* cf = mod->first_func; cf; cf = cf->next) {
                        if (strcmp(cf->name, callee_name) == 0) { callee = cf; break; }
                    }
                    if (!callee || !callee->profile.is_hot) continue;
                    
                    int count = 0; bool has_await = false;
                    for(qisc_ir_block* cb = callee->first_block; cb; cb = cb->next) {
                        for(qisc_ir_inst* ci = cb->first_inst; ci; ci = ci->next) {
                            count++; if(ci->opcode == QISC_OP_AWAIT_DATA) has_await = true;
                        }
                    }
                    if (count > 32 || has_await) continue;
                    
                    qisc_value* param_map[32] = {0};
                    for(size_t p=1; p<i->num_operands && p-1 < 32; p++) param_map[p-1] = i->operands[p];
                    
                    qisc_ir_block* merge_block = qisc_ir_create_block(f, "merge", 1.0);
                    qisc_ir_inst* next_i = i->next;
                    while (next_i) {
                        qisc_ir_inst* move_i = next_i;
                        next_i = move_i->next;
                        if (move_i->prev) move_i->prev->next = move_i->next;
                        if (move_i->next) move_i->next->prev = move_i->prev;
                        if (b->last_inst == move_i) b->last_inst = move_i->prev;
                        if (b->first_inst == move_i) b->first_inst = move_i->next;
                        
                        move_i->parent_block = merge_block;
                        move_i->prev = merge_block->last_inst;
                        move_i->next = NULL;
                        if (merge_block->last_inst) merge_block->last_inst->next = move_i;
                        else merge_block->first_inst = move_i;
                        merge_block->last_inst = move_i;
                    }
                    
                    qisc_ir_block* block_map[10000] = {0};
                    qisc_ir_inst* inst_map[10000] = {0};
                    for(qisc_ir_block* cb = callee->first_block; cb; cb = cb->next) {
                        block_map[cb->id] = qisc_ir_create_block(f, cb->name, cb->profile.branch_probability);
                    }
                    for(qisc_ir_block* cb = callee->first_block; cb; cb = cb->next) {
                        qisc_ir_block* nb = block_map[cb->id];
                        for(qisc_ir_inst* ci = cb->first_inst; ci; ci = ci->next) {
                            qisc_value** ops = NULL;
                            if (ci->num_operands > 0) ops = malloc(sizeof(qisc_value*) * ci->num_operands);
                            for(size_t o=0; o<ci->num_operands; o++) {
                                if (ci->operands[o]->kind == QISC_VAL_PARAM) {
                                    size_t p_idx = ci->operands[o]->as.param_idx;
                                    if (p_idx < 32 && param_map[p_idx]) {
                                        ops[o] = copy_value(param_map[p_idx]);
                                    } else {
                                        ops[o] = copy_value(ci->operands[o]);
                                    }
                                } else if (ci->operands[o]->kind == QISC_VAL_INST) {
                                    qisc_value* v = calloc(1, sizeof(qisc_value));
                                    v->kind = QISC_VAL_INST; v->type = ci->operands[o]->type;
                                    v->as.inst = inst_map[ci->operands[o]->as.inst->id];
                                    ops[o] = v;
                                } else {
                                    ops[o] = copy_value(ci->operands[o]);
                                }
                            }
                            qisc_ir_inst* ni = qisc_ir_emit_inst(nb, ci->opcode, ci->type, ops, ci->num_operands);
                            if(ops) free(ops);
                            inst_map[ci->id] = ni;
                            if (ni->opcode == QISC_OP_RET) {
                                ni->opcode = QISC_OP_BR;
                                qisc_value* res_val = NULL;
                                if (ni->num_operands > 0) { res_val = ni->operands[0]; replace_uses(f, i, res_val); }
                                ni->num_operands = 0; free(ni->operands); ni->operands = NULL;
                                nb->successors = malloc(sizeof(qisc_ir_block*));
                                nb->successors[0] = merge_block;
                                nb->num_successors = 1;
                            }
                        }
                        if (cb->num_successors > 0) {
                            nb->successors = malloc(sizeof(qisc_ir_block*) * cb->num_successors);
                            nb->num_successors = cb->num_successors;
                            for(size_t s=0; s<cb->num_successors; s++) nb->successors[s] = block_map[cb->successors[s]->id];
                        }
                    }
                    
                    for(qisc_ir_block* cb = callee->first_block; cb; cb = cb->next) {
                        for(qisc_ir_inst* ci = cb->first_inst; ci; ci = ci->next) {
                            qisc_ir_inst* ni = inst_map[ci->id];
                            for(size_t o=0; o<ni->num_operands; o++) {
                                if (ni->operands[o]->kind == QISC_VAL_INST && !ni->operands[o]->as.inst) {
                                    ni->operands[o]->as.inst = inst_map[ci->operands[o]->as.inst->id];
                                }
                            }
                        }
                    }
                    
                    i->opcode = QISC_OP_BR;
                    i->num_operands = 0; free(i->operands); i->operands = NULL;
                    b->successors = malloc(sizeof(qisc_ir_block*));
                    b->successors[0] = block_map[callee->first_block->id];
                    b->num_successors = 1;
                    
                    char msg[256]; snprintf(msg, sizeof(msg), "inlined %s into %s at inst %u", callee->name, f->name, i->id);
                    qisc_ir_record_mutation(mod, msg, cycle, f->name, i->id);
                    changed = true;
                    break;
                }
            }
        }
    }
    return changed;
}

bool qisc_pass_cold_outline(qisc_ir_module* mod, uint64_t cycle) {
    bool changed = false;
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            if (b == f->first_block) continue;
            if (b->profile.branch_probability < 0.05 && b->first_inst) {
                
                qisc_ir_inst* inputs[100]; int num_inputs = 0;
                qisc_ir_inst* outputs[100]; int num_outputs = 0;
                
                for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                    for(size_t o=0; o<i->num_operands; o++) {
                        if (i->operands[o]->kind == QISC_VAL_INST && i->operands[o]->as.inst->parent_block != b) {
                            bool found = false;
                            for(int k=0; k<num_inputs; k++) if(inputs[k] == i->operands[o]->as.inst) found = true;
                            if(!found) inputs[num_inputs++] = i->operands[o]->as.inst;
                        }
                    }
                }
                
                for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                    for (qisc_ir_block* ob = f->first_block; ob; ob = ob->next) {
                        if (ob == b) continue;
                        for (qisc_ir_inst* oi = ob->first_inst; oi; oi = oi->next) {
                            for(size_t o=0; o<oi->num_operands; o++) {
                                if (oi->operands[o]->kind == QISC_VAL_INST && oi->operands[o]->as.inst == i) {
                                    bool found = false;
                                    for(int k=0; k<num_outputs; k++) if(outputs[k] == i) found = true;
                                    if(!found) outputs[num_outputs++] = i;
                                }
                            }
                        }
                    }
                }
                
                char new_name[256]; snprintf(new_name, sizeof(new_name), "%s_cold_%u", f->name, b->id);
                qisc_type* cold_type = qisc_type_int(); // simplification for output type
                qisc_ir_function* cold_f = qisc_ir_create_function(mod, new_name, cold_type, false);
                qisc_ir_block* cb = qisc_ir_create_block(cold_f, "entry", 1.0);
                
                qisc_ir_inst* inst = b->first_inst;
                while (inst) {
                    qisc_ir_inst* next = inst->next;
                    inst->parent_block = cb;
                    inst->prev = cb->last_inst;
                    inst->next = NULL;
                    if (cb->last_inst) cb->last_inst->next = inst;
                    else cb->first_inst = inst;
                    cb->last_inst = inst;
                    
                    for(size_t o=0; o<inst->num_operands; o++) {
                        if (inst->operands[o]->kind == QISC_VAL_INST) {
                            for(int k=0; k<num_inputs; k++) {
                                if (inputs[k] == inst->operands[o]->as.inst) {
                                    free(inst->operands[o]);
                                    inst->operands[o] = qisc_value_param(qisc_type_int(), k);
                                }
                            }
                        }
                    }
                    inst = next;
                }
                b->first_inst = b->last_inst = NULL;
                
                if (num_outputs > 0) {
                    qisc_value* ret_ops[] = { qisc_value_inst(outputs[0]) };
                    qisc_ir_emit_inst(cb, QISC_OP_RET, qisc_type_int(), ret_ops, 1);
                } else {
                    qisc_ir_emit_inst(cb, QISC_OP_RET, qisc_type_int(), NULL, 0);
                }
                
                qisc_value** call_ops = malloc(sizeof(qisc_value*) * (1 + num_inputs));
                qisc_value* func_name_val = calloc(1, sizeof(qisc_value));
                func_name_val->kind = QISC_VAL_CONST_STRING;
                func_name_val->as.s_val = strdup(new_name);
                call_ops[0] = func_name_val;
                for(int k=0; k<num_inputs; k++) call_ops[k+1] = qisc_value_inst(inputs[k]);
                qisc_ir_inst* call_inst = qisc_ir_emit_inst(b, QISC_OP_CALL, qisc_type_int(), call_ops, 1 + num_inputs);
                free(call_ops);
                
                if (num_outputs > 0) {
                    replace_uses(f, outputs[0], qisc_value_inst(call_inst));
                }
                qisc_ir_emit_inst(b, QISC_OP_BR, qisc_type_int(), NULL, 0);
                
                char msg[256]; snprintf(msg, sizeof(msg), "outlined cold block %s from %s", b->name, f->name);
                qisc_ir_record_mutation(mod, msg, cycle, f->name, b->id);
                changed = true;
            }
        }
    }
    return changed;
}

bool qisc_pass_specialize_constants(qisc_ir_module* mod, uint64_t cycle) {
    bool changed = false;
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        int call_sites = 0;
        int64_t constant_val = 0;
        bool is_const_uniform = true;
        
        for (qisc_ir_function* cf = mod->first_func; cf; cf = cf->next) {
            for (qisc_ir_block* cb = cf->first_block; cb; cb = cb->next) {
                for (qisc_ir_inst* ci = cb->first_inst; ci; ci = ci->next) {
                    if (ci->opcode == QISC_OP_CALL && strcmp(ci->operands[0]->as.s_val, f->name) == 0) {
                        call_sites++;
                        if (ci->num_operands > 1 && ci->operands[1]->kind == QISC_VAL_CONST_INT) {
                            if (call_sites == 1) constant_val = ci->operands[1]->as.i_val;
                            else if (constant_val != ci->operands[1]->as.i_val) is_const_uniform = false;
                        } else {
                            is_const_uniform = false;
                        }
                    }
                }
            }
        }
        
        if (call_sites >= 2 && is_const_uniform) {
            char new_name[256]; snprintf(new_name, sizeof(new_name), "%s_spec_p0_%ld", f->name, (long)constant_val);
            bool exists = false;
            for (qisc_ir_function* cf = mod->first_func; cf; cf = cf->next) {
                if (strcmp(cf->name, new_name) == 0) exists = true;
            }
            if (exists) continue;
            
            qisc_ir_function* spec_f = qisc_ir_create_function(mod, new_name, f->type, f->profile.is_hot);
            qisc_ir_block* block_map[10000] = {0};
            qisc_ir_inst* inst_map[10000] = {0};
            for(qisc_ir_block* cb = f->first_block; cb; cb = cb->next) {
                block_map[cb->id] = qisc_ir_create_block(spec_f, cb->name, cb->profile.branch_probability);
            }
            for(qisc_ir_block* cb = f->first_block; cb; cb = cb->next) {
                qisc_ir_block* nb = block_map[cb->id];
                for(qisc_ir_inst* ci = cb->first_inst; ci; ci = ci->next) {
                    qisc_value** ops = NULL;
                    if (ci->num_operands > 0) ops = malloc(sizeof(qisc_value*) * ci->num_operands);
                    for(size_t o=0; o<ci->num_operands; o++) {
                        if (ci->operands[o]->kind == QISC_VAL_PARAM && ci->operands[o]->as.param_idx == 0) {
                            ops[o] = qisc_value_int(constant_val);
                        } else if (ci->operands[o]->kind == QISC_VAL_INST) {
                            qisc_value* v = calloc(1, sizeof(qisc_value));
                            v->kind = QISC_VAL_INST; v->type = ci->operands[o]->type;
                            v->as.inst = inst_map[ci->operands[o]->as.inst->id];
                            ops[o] = v;
                        } else ops[o] = copy_value(ci->operands[o]);
                    }
                    qisc_ir_inst* ni = qisc_ir_emit_inst(nb, ci->opcode, ci->type, ops, ci->num_operands);
                    if(ops) free(ops);
                    inst_map[ci->id] = ni;
                    
                    if ((ni->opcode == QISC_OP_ADD || ni->opcode == QISC_OP_SUB || ni->opcode == QISC_OP_MUL || ni->opcode == QISC_OP_DIV) && 
                        ni->num_operands == 2 && ni->operands[0]->kind == QISC_VAL_CONST_INT && ni->operands[1]->kind == QISC_VAL_CONST_INT) {
                        int64_t v0 = ni->operands[0]->as.i_val;
                        int64_t v1 = ni->operands[1]->as.i_val;
                        int64_t res = 0;
                        if (ni->opcode == QISC_OP_ADD) res = v0 + v1;
                        else if (ni->opcode == QISC_OP_SUB) res = v0 - v1;
                        else if (ni->opcode == QISC_OP_MUL) res = v0 * v1;
                        else if (ni->opcode == QISC_OP_DIV) res = v1 != 0 ? v0 / v1 : 0;
                        ni->opcode = QISC_OP_NOP;
                        free(ni->operands[0]); free(ni->operands[1]);
                        ni->operands[0] = qisc_value_int(res);
                        ni->num_operands = 1;
                    }
                }
            }
            
            for (qisc_ir_function* cf = mod->first_func; cf; cf = cf->next) {
                for (qisc_ir_block* cb = cf->first_block; cb; cb = cb->next) {
                    for (qisc_ir_inst* ci = cb->first_inst; ci; ci = ci->next) {
                        if (ci->opcode == QISC_OP_CALL && strcmp(ci->operands[0]->as.s_val, f->name) == 0) {
                            free((void*)ci->operands[0]->as.s_val);
                            ci->operands[0]->as.s_val = strdup(new_name);
                            free(ci->operands[1]);
                            for (size_t o=1; o<ci->num_operands-1; o++) ci->operands[o] = ci->operands[o+1];
                            ci->num_operands--;
                        }
                    }
                }
            }
            
            char msg[256]; snprintf(msg, sizeof(msg), "specialized %s param 0 = %ld", f->name, (long)constant_val);
            qisc_ir_record_mutation(mod, msg, cycle, f->name, f->id);
            changed = true;
        }
    }
    return changed;
}

bool qisc_pass_dead_code_elimination(qisc_ir_module* mod, uint64_t cycle) {
    bool changed = false;
    bool cascade = true;
    while (cascade) {
        cascade = false;
        int use_counts[10000] = {0};
        
        // Remove unreachable blocks
        for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
            qisc_cfg* cfg = qisc_cfg_build(f);
            for (size_t i = 0; i < cfg->num_nodes; i++) {
                if (cfg->nodes[i].block && cfg->nodes[i].block != f->first_block) {
                    if (cfg->nodes[i].preorder == -1 || cfg->nodes[i].block->profile.branch_probability <= 0.0) {
                        qisc_ir_block* b = cfg->nodes[i].block;
                        qisc_ir_inst* inst = b->first_inst;
                        while(inst) {
                            qisc_ir_inst* next = inst->next;
                            char msg[256]; snprintf(msg, sizeof(msg), "DCE unreachable inst %u", inst->id);
                            qisc_ir_record_mutation(mod, msg, cycle, f->name, inst->id);
                            for(size_t op=0; op<inst->num_operands; op++) free(inst->operands[op]);
                            free(inst->operands);
                            free(inst->phi_incoming_blocks);
                            free(inst);
                            inst = next;
                        }
                        b->first_inst = b->last_inst = NULL;
                        
                        // remove from function
                        if (b->prev) b->prev->next = b->next;
                        if (b->next) b->next->prev = b->prev;
                        if (f->last_block == b) f->last_block = b->prev;
                        free((void*)b->name);
                        free(b->successors);
                        free(b->predecessors);
                        free(b);
                        
                        cascade = true;
                        changed = true;
                    }
                }
            }
            qisc_cfg_destroy(cfg);
        }
        if (cascade) continue;

        for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
            for (qisc_ir_block* b = f->first_block; b; b = b->next) {
                for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                    for (size_t o=0; o<i->num_operands; o++) {
                        if (i->operands[o]->kind == QISC_VAL_INST && i->operands[o]->as.inst && i->operands[o]->as.inst->id < 10000) {
                            use_counts[i->operands[o]->as.inst->id]++;
                        }
                    }
                }
            }
        }
        
        for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
            for (qisc_ir_block* b = f->first_block; b; b = b->next) {
                qisc_ir_inst* i = b->first_inst;
                while (i) {
                    qisc_ir_inst* next = i->next;
                    if (i->id < 10000 && use_counts[i->id] == 0 && 
                        i->opcode != QISC_OP_RET && i->opcode != QISC_OP_BR && i->opcode != QISC_OP_BR_COND && 
                        i->opcode != QISC_OP_CALL && i->opcode != QISC_OP_STORE && i->opcode != QISC_OP_FAIL && 
                        i->opcode != QISC_OP_EMIT && i->opcode != QISC_OP_TRY && i->opcode != QISC_OP_CATCH &&
                        i->opcode != QISC_OP_AWAIT_DATA) {
                        
                        if (i->prev) i->prev->next = i->next;
                        if (i->next) i->next->prev = i->prev;
                        if (b->first_inst == i) b->first_inst = i->next;
                        if (b->last_inst == i) b->last_inst = i->prev;
                        char msg[256]; snprintf(msg, sizeof(msg), "DCE removed inst %u opcode %u in %s", i->id, i->opcode, f->name);
                        qisc_ir_record_mutation(mod, msg, cycle, f->name, i->id);
                        for(size_t op=0; op<i->num_operands; op++) free(i->operands[op]);
                        free(i->operands);
                        free(i->phi_incoming_blocks);
                        free(i);
                        cascade = true;
                        changed = true;
                    }
                    i = next;
                }
            }
        }
    }
    return changed;
}

qisc_pass_pipeline* qisc_pipeline_create(void) {
    qisc_pass_pipeline* p = calloc(1, sizeof(qisc_pass_pipeline));
    p->capacity = 16;
    p->passes = calloc(p->capacity, sizeof(qisc_pass));
    
    qisc_pipeline_add_pass(p, QISC_PASS_CONSTANT_FOLD);
    qisc_pipeline_add_pass(p, QISC_PASS_COPY_PROPAGATION);
    qisc_pipeline_add_pass(p, QISC_PASS_DEAD_CODE_ELIM);
    qisc_pipeline_add_pass(p, QISC_PASS_INLINE);
    qisc_pipeline_add_pass(p, QISC_PASS_COLD_OUTLINE);
    qisc_pipeline_add_pass(p, QISC_PASS_CONST_SPECIALIZE);
    return p;
}

void qisc_pipeline_add_pass(qisc_pass_pipeline* p, qisc_pass_id id) {
    if (p->num_passes >= p->capacity) {
        p->capacity *= 2;
        p->passes = realloc(p->passes, p->capacity * sizeof(qisc_pass));
    }
    qisc_pass* pass = &p->passes[p->num_passes++];
    pass->id = id;
    pass->times_run = 0;
    pass->total_mutations = 0;
    pass->total_time_seconds = 0.0;
    
    switch(id) {
        case QISC_PASS_CONSTANT_FOLD: pass->name = "constant_fold"; pass->run = qisc_pass_constant_fold; break;
        case QISC_PASS_COPY_PROPAGATION: pass->name = "copy_propagation"; pass->run = qisc_pass_copy_propagation; break;
        case QISC_PASS_DEAD_CODE_ELIM: pass->name = "dead_code_elim"; pass->run = qisc_pass_dead_code_elimination; break;
        case QISC_PASS_INLINE: pass->name = "inline"; pass->run = qisc_pass_inline; break;
        case QISC_PASS_COLD_OUTLINE: pass->name = "cold_outline"; pass->run = qisc_pass_cold_outline; break;
        case QISC_PASS_CONST_SPECIALIZE: pass->name = "const_specialize"; pass->run = qisc_pass_specialize_constants; break;
        case QISC_PASS_SSA_CONSTRUCT: pass->name = "ssa_construct"; pass->run = qisc_pass_ssa_construct_wrapper; break;
        case QISC_PASS_SSA_DESTRUCT: pass->name = "ssa_destruct"; pass->run = qisc_pass_ssa_destruct_wrapper; break;
    }
}

bool qisc_pipeline_run_once(qisc_pass_pipeline* p, qisc_ir_module* mod, uint64_t cycle) {
    bool changed = false;
    for (size_t i=0; i<p->num_passes; i++) {
        qisc_pass* pass = &p->passes[i];
        double start = get_time_sec();
        
        uint32_t mut_before = 0;
        for(qisc_mutation_entry* e = mod->mutation_log->head; e; e = e->next) mut_before++;
        
        bool r = pass->run(mod, cycle);
        
        uint32_t mut_after = 0;
        for(qisc_mutation_entry* e = mod->mutation_log->head; e; e = e->next) mut_after++;
        
        double end = get_time_sec();
        pass->times_run++;
        pass->total_mutations += (mut_after - mut_before);
        pass->total_time_seconds += (end - start);
        
        if (r) changed = true;
    }
    return changed;
}

void qisc_pipeline_print_stats(qisc_pass_pipeline* p) {
    printf("Pass                  | Runs | Mutations | Time\n");
    printf("----------------------|------|-----------|------\n");
    for (size_t i=0; i<p->num_passes; i++) {
        qisc_pass* pass = &p->passes[i];
        printf("%-21s | %4llu | %9llu | %.3fs\n", pass->name, (unsigned long long)pass->times_run, (unsigned long long)pass->total_mutations, pass->total_time_seconds);
    }
}

void qisc_pipeline_destroy(qisc_pass_pipeline* p) {
    if (!p) return;
    free(p->passes);
    free(p);
}

void qisc_opt_run_pipeline(qisc_ir_module* mod) {
    qisc_pass_pipeline* p = qisc_pipeline_create();
    uint64_t cycle = 1;
    bool changed = true;
    while (changed) {
        changed = qisc_pipeline_run_once(p, mod, cycle);
        cycle++;
        if (cycle > 100) break;
    }
    qisc_pipeline_destroy(p);
}
