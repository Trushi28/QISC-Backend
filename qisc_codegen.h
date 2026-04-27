#ifndef QISC_CODEGEN_H
#define QISC_CODEGEN_H

#include "qisc_ir.h"
#include <stdbool.h>

// Emit object file (ELF format)
// Separates hot functions into .text.hot and cold into .text.cold
bool qisc_codegen_emit_elf(qisc_ir_module* mod, const char* filepath);

// Architecture specific enums for pseudo-registers
typedef enum {
    QISC_REG_STREAM_CONT, // Stream continuation pointer
    QISC_REG_CLOSURE_ENV, // Closure environment pointer
    QISC_REG_COMP_DATA,   // Component data pointer
    // General purpose registers
    QISC_REG_RAX,
    QISC_REG_RCX,
    QISC_REG_RDX,
    QISC_REG_RBX,
    QISC_REG_RSI,
    QISC_REG_RDI,
    QISC_REG_R8,
    QISC_REG_R9,
    QISC_REG_R10,
    QISC_REG_R11,
    QISC_REG_R12,
    QISC_REG_R13,
    QISC_REG_R14,
    QISC_REG_R15,
    QISC_REG_COUNT
} qisc_x86_reg;

// Initialize register allocator
void qisc_codegen_init_regalloc(void);

// Instruction selection mapping
void qisc_codegen_select_instructions(qisc_ir_function* func);

#endif // QISC_CODEGEN_H
