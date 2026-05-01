#ifndef QISC_IR_H
#define QISC_IR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define QISC_MAGIC_HEADER 0x51495343 // "QISC"
#define QISC_IR_VERSION 1

// Forward declarations
typedef struct qisc_type qisc_type;
typedef struct qisc_value qisc_value;
typedef struct qisc_ir_inst qisc_ir_inst;
typedef struct qisc_ir_block qisc_ir_block;
typedef struct qisc_ir_function qisc_ir_function;
typedef struct qisc_ir_module qisc_ir_module;
typedef struct qisc_mutation_log qisc_mutation_log;

// Living Component Lifecycle States
typedef enum {
    QISC_STATE_DORMANT,
    QISC_STATE_TRIGGERED,
    QISC_STATE_EXECUTING,
    QISC_STATE_RETURNING
} qisc_component_state;

// Types matching QISC
typedef enum {
    QISC_TYPE_INT,
    QISC_TYPE_FLOAT,
    QISC_TYPE_BOOL,
    QISC_TYPE_STRING,
    QISC_TYPE_ARRAY,
    QISC_TYPE_STREAM,
    QISC_TYPE_PROC,
    QISC_TYPE_MAYBE,
    QISC_TYPE_STRUCT,
    QISC_TYPE_ENUM,
    QISC_TYPE_VOID,
    QISC_TYPE_PTR // For internal use
} qisc_type_kind;

struct qisc_type {
    qisc_type_kind kind;
    qisc_type* inner; // For Array, Stream, Maybe, Ptr
    qisc_type** params; // For Proc
    size_t num_params;
    qisc_type* ret_type; // For Proc
};

// Opcodes
typedef enum {
    QISC_OP_NOP,
    QISC_OP_ADD,
    QISC_OP_SUB,
    QISC_OP_MUL,
    QISC_OP_DIV,
    QISC_OP_CMP_EQ,
    QISC_OP_CMP_LT,
    QISC_OP_CMP_GT,
    QISC_OP_BR,         // Unconditional branch
    QISC_OP_BR_COND,    // Conditional branch
    QISC_OP_CALL,
    QISC_OP_RET,        // give
    QISC_OP_LOAD,
    QISC_OP_STORE,
    QISC_OP_ALLOCA,
    QISC_OP_TRY,
    QISC_OP_CATCH,
    QISC_OP_FAIL,
    QISC_OP_STREAM_RANGE,
    QISC_OP_STREAM_MAP,
    QISC_OP_STREAM_REDUCE,
    QISC_OP_STREAM_FILTER,
    QISC_OP_PIPELINE,   // >>
    QISC_OP_AWAIT_DATA, // Living Component
    QISC_OP_EMIT,       // Living Component
    QISC_OP_PHI         // SSA Phi node
} qisc_opcode;

/*
 * QISC IR Value Model:
 * TAC mode (default, is_in_ssa_form=false):
 *   - Instructions produce values via operand refs
 *   - Multiple definitions of same logical var allowed
 *   - Used for: Living IR mutations, cloning, 
 *               serialization, profile-driven transforms
 *
 * SSA mode (is_in_ssa_form=true):
 *   - Every instruction produces exactly ONE value
 *   - No redefinition of values
 *   - PHI nodes resolve control flow merges
 *   - Used for: const folding, DCE, copy propagation
 *   - Enter via: qisc_ssa_construct()
 *   - Exit via:  qisc_ssa_destruct()
 *
 * Rule: NEVER mutate CFG (clone/outline/specialize)
 * while is_in_ssa_form=true. Always destruct first.
 */

// Values
typedef enum {
    QISC_VAL_CONST_INT,
    QISC_VAL_CONST_FLOAT,
    QISC_VAL_CONST_BOOL,
    QISC_VAL_CONST_STRING,
    QISC_VAL_INST,      // Result of an instruction
    QISC_VAL_PARAM,
    QISC_VAL_UNDEF
} qisc_value_kind;

struct qisc_value {
    qisc_value_kind kind;
    qisc_type* type;
    union {
        int64_t i_val;
        double f_val;
        bool b_val;
        const char* s_val;
        qisc_ir_inst* inst;
        uint32_t param_idx;
    } as;
};

// Observation Hook API
typedef void (*qisc_ir_observe_fn)(qisc_ir_inst*);

// Profile Data embedded in structs
typedef struct {
    uint64_t execution_count;
    double branch_probability; // For blocks
    bool is_hot; // Derived from profile
} qisc_profile_data;

// Instruction
struct qisc_ir_inst {
    uint32_t id;
    qisc_opcode opcode;
    qisc_type* type;
    qisc_value** operands;
    size_t num_operands;
    qisc_ir_block* parent_block;
    qisc_ir_inst* next;
    qisc_ir_inst* prev;
    
    // SSA Phi node data
    qisc_ir_block** phi_incoming_blocks;
    size_t          phi_num_incoming;
    
    // Instruction constraint flags
    bool            requires_rax;
    bool            clobbers_rdx;
    bool            clobbers_rax;
    
    // Living components
    qisc_component_state comp_state;
};

// Basic Block
struct qisc_ir_block {
    uint32_t id;
    const char* name;
    qisc_ir_inst* first_inst;
    qisc_ir_inst* last_inst;
    qisc_ir_function* parent_func;
    qisc_ir_block* next;
    qisc_ir_block* prev;
    
    // Successors/Predecessors for CFG
    qisc_ir_block** successors;
    size_t num_successors;
    qisc_ir_block** predecessors;
    size_t num_predecessors;
    
    // Embedded profile data
    qisc_profile_data profile;
};

// Mutation log entry
typedef struct qisc_mutation_entry {
    uint64_t cycle;
    const char* reason;
    const char* entity_name;
    uint32_t entity_id;
    struct qisc_mutation_entry* next;
} qisc_mutation_entry;

struct qisc_mutation_log {
    qisc_mutation_entry* head;
    qisc_mutation_entry* tail;
};

// Function
struct qisc_ir_function {
    uint32_t id;
    const char* name;
    qisc_type* type; // Proc type
    qisc_ir_block* first_block;
    qisc_ir_block* last_block;
    qisc_ir_module* parent_module;
    qisc_ir_function* next;
    qisc_ir_function* prev;
    
    // Embedded profile data
    qisc_profile_data profile;
    
    // IR Mode state
    bool is_in_ssa_form;
    struct qisc_cfg* cfg;
};

// Module
struct qisc_ir_module {
    qisc_ir_function* first_func;
    qisc_ir_function* last_func;
    qisc_mutation_log* mutation_log;
    qisc_ir_observe_fn observer;
};

// APIs

// Module & Setup
qisc_ir_module* qisc_ir_create_module(void);
void qisc_ir_set_observer(qisc_ir_module* mod, qisc_ir_observe_fn observer);
void qisc_ir_destroy_module(qisc_ir_module* mod);

// Functions
qisc_ir_function* qisc_ir_create_function(qisc_ir_module* mod, const char* name, qisc_type* type, bool is_hot);

// Blocks
qisc_ir_block* qisc_ir_create_block(qisc_ir_function* func, const char* name, double branch_probability);

// Instructions
qisc_ir_inst* qisc_ir_emit_inst(qisc_ir_block* block, qisc_opcode op, qisc_type* type, qisc_value** operands, size_t num_operands);
qisc_ir_inst* qisc_ir_emit_await_data(qisc_ir_block* block, qisc_type* data_type);
qisc_ir_inst* qisc_ir_emit_emit(qisc_ir_block* block, qisc_value* val);

// Values
qisc_value* qisc_value_int(int64_t val);
qisc_value* qisc_value_inst(qisc_ir_inst* inst);
qisc_value* qisc_value_param(qisc_type* type, uint32_t idx);

// Types
qisc_type* qisc_type_int(void);
qisc_type* qisc_type_proc(qisc_type* ret_type, qisc_type** params, size_t num_params);

// Mutation
void qisc_ir_record_mutation(qisc_ir_module* mod, const char* reason, uint64_t cycle, const char* entity_name, uint32_t entity_id);

// Serialization & Convergence
uint64_t qisc_ir_compute_hash(qisc_ir_module* mod); // Hashes structure + profile data
bool qisc_ir_serialize(qisc_ir_module* mod, const char* filepath);
qisc_ir_module* qisc_ir_deserialize(const char* filepath);

bool qisc_ir_validate_module(qisc_ir_module* mod, char* error_buf, size_t error_buf_len);
void qisc_ir_print_module(qisc_ir_module* mod);

qisc_ir_inst* qisc_ir_emit_phi(qisc_ir_block* block, qisc_type* type, qisc_ir_inst** incoming_values, qisc_ir_block** incoming_blocks, size_t count);

qisc_value* qisc_value_float(double val);
qisc_value* qisc_value_string(const char* str);
qisc_value* qisc_value_bool(bool val);
qisc_type* qisc_type_float(void);

#endif // QISC_IR_H
