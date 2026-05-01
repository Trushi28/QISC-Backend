#define _POSIX_C_SOURCE 200809L
#include "qisc_codegen.h"
#include "qisc_living_component.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

struct qisc_bytebuf {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
};

void bytebuf_init(qisc_bytebuf* b) {
    b->capacity = 1024; b->size = 0;
    b->data = (uint8_t*)malloc(b->capacity);
}
void bytebuf_push(qisc_bytebuf* b, uint8_t byte) {
    if (b->size >= b->capacity) {
        b->capacity *= 2;
        b->data = (uint8_t*)realloc(b->data, b->capacity);
    }
    b->data[b->size++] = byte;
}
void bytebuf_push_u32(qisc_bytebuf* b, uint32_t v) {
    bytebuf_push(b, v & 0xFF); bytebuf_push(b, (v >> 8) & 0xFF);
    bytebuf_push(b, (v >> 16) & 0xFF); bytebuf_push(b, (v >> 24) & 0xFF);
}
void bytebuf_push_u64(qisc_bytebuf* b, uint64_t v) {
    bytebuf_push_u32(b, (uint32_t)(v & 0xFFFFFFFF));
    bytebuf_push_u32(b, (uint32_t)(v >> 32));
}
size_t bytebuf_here(qisc_bytebuf* b) { return b->size; }
void bytebuf_patch_u32(qisc_bytebuf* b, size_t offset, uint32_t v) {
    if (offset + 4 <= b->size) {
        b->data[offset] = v & 0xFF; b->data[offset+1] = (v >> 8) & 0xFF;
        b->data[offset+2] = (v >> 16) & 0xFF; b->data[offset+3] = (v >> 24) & 0xFF;
    }
}
void bytebuf_free(qisc_bytebuf* b) { free(b->data); b->size = b->capacity = 0; }

typedef struct {
    uint32_t inst_id;
    int      start;
    int      end;
    int      stack_slot;
    qisc_x86_reg reg;
} qisc_live_interval;

typedef struct {
    size_t patch_site;
    uint32_t target_block_id;
} qisc_branch_patch;

typedef struct {
    size_t offset;
    const char* symbol_name;
    qisc_ir_function* caller;
} qisc_reloc;

typedef struct {
    size_t offset;
    size_t rodata_offset;
    qisc_ir_function* caller;
} qisc_rodata_reloc;

typedef uint64_t Elf64_Addr;
typedef uint16_t Elf64_Half;
typedef uint64_t Elf64_Off;
typedef int32_t  Elf64_Sword;
typedef int64_t  Elf64_Sxword;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

typedef struct {
    unsigned char e_ident[16];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word    sh_name;
    Elf64_Word    sh_type;
    Elf64_Xword   sh_flags;
    Elf64_Addr    sh_addr;
    Elf64_Off     sh_offset;
    Elf64_Xword   sh_size;
    Elf64_Word    sh_link;
    Elf64_Word    sh_info;
    Elf64_Xword   sh_addralign;
    Elf64_Xword   sh_entsize;
} Elf64_Shdr;

typedef struct {
    Elf64_Word    st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half    st_shndx;
    Elf64_Addr    st_value;
    Elf64_Xword   st_size;
} Elf64_Sym;

typedef struct {
    Elf64_Addr    r_offset;
    Elf64_Xword   r_info;
    Elf64_Sxword  r_addend;
} Elf64_Rela;

#define ELF64_R_INFO(sym, type) (((uint64_t)(sym) << 32) + ((type) & 0xffffffffL))

static int reg_to_id(qisc_x86_reg r) {
    switch(r) {
        case QISC_REG_RAX: return 0;
        case QISC_REG_RCX: return 1;
        case QISC_REG_RDX: return 2;
        case QISC_REG_RBX: return 3;
        case QISC_REG_RSI: return 6;
        case QISC_REG_RDI: return 7;
        case QISC_REG_R8:  return 8;
        case QISC_REG_R9:  return 9;
        case QISC_REG_R10: return 10;
        case QISC_REG_R11: return 11;
        case QISC_REG_STREAM_CONT: return 12;
        case QISC_REG_CLOSURE_ENV: return 13;
        case QISC_REG_COMP_DATA:   return 14;
        default: return 0;
    }
}

void emit_rex(qisc_bytebuf* buf, int w, int r, int x, int b) {
    uint8_t rex = 0x40 | (w<<3) | (r<<2) | (x<<1) | b;
    bytebuf_push(buf, rex);
}
void emit_rex_op(qisc_bytebuf* buf, int dst, int src) {
    emit_rex(buf, 1, dst >= 8 ? 1 : 0, 0, src >= 8 ? 1 : 0);
}
void emit_modrm(qisc_bytebuf* buf, uint8_t mod, uint8_t reg, uint8_t rm) {
    bytebuf_push(buf, (mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

void emit_prologue(qisc_bytebuf* buf, int frame_size) {
    bytebuf_push(buf, 0x55);
    bytebuf_push(buf, 0x48); bytebuf_push(buf, 0x89); bytebuf_push(buf, 0xE5);
    if (frame_size > 0) {
        if (frame_size <= 127) {
            bytebuf_push(buf, 0x48); bytebuf_push(buf, 0x83); bytebuf_push(buf, 0xEC);
            bytebuf_push(buf, frame_size);
        } else {
            bytebuf_push(buf, 0x48); bytebuf_push(buf, 0x81); bytebuf_push(buf, 0xEC);
            bytebuf_push_u32(buf, frame_size);
        }
    }
}
void emit_epilogue(qisc_bytebuf* buf) {
    bytebuf_push(buf, 0x48); bytebuf_push(buf, 0x89); bytebuf_push(buf, 0xEC);
    bytebuf_push(buf, 0x5D);
}

int load_operand(qisc_bytebuf* buf, qisc_value* val, qisc_live_interval** id_to_inter, int default_reg, qisc_bytebuf* rodata_buf, qisc_rodata_reloc* rodata_relocs, int* num_rodata_relocs, qisc_ir_function* f) {
    if (val->kind == QISC_VAL_CONST_INT || val->kind == QISC_VAL_CONST_BOOL) {
        int64_t v = val->kind == QISC_VAL_CONST_INT ? val->as.i_val : (val->as.b_val ? 1 : 0);
        if (v >= -2147483648LL && v <= 2147483647LL) {
            emit_rex(buf, 1, 0, 0, default_reg >= 8 ? 1 : 0);
            bytebuf_push(buf, 0xC7);
            emit_modrm(buf, 3, 0, default_reg & 7);
            bytebuf_push_u32(buf, (uint32_t)v);
        } else {
            emit_rex(buf, 1, 0, 0, default_reg >= 8 ? 1 : 0);
            bytebuf_push(buf, 0xB8 + (default_reg & 7));
            bytebuf_push_u64(buf, v);
        }
        return default_reg;
    } else if (val->kind == QISC_VAL_CONST_FLOAT) {
        // float constants in xmm
        uint64_t v_bits;
        memcpy(&v_bits, &val->as.f_val, sizeof(double));
        int xmm_reg = default_reg; // XMM0-7 maps directly to 0-7
        emit_rex(buf, 1, 0, 0, xmm_reg >= 8 ? 1 : 0);
        bytebuf_push(buf, 0xB8 + (xmm_reg & 7));
        bytebuf_push_u64(buf, v_bits);
        // movq xmm, reg -> 66 48 0F 6E /r
        bytebuf_push(buf, 0x66);
        emit_rex_op(buf, xmm_reg, xmm_reg);
        bytebuf_push(buf, 0x0F);
        bytebuf_push(buf, 0x6E);
        emit_modrm(buf, 3, xmm_reg & 7, xmm_reg & 7);
        return xmm_reg;
    } else if (val->kind == QISC_VAL_CONST_STRING) {
        size_t r_offset = bytebuf_here(rodata_buf);
        size_t len = strlen(val->as.s_val);
        for(size_t i=0; i<len; i++) bytebuf_push(rodata_buf, val->as.s_val[i]);
        bytebuf_push(rodata_buf, 0); // null term
        
        // lea dst, [rip + offset] -> 48 8D 05 + imm32
        emit_rex_op(buf, default_reg, 5);
        bytebuf_push(buf, 0x8D);
        emit_modrm(buf, 0, default_reg & 7, 5);
        
        if (rodata_relocs) {
            rodata_relocs[(*num_rodata_relocs)++] = (qisc_rodata_reloc){ bytebuf_here(buf), r_offset, f };
        }
        bytebuf_push_u32(buf, 0); // patched via relocs
        return default_reg;
    } else if (val->kind == QISC_VAL_INST) {
        qisc_live_interval* inter = id_to_inter[val->as.inst->id];
        if (inter && inter->stack_slot != -1) {
            if (val->as.inst->type && val->as.inst->type->kind == QISC_TYPE_FLOAT) {
                // movsd xmm, [rbp - offset] -> F2 0F 10 /r
                bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x10);
                emit_modrm(buf, 2, default_reg & 7, 5);
                bytebuf_push_u32(buf, (uint32_t)(-inter->stack_slot));
                return default_reg;
            } else {
                emit_rex(buf, 1, default_reg >= 8 ? 1 : 0, 0, 0);
                bytebuf_push(buf, 0x8B);
                emit_modrm(buf, 2, default_reg & 7, 5);
                bytebuf_push_u32(buf, (uint32_t)(-inter->stack_slot));
                return default_reg;
            }
        } else if (inter) {
            return reg_to_id(inter->reg);
        }
    } else if (val->kind == QISC_VAL_PARAM) {
        int param_regs[] = {7, 6, 2, 1, 8, 9};
        if (val->as.param_idx < 6) {
            int src_reg = param_regs[val->as.param_idx];
            if (val->type && val->type->kind == QISC_TYPE_FLOAT) {
                // XMM0-7 are args 0-7
                src_reg = val->as.param_idx;
                if (default_reg != src_reg) {
                    // movsd xmm_dst, xmm_src -> F2 0F 10 /r
                    bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x10);
                    emit_modrm(buf, 3, default_reg & 7, src_reg & 7);
                }
                return default_reg;
            } else {
                if (default_reg != src_reg) {
                    emit_rex_op(buf, default_reg, src_reg); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, default_reg, src_reg);
                }
                return default_reg;
            }
        }
    }
    return default_reg;
}

qisc_reloc relocs[10000];
int num_relocs = 0;

qisc_rodata_reloc r_relocs[10000];
int num_r_relocs = 0;

typedef struct {
    qisc_ir_function* func;
    qisc_bytebuf buf;
} qisc_func_code;

qisc_func_code func_codes[1000];
int num_func_codes = 0;

qisc_bytebuf rodata_buffer;

void qisc_codegen_init_regalloc(void) {}
void qisc_codegen_select_instructions(qisc_ir_function* func) { (void)func; }

bool qisc_codegen_emit_elf(qisc_ir_module* mod, const char* filepath) {
    num_relocs = 0;
    num_r_relocs = 0;
    num_func_codes = 0;
    bytebuf_init(&rodata_buffer);
    
    for (qisc_ir_function* f = mod->first_func; f; f = f->next) {
        int inst_idx = 0;
        uint32_t max_id = 0;
        for(qisc_ir_block* b = f->first_block; b; b = b->next) {
            for(qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                if (i->id > max_id) max_id = i->id;
            }
        }
        
        qisc_live_interval* intervals = calloc(max_id + 1, sizeof(qisc_live_interval));
        int num_intervals = 0;
        int frame_alloca = 0;
        int call_inst_indices[1000]; int num_calls = 0;
        int temp_idx = 0;
        
        for(qisc_ir_block* b = f->first_block; b; b = b->next) {
            for(qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                if (i->opcode == QISC_OP_CALL) call_inst_indices[num_calls++] = temp_idx;
                temp_idx++;
            }
        }
        
        inst_idx = 0;
        for(qisc_ir_block* b = f->first_block; b; b = b->next) {
            for(qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                if (i->opcode == QISC_OP_ALLOCA) frame_alloca += 8;
                if (i->opcode == QISC_OP_TRY) frame_alloca += 8;
                
                if (i->type && i->type->kind != QISC_TYPE_VOID) {
                    intervals[num_intervals].inst_id = i->id;
                    intervals[num_intervals].start = inst_idx;
                    intervals[num_intervals].end = inst_idx;
                    intervals[num_intervals].stack_slot = -1;
                    num_intervals++;
                }
                for (size_t o = 0; o < i->num_operands; o++) {
                    if (i->operands[o]->kind == QISC_VAL_INST) {
                        uint32_t dep_id = i->operands[o]->as.inst->id;
                        for(int j=0; j<num_intervals; j++) {
                            if(intervals[j].inst_id == dep_id) {
                                intervals[j].end = inst_idx;
                                break;
                            }
                        }
                    }
                }
                inst_idx++;
            }
        }
        
        for (int i=0; i<num_intervals-1; i++) {
            for (int j=0; j<num_intervals-i-1; j++) {
                if (intervals[j].start > intervals[j+1].start) {
                    qisc_live_interval tmp = intervals[j];
                    intervals[j] = intervals[j+1];
                    intervals[j+1] = tmp;
                }
            }
        }
        
        int active[32]; int num_active = 0;
        qisc_x86_reg avail_regs[] = {QISC_REG_RCX, QISC_REG_RSI, QISC_REG_RDI, QISC_REG_R8, QISC_REG_R9, QISC_REG_R10, QISC_REG_R11};
        bool reg_free[QISC_REG_COUNT];
        for(int i=0; i<QISC_REG_COUNT; i++) reg_free[i] = false;
        for(size_t i=0; i<sizeof(avail_regs) / sizeof(avail_regs[0]); i++) reg_free[avail_regs[i]] = true;
        
        int next_stack_slot = 16;
        
        for (int i=0; i<num_intervals; i++) {
            bool crosses_call = false;
            for(int c=0; c<num_calls; c++) {
                if (intervals[i].start < call_inst_indices[c] && intervals[i].end > call_inst_indices[c]) {
                    crosses_call = true; break;
                }
            }
            if (crosses_call) {
                intervals[i].reg = 0; // dummy
                intervals[i].stack_slot = next_stack_slot; 
                next_stack_slot += 8;
                continue;
            }
            int start = intervals[i].start;
            for (int j=0; j<num_active; j++) {
                int idx = active[j];
                if (intervals[idx].end < start) {
                    if (intervals[idx].stack_slot == -1) reg_free[intervals[idx].reg] = true;
                    active[j] = active[num_active-1]; num_active--; j--;
                }
            }
            int assigned = -1;
            for (int r=0; r<QISC_REG_COUNT; r++) {
                if (reg_free[r]) { assigned = r; break; }
            }
            if (assigned != -1) {
                intervals[i].reg = assigned; intervals[i].stack_slot = -1;
                reg_free[assigned] = false; active[num_active++] = i;
            } else {
                int spill_idx = -1; int max_end = -1;
                for (int j=0; j<num_active; j++) {
                    int idx = active[j];
                    if (intervals[idx].end > max_end) { max_end = intervals[idx].end; spill_idx = j; }
                }
                if (max_end > intervals[i].end) {
                    int s_idx = active[spill_idx];
                    intervals[i].reg = intervals[s_idx].reg; intervals[i].stack_slot = -1;
                    intervals[s_idx].stack_slot = next_stack_slot; next_stack_slot += 8;
                    active[spill_idx] = i;
                } else {
                    intervals[i].stack_slot = next_stack_slot; next_stack_slot += 8;
                }
            }
        }
        int frame_size = (next_stack_slot + frame_alloca + 15) & ~15;
        
        qisc_live_interval** id_to_inter = calloc(max_id + 1, sizeof(qisc_live_interval*));
        for(int i=0; i<num_intervals; i++) id_to_inter[intervals[i].inst_id] = &intervals[i];
        
        func_codes[num_func_codes].func = f;
        bytebuf_init(&func_codes[num_func_codes].buf);
        qisc_bytebuf* buf = &func_codes[num_func_codes].buf;
        num_func_codes++;
        
        emit_prologue(buf, frame_size);
        int current_alloca_offset = next_stack_slot;
        
        uint32_t block_offsets[1000] = {0};
        qisc_branch_patch patches[1000]; int num_patches = 0;
        int current_inst_index = 0;
        
        for(qisc_ir_block* b = f->first_block; b; b = b->next) {
            if (b->id < 1000) block_offsets[b->id] = bytebuf_here(buf);
            for(qisc_ir_inst* i = b->first_inst; i; i = i->next) {
                current_inst_index++;
                qisc_live_interval* inter = id_to_inter[i->id];
                int dst_reg = (inter && inter->stack_slot != -1) ? 10 : (inter ? reg_to_id(inter->reg) : 0);
                
                if (i->opcode == QISC_OP_NOP) {
                    if (i->num_operands > 0) {
                        int src_reg = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (dst_reg != src_reg) {
                            emit_rex_op(buf, dst_reg, src_reg); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, src_reg);
                        }
                    }
                } else if (i->opcode == QISC_OP_ADD) {
                    if (i->type && i->type->kind == QISC_TYPE_FLOAT) {
                        int op0 = load_operand(buf, i->operands[0], id_to_inter, dst_reg, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        int op1 = load_operand(buf, i->operands[1], id_to_inter, (dst_reg == 1) ? 2 : 1, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (dst_reg != op0) { bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x10); emit_modrm(buf, 3, dst_reg, op0); }
                        bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x58); emit_modrm(buf, 3, dst_reg, op1);
                    } else {
                        int op0 = load_operand(buf, i->operands[0], id_to_inter, dst_reg, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (dst_reg != op0) { emit_rex_op(buf, dst_reg, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, op0); }
                        int op1 = load_operand(buf, i->operands[1], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        emit_rex_op(buf, dst_reg, op1); bytebuf_push(buf, 0x03); emit_modrm(buf, 3, dst_reg, op1);
                    }
                } else if (i->opcode == QISC_OP_SUB) {
                    if (i->type && i->type->kind == QISC_TYPE_FLOAT) {
                        int op0 = load_operand(buf, i->operands[0], id_to_inter, dst_reg, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        int op1 = load_operand(buf, i->operands[1], id_to_inter, (dst_reg == 1) ? 2 : 1, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (dst_reg != op0) { bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x10); emit_modrm(buf, 3, dst_reg, op0); }
                        bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x5C); emit_modrm(buf, 3, dst_reg, op1);
                    } else {
                        int op0 = load_operand(buf, i->operands[0], id_to_inter, dst_reg, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (dst_reg != op0) { emit_rex_op(buf, dst_reg, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, op0); }
                        int op1 = load_operand(buf, i->operands[1], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        emit_rex_op(buf, dst_reg, op1); bytebuf_push(buf, 0x2B); emit_modrm(buf, 3, dst_reg, op1);
                    }
                } else if (i->opcode == QISC_OP_MUL) {
                    if (i->type && i->type->kind == QISC_TYPE_FLOAT) {
                        int op0 = load_operand(buf, i->operands[0], id_to_inter, dst_reg, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        int op1 = load_operand(buf, i->operands[1], id_to_inter, (dst_reg == 1) ? 2 : 1, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (dst_reg != op0) { bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x10); emit_modrm(buf, 3, dst_reg, op0); }
                        bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x59); emit_modrm(buf, 3, dst_reg, op1);
                    } else {
                        int op0 = load_operand(buf, i->operands[0], id_to_inter, dst_reg, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (dst_reg != op0) { emit_rex_op(buf, dst_reg, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, op0); }
                        int op1 = load_operand(buf, i->operands[1], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        emit_rex_op(buf, dst_reg, op1); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0xAF); emit_modrm(buf, 3, dst_reg, op1);
                    }
                } else if (i->opcode == QISC_OP_DIV) {
                    if (i->type && i->type->kind == QISC_TYPE_FLOAT) {
                        int op0 = load_operand(buf, i->operands[0], id_to_inter, dst_reg, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        int op1 = load_operand(buf, i->operands[1], id_to_inter, (dst_reg == 1) ? 2 : 1, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (dst_reg != op0) { bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x10); emit_modrm(buf, 3, dst_reg, op0); }
                        bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x5E); emit_modrm(buf, 3, dst_reg, op1);
                    } else {
                        int rdx_spill_slot = -1;
                    for (int j = 0; j < num_active; j++) {
                        int act_idx = active[j];
                        if (intervals[act_idx].reg == QISC_REG_RDX && intervals[act_idx].end > current_inst_index - 1) {
                            rdx_spill_slot = next_stack_slot;
                            next_stack_slot += 8;
                            // mov [rbp-slot], rdx (RDX is reg 2)
                            emit_rex_op(buf, 2, 5); bytebuf_push(buf, 0x89); emit_modrm(buf, 2, 2, 5);
                            bytebuf_push_u32(buf, (uint32_t)(-rdx_spill_slot));
                            break;
                        }
                    }
                    int op0 = load_operand(buf, i->operands[0], id_to_inter, 0, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    if (op0 != 0) { emit_rex_op(buf, 0, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 0, op0); }
                    bytebuf_push(buf, 0x48); bytebuf_push(buf, 0x99); // cqo
                    int op1 = load_operand(buf, i->operands[1], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    emit_rex_op(buf, 0, op1); bytebuf_push(buf, 0xF7); emit_modrm(buf, 3, 7, op1);
                    if (rdx_spill_slot != -1) {
                        // mov rdx, [rbp-slot]
                        emit_rex_op(buf, 2, 5); bytebuf_push(buf, 0x8B); emit_modrm(buf, 2, 2, 5);
                        bytebuf_push_u32(buf, (uint32_t)(-rdx_spill_slot));
                    }
                    if (dst_reg != 0) { emit_rex_op(buf, dst_reg, 0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, 0); }
                }
                } else if (i->opcode == QISC_OP_CMP_EQ || i->opcode == QISC_OP_CMP_LT || i->opcode == QISC_OP_CMP_GT) {
                    int op0 = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    int op1 = load_operand(buf, i->operands[1], id_to_inter, 15, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    emit_rex_op(buf, op0, op1); bytebuf_push(buf, 0x3B); emit_modrm(buf, 3, op0, op1);
                    bytebuf_push(buf, 0x0F);
                    if (i->opcode == QISC_OP_CMP_EQ) bytebuf_push(buf, 0x94);
                    else if (i->opcode == QISC_OP_CMP_LT) bytebuf_push(buf, 0x9C);
                    else bytebuf_push(buf, 0x9F);
                    bytebuf_push(buf, 0xC0);
                    emit_rex_op(buf, dst_reg, 0); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0xB6); emit_modrm(buf, 3, dst_reg, 0);
                } else if (i->opcode == QISC_OP_LOAD) {
                    int src = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    uint32_t offset = 0; // Simple mock struct offset support
                    if (offset > 0) {
                        emit_rex_op(buf, dst_reg, src); bytebuf_push(buf, 0x8B); emit_modrm(buf, 2, dst_reg, src);
                        bytebuf_push_u32(buf, offset);
                    } else {
                        emit_rex_op(buf, dst_reg, src); bytebuf_push(buf, 0x8B); emit_modrm(buf, 0, dst_reg, src);
                        if ((src & 7) == 4 || (src & 7) == 5) bytebuf_push(buf, 0x24);
                    }
                } else if (i->opcode == QISC_OP_STORE) {
                    int dst_addr = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    int src_val = load_operand(buf, i->operands[1], id_to_inter, 15, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    emit_rex_op(buf, src_val, dst_addr); bytebuf_push(buf, 0x89); emit_modrm(buf, 0, src_val, dst_addr);
                    if ((dst_addr & 7) == 4 || (dst_addr & 7) == 5) bytebuf_push(buf, 0x24);
                } else if (i->opcode == QISC_OP_ALLOCA) {
                    emit_rex_op(buf, dst_reg, 5); bytebuf_push(buf, 0x8D); emit_modrm(buf, 2, dst_reg, 5);
                    bytebuf_push_u32(buf, (uint32_t)(-current_alloca_offset));
                    current_alloca_offset += 8;
                } else if (i->opcode == QISC_OP_CALL) {
                    int param_regs[] = {7, 6, 2, 1, 8, 9};
                    for (size_t arg=1; arg<i->num_operands; arg++) {
                        int arg_reg = load_operand(buf, i->operands[arg], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        int p_reg = param_regs[arg-1];
                        if (arg_reg != p_reg) { emit_rex_op(buf, p_reg, arg_reg); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, p_reg, arg_reg); }
                    }
                    bytebuf_push(buf, 0xE8);
                    relocs[num_relocs++] = (qisc_reloc){ bytebuf_here(buf), i->operands[0]->as.s_val, f };
                    bytebuf_push_u32(buf, 0);
                    if (dst_reg != 0) { emit_rex_op(buf, dst_reg, 0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, 0); }
                } else if (i->opcode == QISC_OP_RET) {
                    if (i->num_operands > 0) {
                        int op0 = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                        if (i->operands[0]->type && i->operands[0]->type->kind == QISC_TYPE_FLOAT) {
                            if (op0 != 0) { bytebuf_push(buf, 0xF2); bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x10); emit_modrm(buf, 3, 0, op0); }
                        } else {
                            if (op0 != 0) { emit_rex_op(buf, 0, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 0, op0); }
                        }
                    }
                    emit_epilogue(buf); bytebuf_push(buf, 0xC3);
                } else if (i->opcode == QISC_OP_BR) {
                    if (b->num_successors > 0 && b->successors[0] != b->next) {
                        bytebuf_push(buf, 0xE9);
                        patches[num_patches++] = (qisc_branch_patch){bytebuf_here(buf), b->successors[0]->id};
                        bytebuf_push_u32(buf, 0);
                    }
                } else if (i->opcode == QISC_OP_BR_COND) {
                    int cond = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    emit_rex_op(buf, cond, cond); bytebuf_push(buf, 0x85); emit_modrm(buf, 3, cond, cond);
                    if (b->num_successors > 0 && b->successors[0] != b->next) {
                        bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x85);
                        patches[num_patches++] = (qisc_branch_patch){bytebuf_here(buf), b->successors[0]->id};
                        bytebuf_push_u32(buf, 0);
                    }
                    if (b->num_successors > 1 && b->successors[1] != b->next) {
                        bytebuf_push(buf, 0x0F); bytebuf_push(buf, 0x84);
                        patches[num_patches++] = (qisc_branch_patch){bytebuf_here(buf), b->successors[1]->id};
                        bytebuf_push_u32(buf, 0);
                    }
                } else if (i->opcode == QISC_OP_AWAIT_DATA) {
                    emit_rex_op(buf, dst_reg, 14); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, 14);
                } else if (i->opcode == QISC_OP_EMIT) {
                    int op0 = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    if (op0 != 0) { emit_rex_op(buf, 0, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 0, op0); }
                } else if (i->opcode == QISC_OP_TRY) {
                    bytebuf_push(buf, 0xE8);
                    relocs[num_relocs++] = (qisc_reloc){ bytebuf_here(buf), "__qisc_try_begin", f };
                    bytebuf_push_u32(buf, 0);
                    current_alloca_offset += 8;
                } else if (i->opcode == QISC_OP_CATCH) {
                    bytebuf_push(buf, 0xE8);
                    relocs[num_relocs++] = (qisc_reloc){ bytebuf_here(buf), "__qisc_try_catch", f };
                    bytebuf_push_u32(buf, 0);
                    if (dst_reg != 0) { emit_rex_op(buf, dst_reg, 0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, 0); }
                } else if (i->opcode == QISC_OP_FAIL) {
                    int op0 = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    if (op0 != 7) { emit_rex_op(buf, 7, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 7, op0); }
                    bytebuf_push(buf, 0xE8);
                    relocs[num_relocs++] = (qisc_reloc){ bytebuf_here(buf), "__qisc_fail", f };
                    bytebuf_push_u32(buf, 0);
                } else if (i->opcode == QISC_OP_STREAM_RANGE) {
                    int op0 = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    emit_rex_op(buf, 12, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 12, op0);
                    int op1 = load_operand(buf, i->operands[1], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    emit_rex_op(buf, op1, 12); bytebuf_push(buf, 0x89); emit_modrm(buf, 2, op1, 12); bytebuf_push(buf, 8);
                    int op2 = load_operand(buf, i->operands[2], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    emit_rex_op(buf, op2, 12); bytebuf_push(buf, 0x89); emit_modrm(buf, 2, op2, 12); bytebuf_push(buf, 16);
                } else if (i->opcode == QISC_OP_STREAM_MAP || i->opcode == QISC_OP_STREAM_FILTER || i->opcode == QISC_OP_STREAM_REDUCE) {
                    int op0 = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    if (op0 != 6) { emit_rex_op(buf, 6, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 6, op0); }
                    emit_rex_op(buf, 7, 12); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 7, 12);
                    bytebuf_push(buf, 0xE8);
                    if (i->opcode == QISC_OP_STREAM_MAP) relocs[num_relocs++] = (qisc_reloc){ bytebuf_here(buf), "__qisc_stream_map", f };
                    else if (i->opcode == QISC_OP_STREAM_FILTER) relocs[num_relocs++] = (qisc_reloc){ bytebuf_here(buf), "__qisc_stream_filter", f };
                    else relocs[num_relocs++] = (qisc_reloc){ bytebuf_here(buf), "__qisc_stream_reduce", f };
                    bytebuf_push_u32(buf, 0);
                } else if (i->opcode == QISC_OP_PIPELINE) {
                    int op0 = load_operand(buf, i->operands[0], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    if (op0 != 7) { emit_rex_op(buf, 7, op0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 7, op0); }
                    int op1 = load_operand(buf, i->operands[1], id_to_inter, 11, &rodata_buffer, r_relocs, &num_r_relocs, f);
                    if (op1 != 6) { emit_rex_op(buf, 6, op1); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, 6, op1); }
                    bytebuf_push(buf, 0xE8);
                    relocs[num_relocs++] = (qisc_reloc){ bytebuf_here(buf), "__qisc_pipeline", f };
                    bytebuf_push_u32(buf, 0);
                    if (dst_reg != 0) { emit_rex_op(buf, dst_reg, 0); bytebuf_push(buf, 0x8B); emit_modrm(buf, 3, dst_reg, 0); }
                }
                
                if (inter && inter->stack_slot != -1) {
                    emit_rex_op(buf, 10, 5); bytebuf_push(buf, 0x89); emit_modrm(buf, 2, 10 & 7, 5);
                    bytebuf_push_u32(buf, (uint32_t)(-inter->stack_slot));
                }
            }
        }
        
        for (int i=0; i<num_patches; i++) {
            uint32_t target_offset = 0;
            if (patches[i].target_block_id < 1000) target_offset = block_offsets[patches[i].target_block_id];
            uint32_t patch_site = patches[i].patch_site;
            uint32_t relative = target_offset - (patch_site + 4);
            bytebuf_patch_u32(buf, patch_site, relative);
        }
        
        free(id_to_inter); free(intervals);
    }
    
    size_t hot_code_size = 0, cold_code_size = 0;
    for (int i=0; i<num_func_codes; i++) {
        if (func_codes[i].func->profile.is_hot) hot_code_size += func_codes[i].buf.size;
        else cold_code_size += func_codes[i].buf.size;
    }
    
    size_t hot_offset = 64;
    size_t cold_offset = hot_offset + hot_code_size;
    size_t rodata_offset = cold_offset + cold_code_size;
    size_t symtab_offset = rodata_offset + rodata_buffer.size;
    size_t symtab_size = (1 + num_func_codes + 8) * sizeof(Elf64_Sym);
    size_t strtab_offset = symtab_offset + symtab_size;
    size_t strtab_size = 1;
    for (int i=0; i<num_func_codes; i++) strtab_size += strlen(func_codes[i].func->name) + 1;
    strtab_size += strlen(".rodata") + 1;
    strtab_size += strlen("__qisc_try_begin") + 1;
    strtab_size += strlen("__qisc_try_catch") + 1;
    strtab_size += strlen("__qisc_fail") + 1;
    strtab_size += strlen("__qisc_stream_map") + 1;
    strtab_size += strlen("__qisc_stream_filter") + 1;
    strtab_size += strlen("__qisc_stream_reduce") + 1;
    strtab_size += strlen("__qisc_pipeline") + 1;
    
    size_t shstrtab_offset = strtab_offset + strtab_size;
    const char* shstrtab = "\0.text.hot\0.text.cold\0.rodata\0.symtab\0.strtab\0.shstrtab\0.rela.text\0";
    size_t shstrtab_size = 67; // exact bytes
    size_t rela_offset = shstrtab_offset + shstrtab_size;
    size_t rela_size = (num_relocs + num_r_relocs) * sizeof(Elf64_Rela);
    size_t shdr_offset = rela_offset + rela_size;
    shdr_offset = (shdr_offset + 7) & ~7;
    
    Elf64_Ehdr ehdr; memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = 0x7F; ehdr.e_ident[1] = 'E'; ehdr.e_ident[2] = 'L'; ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; ehdr.e_ident[5] = 1; ehdr.e_ident[6] = 1;
    ehdr.e_type = 1; ehdr.e_machine = 62; ehdr.e_version = 1;
    ehdr.e_shoff = shdr_offset; ehdr.e_ehsize = 64; ehdr.e_shentsize = 64;
    ehdr.e_shnum = 8; ehdr.e_shstrndx = 6;
    
    FILE* f = fopen(filepath, "wb");
    fwrite(&ehdr, sizeof(ehdr), 1, f);
    
    for (int i=0; i<num_func_codes; i++) {
        if (func_codes[i].func->profile.is_hot) fwrite(func_codes[i].buf.data, 1, func_codes[i].buf.size, f);
    }
    for (int i=0; i<num_func_codes; i++) {
        if (!func_codes[i].func->profile.is_hot) fwrite(func_codes[i].buf.data, 1, func_codes[i].buf.size, f);
    }
    fwrite(rodata_buffer.data, 1, rodata_buffer.size, f);
    
    Elf64_Sym sym_zero; memset(&sym_zero, 0, sizeof(sym_zero));
    fwrite(&sym_zero, sizeof(sym_zero), 1, f);
    size_t name_off = 1, current_hot = 0, current_cold = 0;
    
    Elf64_Sym ext_syms[8]; memset(ext_syms, 0, sizeof(ext_syms));
    for (int i=0; i<8; i++) {
        if (i==0) {
            ext_syms[i].st_name = name_off; ext_syms[i].st_info = 0x01; // STT_SECTION
            ext_syms[i].st_shndx = 3; // .rodata
            name_off += strlen(".rodata") + 1;
        } else {
            ext_syms[i].st_name = name_off; ext_syms[i].st_info = 0x10;
            if (i==1) name_off += strlen("__qisc_try_begin") + 1;
            if (i==2) name_off += strlen("__qisc_try_catch") + 1;
            if (i==3) name_off += strlen("__qisc_fail") + 1;
            if (i==4) name_off += strlen("__qisc_stream_map") + 1;
            if (i==5) name_off += strlen("__qisc_stream_filter") + 1;
            if (i==6) name_off += strlen("__qisc_stream_reduce") + 1;
            if (i==7) name_off += strlen("__qisc_pipeline") + 1;
        }
        fwrite(&ext_syms[i], sizeof(Elf64_Sym), 1, f);
    }
    
    for (int i=0; i<num_func_codes; i++) {
        Elf64_Sym sym; memset(&sym, 0, sizeof(sym));
        sym.st_name = name_off; sym.st_info = 0x12;
        sym.st_shndx = func_codes[i].func->profile.is_hot ? 1 : 2;
        sym.st_value = func_codes[i].func->profile.is_hot ? current_hot : current_cold;
        sym.st_size = func_codes[i].buf.size;
        fwrite(&sym, sizeof(sym), 1, f);
        name_off += strlen(func_codes[i].func->name) + 1;
        if (func_codes[i].func->profile.is_hot) current_hot += sym.st_size;
        else current_cold += sym.st_size;
    }
    
    char zero = 0; fwrite(&zero, 1, 1, f);
    fwrite(".rodata\0", 1, strlen(".rodata")+1, f);
    fwrite("__qisc_try_begin\0", 1, strlen("__qisc_try_begin")+1, f);
    fwrite("__qisc_try_catch\0", 1, strlen("__qisc_try_catch")+1, f);
    fwrite("__qisc_fail\0", 1, strlen("__qisc_fail")+1, f);
    fwrite("__qisc_stream_map\0", 1, strlen("__qisc_stream_map")+1, f);
    fwrite("__qisc_stream_filter\0", 1, strlen("__qisc_stream_filter")+1, f);
    fwrite("__qisc_stream_reduce\0", 1, strlen("__qisc_stream_reduce")+1, f);
    fwrite("__qisc_pipeline\0", 1, strlen("__qisc_pipeline")+1, f);
    for (int i=0; i<num_func_codes; i++) fwrite(func_codes[i].func->name, 1, strlen(func_codes[i].func->name) + 1, f);
    
    fwrite(shstrtab, 1, shstrtab_size, f);
    
    for (int i=0; i<num_relocs; i++) {
        Elf64_Rela r; memset(&r, 0, sizeof(r));
        int sym_idx = 0;
        if (strcmp("__qisc_try_begin", relocs[i].symbol_name) == 0) sym_idx = 2;
        else if (strcmp("__qisc_try_catch", relocs[i].symbol_name) == 0) sym_idx = 3;
        else if (strcmp("__qisc_fail", relocs[i].symbol_name) == 0) sym_idx = 4;
        else if (strcmp("__qisc_stream_map", relocs[i].symbol_name) == 0) sym_idx = 5;
        else if (strcmp("__qisc_stream_filter", relocs[i].symbol_name) == 0) sym_idx = 6;
        else if (strcmp("__qisc_stream_reduce", relocs[i].symbol_name) == 0) sym_idx = 7;
        else if (strcmp("__qisc_pipeline", relocs[i].symbol_name) == 0) sym_idx = 8;
        else {
            for (int j=0; j<num_func_codes; j++) {
                if (strcmp(func_codes[j].func->name, relocs[i].symbol_name) == 0) { sym_idx = j + 9; break; }
            }
        }
        size_t func_base = 0;
        for (int j=0; j<num_func_codes; j++) {
            if (func_codes[j].func == relocs[i].caller) break;
            if (func_codes[j].func->profile.is_hot == relocs[i].caller->profile.is_hot) func_base += func_codes[j].buf.size;
        }
        r.r_offset = func_base + relocs[i].offset;
        r.r_info = ELF64_R_INFO(sym_idx, 4); // R_X86_64_PLT32
        r.r_addend = -4;
        fwrite(&r, sizeof(r), 1, f);
    }
    
    for (int i=0; i<num_r_relocs; i++) {
        Elf64_Rela r; memset(&r, 0, sizeof(r));
        size_t func_base = 0;
        for (int j=0; j<num_func_codes; j++) {
            if (func_codes[j].func == r_relocs[i].caller) break;
            if (func_codes[j].func->profile.is_hot == r_relocs[i].caller->profile.is_hot) func_base += func_codes[j].buf.size;
        }
        r.r_offset = func_base + r_relocs[i].offset;
        r.r_info = ELF64_R_INFO(1, 2); // 1 is .rodata section sym, 2 is R_X86_64_PC32
        r.r_addend = r_relocs[i].rodata_offset - 4;
        fwrite(&r, sizeof(r), 1, f);
    }
    
    size_t pad = shdr_offset - ftell(f);
    while (pad--) fwrite(&zero, 1, 1, f);
    
    Elf64_Shdr shdrs[8]; memset(shdrs, 0, sizeof(shdrs));
    shdrs[1].sh_name = 1; shdrs[1].sh_type = 1; shdrs[1].sh_flags = 6; shdrs[1].sh_offset = hot_offset; shdrs[1].sh_size = hot_code_size;
    shdrs[2].sh_name = 11; shdrs[2].sh_type = 1; shdrs[2].sh_flags = 6; shdrs[2].sh_offset = cold_offset; shdrs[2].sh_size = cold_code_size;
    shdrs[3].sh_name = 22; shdrs[3].sh_type = 1; shdrs[3].sh_flags = 2; shdrs[3].sh_offset = rodata_offset; shdrs[3].sh_size = rodata_buffer.size; // .rodata
    shdrs[4].sh_name = 30; shdrs[4].sh_type = 2; shdrs[4].sh_offset = symtab_offset; shdrs[4].sh_size = symtab_size; shdrs[4].sh_link = 5; shdrs[4].sh_info = 9; shdrs[4].sh_entsize = 24;
    shdrs[5].sh_name = 38; shdrs[5].sh_type = 3; shdrs[5].sh_offset = strtab_offset; shdrs[5].sh_size = strtab_size;
    shdrs[6].sh_name = 46; shdrs[6].sh_type = 3; shdrs[6].sh_offset = shstrtab_offset; shdrs[6].sh_size = shstrtab_size;
    shdrs[7].sh_name = 56; shdrs[7].sh_type = 4; shdrs[7].sh_offset = rela_offset; shdrs[7].sh_size = rela_size; shdrs[7].sh_link = 4; shdrs[7].sh_info = 1; shdrs[7].sh_entsize = 24;
    
    fwrite(shdrs, sizeof(Elf64_Shdr), 8, f);
    fclose(f);
    
    for (int i=0; i<num_func_codes; i++) bytebuf_free(&func_codes[i].buf);
    bytebuf_free(&rodata_buffer);
    return true;
}
