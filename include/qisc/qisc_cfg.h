#ifndef QISC_CFG_H
#define QISC_CFG_H

#include "qisc_ir.h"
#include <stdbool.h>

typedef struct qisc_cfg_edge {
    qisc_ir_block*      from;
    qisc_ir_block*      to;
    double              probability;
    struct qisc_cfg_edge* next;
} qisc_cfg_edge;

typedef struct qisc_cfg_node {
    qisc_ir_block*      block;
    qisc_cfg_edge*      successors;
    qisc_cfg_edge*      predecessors;
    size_t              num_successors;
    size_t              num_predecessors;
    // Dominance
    struct qisc_cfg_node* idom;        // immediate dominator
    struct qisc_cfg_node** dom_frontier; // dominance frontier
    size_t              dom_frontier_count;
    // DFS ordering
    int                 preorder;
    int                 postorder;
    bool                visited;
} qisc_cfg_node;

typedef struct qisc_cfg {
    qisc_ir_function*   func;
    qisc_cfg_node*      nodes;      // array, one per block
    size_t              num_nodes;
    qisc_cfg_node*      entry;
    qisc_cfg_node*      exit;       // synthetic exit node
    bool                is_valid;
} qisc_cfg;

qisc_cfg* qisc_cfg_build(qisc_ir_function* func);
void qisc_cfg_invalidate(qisc_cfg* cfg);
void qisc_cfg_compute_dominators(qisc_cfg* cfg);
void qisc_cfg_compute_dominance_frontier(qisc_cfg* cfg);
bool qisc_cfg_dominates(qisc_cfg* cfg, qisc_cfg_node* a, qisc_cfg_node* b);
void qisc_cfg_print(qisc_cfg* cfg);
void qisc_cfg_destroy(qisc_cfg* cfg);

#endif // QISC_CFG_H
