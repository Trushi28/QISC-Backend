#define _POSIX_C_SOURCE 200809L
#include "qisc_ssa.h"
#include <stdlib.h>
#include <string.h>

qisc_ssa_builder* qisc_ssa_create(qisc_cfg* cfg) {
    qisc_ssa_builder* b = calloc(1, sizeof(qisc_ssa_builder));
    b->cfg = cfg;
    b->max_vars = 10000;
    b->max_blocks = 10000;
    b->current_def = calloc(b->max_vars * b->max_blocks, sizeof(qisc_ir_inst*));
    return b;
}

void qisc_ssa_destroy(qisc_ssa_builder* b) {
    if (!b) return;
    free(b->current_def);
    free(b);
}

void qisc_ssa_write_variable(qisc_ssa_builder* b, uint32_t var_id, qisc_ir_block* block, qisc_ir_inst* value) {
    if (var_id < b->max_vars && block->id < b->max_blocks) {
        b->current_def[var_id * b->max_blocks + block->id] = value;
    }
}

qisc_ir_inst* qisc_ssa_read_variable(qisc_ssa_builder* b, uint32_t var_id, qisc_ir_block* block) {
    if (var_id < b->max_vars && block->id < b->max_blocks) {
        qisc_ir_inst* val = b->current_def[var_id * b->max_blocks + block->id];
        if (val) return val;
    }
    return qisc_ssa_read_variable_recursive(b, var_id, block);
}

qisc_ir_inst* qisc_ssa_read_variable_recursive(qisc_ssa_builder* b, uint32_t var_id, qisc_ir_block* block) {
    qisc_ir_inst* val = NULL;
    qisc_cfg_node* node = NULL;
    for(size_t i=0; i<b->cfg->num_nodes; i++) {
        if (b->cfg->nodes[i].block == block) {
            node = &b->cfg->nodes[i];
            break;
        }
    }
    if (!node) return NULL;
    
    if (node->num_predecessors == 1) {
        val = qisc_ssa_read_variable(b, var_id, node->predecessors->from);
    } else if (node->num_predecessors > 1) {
        qisc_ir_inst** incoming_vals = calloc(node->num_predecessors, sizeof(qisc_ir_inst*));
        qisc_ir_block** incoming_blks = calloc(node->num_predecessors, sizeof(qisc_ir_block*));
        
        qisc_ir_inst* phi = qisc_ir_emit_phi(block, qisc_type_int(), incoming_vals, incoming_blks, node->num_predecessors);
        qisc_ssa_write_variable(b, var_id, block, phi);
        
        size_t idx = 0;
        bool all_same = true;
        qisc_ir_inst* first_val = NULL;
        
        for (qisc_cfg_edge* p = node->predecessors; p; p = p->next) {
            qisc_ir_inst* pval = qisc_ssa_read_variable(b, var_id, p->from);
            phi->operands[idx] = qisc_value_inst(pval);
            phi->phi_incoming_blocks[idx] = p->from;
            if (idx == 0) first_val = pval;
            else if (pval != first_val && pval != phi) all_same = false;
            if (pval && (!phi->type || phi->type->kind == QISC_TYPE_VOID)) phi->type = pval->type;
            idx++;
        }
        
        if (all_same) {
            if (phi->prev) phi->prev->next = phi->next;
            if (phi->next) phi->next->prev = phi->prev;
            if (block->first_inst == phi) block->first_inst = phi->next;
            if (block->last_inst == phi) block->last_inst = phi->prev;
            
            for(size_t op=0; op<phi->num_operands; op++) free(phi->operands[op]);
            free(phi->operands);
            free(phi->phi_incoming_blocks);
            free(phi);
            
            val = first_val;
        } else {
            val = phi;
        }
        free(incoming_vals);
        free(incoming_blks);
    }
    
    qisc_ssa_write_variable(b, var_id, block, val);
    return val;
}

static bool qisc_ssa_eliminate_trivial_phis(qisc_ir_module* mod) {
    bool changed = false;
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            qisc_ir_inst* i = b->first_inst;
            while (i) {
                qisc_ir_inst* next = i->next;
                if (i->opcode == QISC_OP_PHI && i->num_operands > 0) {
                    bool all_same = true;
                    qisc_ir_inst* first_val = i->operands[0]->kind == QISC_VAL_INST ? i->operands[0]->as.inst : NULL;
                    for (size_t op = 1; op < i->num_operands; op++) {
                        qisc_ir_inst* op_inst = i->operands[op]->kind == QISC_VAL_INST ? i->operands[op]->as.inst : NULL;
                        if (op_inst != first_val && op_inst != i) {
                            all_same = false;
                            break;
                        }
                    }
                    if (all_same) {
                        for (qisc_ir_function* af = mod->first_func; af; af = af->next) {
                            for (qisc_ir_block* ab = af->first_block; ab; ab = ab->next) {
                                for (qisc_ir_inst* ai = ab->first_inst; ai; ai = ai->next) {
                                    for (size_t aop = 0; aop < ai->num_operands; aop++) {
                                        if (ai->operands[aop]->kind == QISC_VAL_INST && ai->operands[aop]->as.inst == i) {
                                            ai->operands[aop]->as.inst = first_val;
                                        }
                                    }
                                }
                            }
                        }
                        if (i->prev) i->prev->next = i->next;
                        if (i->next) i->next->prev = i->prev;
                        if (b->first_inst == i) b->first_inst = i->next;
                        if (b->last_inst == i) b->last_inst = i->prev;
                        
                        for(size_t op = 0; op < i->num_operands; op++) free(i->operands[op]);
                        free(i->operands);
                        free(i->phi_incoming_blocks);
                        free(i);
                        
                        changed = true;
                    }
                }
                i = next;
            }
        }
    }
    return changed;
}

bool qisc_ssa_construct(qisc_ir_module* mod) {
    bool inserted_phis = false;
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        f->is_in_ssa_form = true;
        qisc_cfg* cfg = qisc_cfg_build(f);
        qisc_ssa_builder* b = qisc_ssa_create(cfg);
        
        for (qisc_ir_block* blk = f->first_block; blk; blk = blk->next) {
            for (qisc_ir_inst* i = blk->first_inst; i; i = i->next) {
                if (i->opcode != QISC_OP_PHI) {
                    qisc_ssa_write_variable(b, i->id, blk, i);
                }
            }
        }
        
        for (qisc_ir_block* blk = f->first_block; blk; blk = blk->next) {
            for (qisc_ir_inst* i = blk->first_inst; i; i = i->next) {
                if (i->opcode == QISC_OP_PHI) continue;
                for (size_t op=0; op<i->num_operands; op++) {
                    if (i->operands[op]->kind == QISC_VAL_INST) {
                        qisc_ir_inst* ref = i->operands[op]->as.inst;
                        if (ref) {
                            qisc_ir_inst* ssa_val = qisc_ssa_read_variable(b, ref->id, blk);
                            if (ssa_val && ssa_val != ref) {
                                i->operands[op]->as.inst = ssa_val;
                                if (ssa_val->opcode == QISC_OP_PHI) inserted_phis = true;
                            }
                        }
                    }
                }
            }
        }
        
        qisc_ssa_destroy(b);
        qisc_cfg_destroy(cfg);
    }
    
    while (qisc_ssa_eliminate_trivial_phis(mod));
    
    return inserted_phis;
}

bool qisc_ssa_destruct(qisc_ir_module* mod) {
    bool processed = false;
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        f->is_in_ssa_form = false;
        for (qisc_ir_block* b = f->first_block; b; b = b->next) {
            qisc_ir_inst* i = b->first_inst;
            while (i) {
                qisc_ir_inst* next = i->next;
                if (i->opcode == QISC_OP_PHI) {
                    qisc_ir_inst* lowered_value = NULL;
                    for(size_t inc=0; inc<i->phi_num_incoming; inc++) {
                        qisc_ir_block* pred = i->phi_incoming_blocks[inc];
                        qisc_ir_inst* val = i->operands[inc]->as.inst;
                        
                        qisc_value* copy_ops[] = { qisc_value_inst(val) };
                        qisc_ir_inst* copy = qisc_ir_emit_inst(pred, QISC_OP_NOP, i->type, copy_ops, 1);
                        copy->id = i->id;
                        if (!lowered_value) lowered_value = copy;
                        
                        if (pred->last_inst && pred->last_inst != copy) {
                            qisc_ir_inst* term = pred->last_inst->prev;
                            if (term && (term->opcode == QISC_OP_BR || term->opcode == QISC_OP_BR_COND || term->opcode == QISC_OP_RET || term->opcode == QISC_OP_EMIT)) {
                                if (copy->prev) copy->prev->next = copy->next;
                                if (copy->next) copy->next->prev = copy->prev;
                                if (pred->last_inst == copy) pred->last_inst = copy->prev;
                                
                                copy->prev = term->prev;
                                copy->next = term;
                                if (term->prev) term->prev->next = copy;
                                else pred->first_inst = copy;
                                term->prev = copy;
                            }
                        }
                        
                        for (qisc_ir_block* ub = f->first_block; ub; ub = ub->next) {
                            for (qisc_ir_inst* ui = ub->first_inst; ui; ui = ui->next) {
                                for (size_t uop=0; uop<ui->num_operands; uop++) {
                                    if (ui->operands[uop]->kind == QISC_VAL_INST && ui->operands[uop]->as.inst == i) {
                                        ui->operands[uop]->as.inst = lowered_value;
                                    }
                                }
                            }
                        }
                    }
                    
                    if (i->prev) i->prev->next = i->next;
                    if (i->next) i->next->prev = i->prev;
                    if (b->first_inst == i) b->first_inst = i->next;
                    if (b->last_inst == i) b->last_inst = i->prev;
                    for(size_t op=0; op<i->num_operands; op++) free(i->operands[op]);
                    free(i->operands);
                    free(i->phi_incoming_blocks);
                    free(i);
                    
                    processed = true;
                }
                i = next;
            }
        }
    }
    return processed;
}
