#define _POSIX_C_SOURCE 200809L
#include "qisc_cfg.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void add_edge(qisc_cfg_node* from, qisc_cfg_node* to, double prob) {
    qisc_cfg_edge* succ_edge = calloc(1, sizeof(qisc_cfg_edge));
    succ_edge->from = from->block;
    succ_edge->to = to->block;
    succ_edge->probability = prob;
    succ_edge->next = from->successors;
    from->successors = succ_edge;
    from->num_successors++;

    qisc_cfg_edge* pred_edge = calloc(1, sizeof(qisc_cfg_edge));
    pred_edge->from = from->block;
    pred_edge->to = to->block;
    pred_edge->probability = prob;
    pred_edge->next = to->predecessors;
    to->predecessors = pred_edge;
    to->num_predecessors++;
}

// Since edge stores qisc_ir_block*, we need a way to get qisc_cfg_node* from block.
// We can use a simple loop or ID map.

qisc_cfg* qisc_cfg_build(qisc_ir_function* func) {
    qisc_cfg* cfg = calloc(1, sizeof(qisc_cfg));
    cfg->func = func;
    
    size_t count = 0;
    for (qisc_ir_block* b = func->first_block; b; b = b->next) count++;
    cfg->num_nodes = count + 1; // +1 for synthetic exit
    cfg->nodes = calloc(cfg->num_nodes, sizeof(qisc_cfg_node));
    
    // Map block pointers to nodes
    qisc_cfg_node** node_map = calloc(10000, sizeof(qisc_cfg_node*));
    
    size_t idx = 0;
    for (qisc_ir_block* b = func->first_block; b; b = b->next) {
        cfg->nodes[idx].block = b;
        cfg->nodes[idx].preorder = -1;
        cfg->nodes[idx].postorder = -1;
        if (b->id < 10000) node_map[b->id] = &cfg->nodes[idx];
        if (b == func->first_block) cfg->entry = &cfg->nodes[idx];
        idx++;
    }
    
    qisc_cfg_node* exit_node = &cfg->nodes[idx];
    exit_node->block = NULL; // Synthetic exit
    exit_node->preorder = -1;
    exit_node->postorder = -1;
    cfg->exit = exit_node;
    
    for (size_t i = 0; i < count; ++i) {
        qisc_cfg_node* n = &cfg->nodes[i];
        qisc_ir_block* b = n->block;
        qisc_ir_inst* last = b->last_inst;
        
        if (last) {
            if (last->opcode == QISC_OP_BR) {
                if (b->num_successors > 0 && b->successors[0]->id < 10000) {
                    qisc_cfg_node* target = node_map[b->successors[0]->id];
                    add_edge(n, target, 1.0);
                }
            } else if (last->opcode == QISC_OP_BR_COND) {
                if (b->num_successors > 0 && b->successors[0]->id < 10000) {
                    qisc_cfg_node* target1 = node_map[b->successors[0]->id];
                    add_edge(n, target1, b->profile.branch_probability);
                }
                if (b->num_successors > 1 && b->successors[1]->id < 10000) {
                    qisc_cfg_node* target2 = node_map[b->successors[1]->id];
                    add_edge(n, target2, 1.0 - b->profile.branch_probability);
                }
            } else if (last->opcode == QISC_OP_RET || last->opcode == QISC_OP_EMIT) {
                add_edge(n, exit_node, 1.0);
            } else {
                // Implicit fallthrough if it's not a branch but has successors?
                if (b->num_successors > 0 && b->successors[0]->id < 10000) {
                    qisc_cfg_node* target = node_map[b->successors[0]->id];
                    add_edge(n, target, 1.0);
                }
            }
        }
    }
    
    // DFS
    int pre = 0, post = 0;
    qisc_cfg_node* stack[10000];
    int state[10000] = {0}; // 0 = not visited, 1 = visiting, 2 = visited
    qisc_cfg_edge* edge_stack[10000];
    
    if (cfg->entry) {
        int top = 0;
        stack[top] = cfg->entry;
        state[cfg->entry->block->id] = 1;
        cfg->entry->preorder = pre++;
        edge_stack[top] = cfg->entry->successors;
        
        while (top >= 0) {
            qisc_cfg_node* u = stack[top];
            qisc_cfg_edge* e = edge_stack[top];
            
            if (e) {
                edge_stack[top] = e->next;
                qisc_cfg_node* v = NULL;
                if (e->to) {
                    if (e->to->id < 10000) v = node_map[e->to->id];
                } else {
                    v = cfg->exit;
                }
                if (v && v != cfg->exit && state[v->block->id] == 0) { // skip synthetic exit in postorder for now, or assign an ID
                    state[v->block->id] = 1;
                    v->preorder = pre++;
                    top++;
                    stack[top] = v;
                    edge_stack[top] = v->successors;
                } else if (v == cfg->exit && state[9999] == 0) { // hack for synthetic
                    state[9999] = 1;
                    v->preorder = pre++;
                    top++;
                    stack[top] = v;
                    edge_stack[top] = v->successors;
                }
            } else {
                u->postorder = post++;
                top--;
            }
        }
    }
    
    free(node_map);
    return cfg;
}

static qisc_cfg_node* get_node(qisc_cfg* cfg, qisc_ir_block* b) {
    if (!b) return cfg->exit;
    for(size_t i=0; i<cfg->num_nodes; i++) {
        if(cfg->nodes[i].block == b) return &cfg->nodes[i];
    }
    return NULL;
}

static qisc_cfg_node* intersect(qisc_cfg_node* b1, qisc_cfg_node* b2) {
    while (b1 != b2 && b1 && b2) {
        while (b1 && b2 && b1->postorder < b2->postorder) {
            b1 = b1->idom;
        }
        while (b1 && b2 && b2->postorder < b1->postorder) {
            b2 = b2->idom;
        }
    }
    return b1;
}

void qisc_cfg_compute_dominators(qisc_cfg* cfg) {
    if (!cfg->entry) return;
    cfg->entry->idom = cfg->entry;
    
    // Reverse postorder sort
    qisc_cfg_node** rpo = calloc(cfg->num_nodes, sizeof(qisc_cfg_node*));
    int rpo_count = 0;
    for (int p = (int)cfg->num_nodes - 1; p >= 0; p--) {
        for (size_t i=0; i<cfg->num_nodes; i++) {
            if (cfg->nodes[i].postorder == p) {
                rpo[rpo_count++] = &cfg->nodes[i];
            }
        }
    }
    
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < rpo_count; i++) {
            qisc_cfg_node* n = rpo[i];
            if (n == cfg->entry) continue;
            
            qisc_cfg_node* new_idom = NULL;
            for (qisc_cfg_edge* p = n->predecessors; p; p = p->next) {
                qisc_cfg_node* pred_node = get_node(cfg, p->from);
                if (pred_node && pred_node->idom != NULL) {
                    if (new_idom == NULL) {
                        new_idom = pred_node;
                    } else {
                        new_idom = intersect(pred_node, new_idom);
                    }
                }
            }
            if (n->idom != new_idom) {
                n->idom = new_idom;
                changed = true;
            }
        }
    }
    free(rpo);
}

void qisc_cfg_compute_dominance_frontier(qisc_cfg* cfg) {
    for (size_t i=0; i<cfg->num_nodes; i++) {
        qisc_cfg_node* b = &cfg->nodes[i];
        if (b->num_predecessors >= 2) {
            for (qisc_cfg_edge* p = b->predecessors; p; p = p->next) {
                qisc_cfg_node* runner = get_node(cfg, p->from);
                while (runner && runner != b->idom) {
                    bool found = false;
                    for (size_t j=0; j<runner->dom_frontier_count; j++) {
                        if (runner->dom_frontier[j] == b) { found = true; break; }
                    }
                    if (!found) {
                        runner->dom_frontier = realloc(runner->dom_frontier, sizeof(qisc_cfg_node*) * (runner->dom_frontier_count + 1));
                        runner->dom_frontier[runner->dom_frontier_count++] = b;
                    }
                    runner = runner->idom;
                }
            }
        }
    }
}

bool qisc_cfg_dominates(qisc_cfg_node* a, qisc_cfg_node* b) {
    if (!a || !b) return false;
    qisc_cfg_node* curr = b;
    while (curr && curr != curr->idom) {
        if (curr == a) return true;
        curr = curr->idom;
    }
    return curr == a; // checks if entry dominates
}

void qisc_cfg_print(qisc_cfg* cfg) {
    if (!cfg || !cfg->func) return;
    printf("digraph %s {\n", cfg->func->name);
    for (size_t i=0; i<cfg->num_nodes; i++) {
        qisc_cfg_node* n = &cfg->nodes[i];
        const char* from_name = n->block ? n->block->name : "exit";
        for (qisc_cfg_edge* e = n->successors; e; e = e->next) {
            const char* to_name = e->to ? e->to->name : "exit";
            printf("  %s -> %s [label=\"%.2f\"]\n", from_name, to_name, e->probability);
        }
    }
    printf("}\n");
}

void qisc_cfg_destroy(qisc_cfg* cfg) {
    if (!cfg) return;
    for (size_t i=0; i<cfg->num_nodes; i++) {
        qisc_cfg_edge* curr = cfg->nodes[i].successors;
        while (curr) {
            qisc_cfg_edge* next = curr->next;
            free(curr);
            curr = next;
        }
        curr = cfg->nodes[i].predecessors;
        while (curr) {
            qisc_cfg_edge* next = curr->next;
            free(curr);
            curr = next;
        }
        free(cfg->nodes[i].dom_frontier);
    }
    free(cfg->nodes);
    free(cfg);
}