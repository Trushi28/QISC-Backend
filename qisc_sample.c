#define _POSIX_C_SOURCE 200809L
#include "qisc_ir.h"
#include "qisc_opt.h"
#include "qisc_codegen.h"
#include "qisc_convergence.h"
#include "qisc_living_component.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

int main(void) {
    qisc_ir_module* mod = qisc_ir_create_module();
    qisc_type* t_int = qisc_type_int();
    
    // compute_value() -> 5
    qisc_ir_function* f_cv = qisc_ir_create_function(mod, "compute_value", qisc_type_proc(t_int, NULL, 0), true);
    f_cv->profile.execution_count = 1000;
    qisc_ir_block* b_cv = qisc_ir_create_block(f_cv, "entry", 1.0);
    qisc_value* const_5 = qisc_value_int(5);
    qisc_value* ret_cv_ops[] = { const_5 };
    qisc_ir_emit_inst(b_cv, QISC_OP_RET, t_int, ret_cv_ops, 1);

    // add_two(a, b) -> a + b
    qisc_type* params2[] = { t_int, t_int };
    qisc_ir_function* f_add2 = qisc_ir_create_function(mod, "add_two", qisc_type_proc(t_int, params2, 2), true);
    f_add2->profile.execution_count = 1000;
    qisc_ir_block* b_add2 = qisc_ir_create_block(f_add2, "entry", 1.0);
    qisc_value* p0 = qisc_value_param(t_int, 0);
    qisc_value* p1 = qisc_value_param(t_int, 1);
    qisc_value* add_ops[] = { p0, p1 };
    qisc_ir_inst* i_add = qisc_ir_emit_inst(b_add2, QISC_OP_ADD, t_int, add_ops, 2);
    qisc_value* ret_add_ops[] = { qisc_value_inst(i_add) };
    qisc_ir_emit_inst(b_add2, QISC_OP_RET, t_int, ret_add_ops, 1);
    
    // factorial(n) -> n!
    qisc_type* params1[] = { t_int };
    qisc_ir_function* f_fact = qisc_ir_create_function(mod, "factorial", qisc_type_proc(t_int, params1, 1), true);
    f_fact->profile.execution_count = 1000;
    qisc_ir_block* b_fact_entry = qisc_ir_create_block(f_fact, "entry", 1.0);
    qisc_ir_block* b_fact_base = qisc_ir_create_block(f_fact, "base_block", 0.5);
    qisc_ir_block* b_fact_rec = qisc_ir_create_block(f_fact, "recurse_block", 0.5);
    
    b_fact_entry->successors = malloc(2 * sizeof(qisc_ir_block*));
    b_fact_entry->num_successors = 2;
    b_fact_entry->successors[0] = b_fact_base;
    b_fact_entry->successors[1] = b_fact_rec;
    
    qisc_value* p_n = qisc_value_param(t_int, 0);
    qisc_value* const_2 = qisc_value_int(2);
    qisc_value* cmp_ops[] = { p_n, const_2 };
    qisc_ir_inst* i_cmp = qisc_ir_emit_inst(b_fact_entry, QISC_OP_CMP_LT, t_int, cmp_ops, 2);
    qisc_value* br_cond_ops[] = { qisc_value_inst(i_cmp) };
    qisc_ir_emit_inst(b_fact_entry, QISC_OP_BR_COND, qisc_type_int(), br_cond_ops, 1);
    
    // Add a NOP to be eliminated by DCE to force at least 1 mutation!
    qisc_value* nops_ops[] = { const_2 };
    qisc_ir_emit_inst(b_fact_entry, QISC_OP_NOP, t_int, nops_ops, 1);
    
    qisc_value* const_1 = qisc_value_int(1);
    qisc_value* ret_base_ops[] = { const_1 };
    qisc_ir_emit_inst(b_fact_base, QISC_OP_RET, t_int, ret_base_ops, 1);
    
    qisc_value* sub_ops[] = { p_n, const_1 };
    qisc_ir_inst* i_sub = qisc_ir_emit_inst(b_fact_rec, QISC_OP_SUB, t_int, sub_ops, 2);
    
    qisc_value* func_name_val = calloc(1, sizeof(qisc_value));
    func_name_val->kind = QISC_VAL_CONST_STRING;
    func_name_val->as.s_val = "factorial";
    qisc_value* call_ops[] = { func_name_val, qisc_value_inst(i_sub) };
    qisc_ir_inst* i_call = qisc_ir_emit_inst(b_fact_rec, QISC_OP_CALL, t_int, call_ops, 2);
    
    qisc_value* mul_ops[] = { p_n, qisc_value_inst(i_call) };
    qisc_ir_inst* i_mul = qisc_ir_emit_inst(b_fact_rec, QISC_OP_MUL, t_int, mul_ops, 2);
    
    qisc_value* ret_rec_ops[] = { qisc_value_inst(i_mul) };
    qisc_ir_emit_inst(b_fact_rec, QISC_OP_RET, t_int, ret_rec_ops, 1);
    
    qisc_convergence_state* conv = qisc_convergence_create(10, "profile.dat", "ir_cache.dat");
    if (!qisc_convergence_run_to_completion(conv, mod)) {
        return 1;
    }
    qisc_convergence_print_report(conv);
    
    qisc_codegen_emit_elf(mod, "output.o");
    
    FILE* f = fopen("test_harness.c", "w");
    fprintf(f, "#include <stdio.h>\n#include <stdint.h>\nextern int64_t compute_value(void);\nextern int64_t add_two(int64_t a, int64_t b);\nextern int64_t factorial(int64_t n);\nint main(void) {\nint64_t r1 = compute_value();\nint64_t r2 = add_two(3, 4);\nint64_t r3 = factorial(5);\nprintf(\"compute_value = %%lld (expected 5)\\n\", (long long)r1);\nprintf(\"add_two(3,4)  = %%lld (expected 7)\\n\", (long long)r2);\nprintf(\"factorial(5)  = %%lld (expected 120)\\n\", (long long)r3);\nif(r1==5 && r2==7 && r3==120) { printf(\"[PASS] compute_value = 5\\n[PASS] add_two(3,4) = 7\\n[PASS] factorial(5) = 120\\nALL TESTS PASSED\\n\"); return 0; } else { printf(\"FAIL\\n\"); return 1; }\n}\n");
    fclose(f);
    
    int ret = system("gcc test_harness.c output.o -o test_run && ./test_run");
    return ret == 0 ? 0 : 1;
}