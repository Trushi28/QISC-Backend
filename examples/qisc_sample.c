#define _POSIX_C_SOURCE 200809L
#include "qisc_ir.h"
#include "qisc_cfg.h"
#include "qisc_ssa.h"
#include "qisc_opt.h"
#include "qisc_codegen.h"
#include "qisc_convergence.h"
#include "qisc_living_component.h"
#include "qisc_backend_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void comp_double_native(qisc_component* comp, void* input_data, size_t input_data_size) {
    if (!comp || !input_data || input_data_size != sizeof(int64_t)) return;
    int64_t input = 0;
    memcpy(&input, input_data, sizeof(input));
    int64_t* output = (int64_t*)malloc(sizeof(int64_t));
    if (!output) return;
    *output = input * 2;
    comp->output_data = output;
    comp->output_data_size = sizeof(*output);
}

static uint64_t file_hash(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    uint64_t hash = 14695981039346656037ULL;
    int ch = 0;
    while ((ch = fgetc(f)) != EOF) {
        hash ^= (uint8_t)ch;
        hash *= 1099511628211ULL;
    }
    fclose(f);
    return hash;
}

static uint64_t build_direct_add_two_object_hash(const char* path) {
    qisc_ir_module* mod = qisc_ir_create_module();
    qisc_type* t_int = qisc_type_int();
    qisc_type* params[] = { t_int, t_int };
    qisc_ir_function* func = qisc_ir_create_function(mod, "add_two", qisc_type_proc(t_int, params, 2), true);
    func->profile.execution_count = 1000;
    qisc_ir_block* block = qisc_ir_create_block(func, "entry", 1.0);
    qisc_value* p0_ops[] = { qisc_value_param(t_int, 0) };
    qisc_ir_inst* p0 = qisc_ir_emit_inst(block, QISC_OP_NOP, t_int, p0_ops, 1);
    qisc_value* p1_ops[] = { qisc_value_param(t_int, 1) };
    qisc_ir_inst* p1 = qisc_ir_emit_inst(block, QISC_OP_NOP, t_int, p1_ops, 1);
    qisc_value* add_ops[] = { qisc_value_inst(p0), qisc_value_inst(p1) };
    qisc_ir_inst* add = qisc_ir_emit_inst(block, QISC_OP_ADD, t_int, add_ops, 2);
    qisc_value* ret_ops[] = { qisc_value_inst(add) };
    qisc_ir_emit_inst(block, QISC_OP_RET, t_int, ret_ops, 1);
    qisc_codegen_emit_elf(mod, path);
    uint64_t hash = file_hash(path);
    qisc_ir_destroy_module(mod);
    return hash;
}

int main(void) {
    setbuf(stdout, NULL);
    mkdir("build", 0777);
    qisc_ir_module* mod = qisc_ir_create_module();
    qisc_type* t_int = qisc_type_int();
    qisc_type* t_float = qisc_type_float();
    qisc_type* t_string = (qisc_type*)calloc(1, sizeof(qisc_type));
    t_string->kind = QISC_TYPE_STRING;
    
    // 1. compute_value
    qisc_ir_function* f_cv = qisc_ir_create_function(mod, "compute_value", qisc_type_proc(t_int, NULL, 0), true);
    f_cv->profile.execution_count = 1000;
    qisc_ir_block* b_cv = qisc_ir_create_block(f_cv, "entry", 1.0);
    qisc_value* ret_cv_ops[] = { qisc_value_int(5) };
    qisc_ir_emit_inst(b_cv, QISC_OP_RET, t_int, ret_cv_ops, 1);

    // 2. add_two
    qisc_type* params2[] = { t_int, t_int };
    qisc_ir_function* f_add2 = qisc_ir_create_function(mod, "add_two", qisc_type_proc(t_int, params2, 2), true);
    f_add2->profile.execution_count = 1000;
    qisc_ir_block* b_add2 = qisc_ir_create_block(f_add2, "entry", 1.0);
    qisc_value* add_ops[] = { qisc_value_param(t_int, 0), qisc_value_param(t_int, 1) };
    qisc_ir_inst* i_add = qisc_ir_emit_inst(b_add2, QISC_OP_ADD, t_int, add_ops, 2);
    qisc_value* ret_add_ops[] = { qisc_value_inst(i_add) };
    qisc_ir_emit_inst(b_add2, QISC_OP_RET, t_int, ret_add_ops, 1);
    
    // 3. factorial
    qisc_type* params1[] = { t_int };
    qisc_ir_function* f_fact = qisc_ir_create_function(mod, "factorial", qisc_type_proc(t_int, params1, 1), true);
    f_fact->profile.execution_count = 1000;
    qisc_ir_block* b_fact_entry = qisc_ir_create_block(f_fact, "entry", 1.0);
    qisc_ir_block* b_fact_base = qisc_ir_create_block(f_fact, "base_block", 0.5);
    qisc_ir_block* b_fact_rec = qisc_ir_create_block(f_fact, "recurse_block", 0.5);
    b_fact_entry->successors = malloc(2 * sizeof(qisc_ir_block*));
    b_fact_entry->num_successors = 2;
    b_fact_entry->successors[0] = b_fact_base; b_fact_entry->successors[1] = b_fact_rec;
    
    qisc_value* nop_ops_pn[] = { qisc_value_param(t_int, 0) };
    qisc_ir_inst* i_p_n = qisc_ir_emit_inst(b_fact_entry, QISC_OP_NOP, t_int, nop_ops_pn, 1);
    
    qisc_value* cmp_ops[] = { qisc_value_inst(i_p_n), qisc_value_int(2) };
    qisc_ir_inst* i_cmp = qisc_ir_emit_inst(b_fact_entry, QISC_OP_CMP_LT, t_int, cmp_ops, 2);
    qisc_value* br_cond_ops[] = { qisc_value_inst(i_cmp) };
    qisc_ir_emit_inst(b_fact_entry, QISC_OP_BR_COND, qisc_type_int(), br_cond_ops, 1);
    
    qisc_value* ret_base_ops[] = { qisc_value_int(1) };
    qisc_ir_emit_inst(b_fact_base, QISC_OP_RET, t_int, ret_base_ops, 1);
    
    qisc_value* sub_ops[] = { qisc_value_inst(i_p_n), qisc_value_int(1) };
    qisc_ir_inst* i_sub = qisc_ir_emit_inst(b_fact_rec, QISC_OP_SUB, t_int, sub_ops, 2);
    qisc_value* call_ops[] = { qisc_value_string("factorial"), qisc_value_inst(i_sub) };
    qisc_ir_inst* i_call = qisc_ir_emit_inst(b_fact_rec, QISC_OP_CALL, t_int, call_ops, 2);
    qisc_value* mul_ops[] = { qisc_value_inst(i_p_n), qisc_value_inst(i_call) };
    qisc_ir_inst* i_mul = qisc_ir_emit_inst(b_fact_rec, QISC_OP_MUL, t_int, mul_ops, 2);
    qisc_value* ret_rec_ops[] = { qisc_value_inst(i_mul) };
    qisc_ir_emit_inst(b_fact_rec, QISC_OP_RET, t_int, ret_rec_ops, 1);
    
    // 4. sub_test
    qisc_ir_function* f_sub = qisc_ir_create_function(mod, "sub_test", qisc_type_proc(t_int, NULL, 0), true);
    qisc_ir_block* b_sub = qisc_ir_create_block(f_sub, "entry", 1.0);
    qisc_value* sub2_ops[] = { qisc_value_int(10), qisc_value_int(3) };
    qisc_ir_inst* i_sub2 = qisc_ir_emit_inst(b_sub, QISC_OP_SUB, t_int, sub2_ops, 2);
    qisc_value* ret_sub_ops[] = { qisc_value_inst(i_sub2) };
    qisc_ir_emit_inst(b_sub, QISC_OP_RET, t_int, ret_sub_ops, 1);
    
    // 5. mul_test
    qisc_ir_function* f_mul = qisc_ir_create_function(mod, "mul_test", qisc_type_proc(t_int, NULL, 0), true);
    qisc_ir_block* b_mul = qisc_ir_create_block(f_mul, "entry", 1.0);
    qisc_value* mul2_ops[] = { qisc_value_int(6), qisc_value_int(7) };
    qisc_ir_inst* i_mul2 = qisc_ir_emit_inst(b_mul, QISC_OP_MUL, t_int, mul2_ops, 2);
    qisc_value* ret_mul_ops[] = { qisc_value_inst(i_mul2) };
    qisc_ir_emit_inst(b_mul, QISC_OP_RET, t_int, ret_mul_ops, 1);
    
    // 6. div_test
    qisc_ir_function* f_div = qisc_ir_create_function(mod, "div_test", qisc_type_proc(t_int, NULL, 0), true);
    qisc_ir_block* b_div = qisc_ir_create_block(f_div, "entry", 1.0);
    qisc_value* div_ops[] = { qisc_value_int(100), qisc_value_int(4) };
    qisc_ir_inst* i_div = qisc_ir_emit_inst(b_div, QISC_OP_DIV, t_int, div_ops, 2);
    qisc_value* ret_div_ops[] = { qisc_value_inst(i_div) };
    qisc_ir_emit_inst(b_div, QISC_OP_RET, t_int, ret_div_ops, 1);
    
    // 7. cmp_test
    qisc_ir_function* f_cmp = qisc_ir_create_function(mod, "cmp_test", qisc_type_proc(t_int, NULL, 0), true);
    qisc_ir_block* b_cmp = qisc_ir_create_block(f_cmp, "entry", 1.0);
    qisc_value* cmp2_ops[] = { qisc_value_int(3), qisc_value_int(5) };
    qisc_ir_inst* i_cmp2 = qisc_ir_emit_inst(b_cmp, QISC_OP_CMP_LT, t_int, cmp2_ops, 2);
    qisc_value* ret_cmp_ops[] = { qisc_value_inst(i_cmp2) };
    qisc_ir_emit_inst(b_cmp, QISC_OP_RET, t_int, ret_cmp_ops, 1);
    
    // 8. float_test
    qisc_ir_function* f_flt = qisc_ir_create_function(mod, "float_test", qisc_type_proc(t_float, NULL, 0), true);
    qisc_ir_block* b_flt = qisc_ir_create_block(f_flt, "entry", 1.0);
    qisc_value* flt_ops[] = { qisc_value_float(2.5), qisc_value_float(2.0) };
    qisc_ir_inst* i_flt = qisc_ir_emit_inst(b_flt, QISC_OP_MUL, t_float, flt_ops, 2);
    qisc_value* ret_flt_ops[] = { qisc_value_inst(i_flt) };
    qisc_ir_emit_inst(b_flt, QISC_OP_RET, t_float, ret_flt_ops, 1);
    
    // 9. string_test
    qisc_ir_function* f_str = qisc_ir_create_function(mod, "string_test", qisc_type_proc(t_string, NULL, 0), true);
    qisc_ir_block* b_str = qisc_ir_create_block(f_str, "entry", 1.0);
    qisc_value* str_ops[] = { qisc_value_string("Hello QISC") };
    qisc_ir_inst* i_str = qisc_ir_emit_inst(b_str, QISC_OP_NOP, t_string, str_ops, 1);
    qisc_value* ret_str_ops[] = { qisc_value_inst(i_str) };
    qisc_ir_emit_inst(b_str, QISC_OP_RET, t_string, ret_str_ops, 1);

    // Section 1 — TAC validation
    char error_buf[1024];
    if (qisc_ir_validate_module(mod, error_buf, sizeof(error_buf))) {
        printf("[PASS] IR validation\n");
    } else {
        printf("[FAIL] IR validation: %s\n", error_buf);
        return 1;
    }
    // qisc_ir_print_module(mod); // Commented to reduce noise

    // Section 2 — CFG construction and printing
    qisc_cfg* cfg = qisc_cfg_build(f_fact);
    qisc_cfg_compute_dominators(cfg);
    qisc_cfg_compute_dominance_frontier(cfg);
    // qisc_cfg_print(cfg); // Commented to reduce noise
    printf("[PASS] CFG built\n");
    qisc_cfg_destroy(cfg);

    // Section 3 — Pass pipeline
    qisc_pass_pipeline* p = qisc_pipeline_create();
    uint64_t cycle = 1;
    bool changed = true;
    while (changed) {
        changed = qisc_pipeline_run_once(p, mod, cycle);
        cycle++;
        if (cycle > 100) break;
    }
    if (cycle > 2) printf("[PASS] convergence in >= 2 cycles\n");
    else printf("[FAIL] convergence too fast\n");
    qisc_pipeline_print_stats(p);
    qisc_pipeline_destroy(p);

    qisc_ir_function* f_cond = qisc_ir_create_function(mod, "conditional_value", qisc_type_proc(t_int, params1, 1), true);
    qisc_ir_block* b_cond_entry = qisc_ir_create_block(f_cond, "entry", 1.0);
    qisc_ir_block* b_cond_true = qisc_ir_create_block(f_cond, "true_block", 0.5);
    qisc_ir_block* b_cond_false = qisc_ir_create_block(f_cond, "false_block", 0.5);
    qisc_ir_block* b_cond_merge = qisc_ir_create_block(f_cond, "merge_block", 1.0);
    b_cond_entry->successors = malloc(2 * sizeof(qisc_ir_block*));
    b_cond_entry->num_successors = 2;
    b_cond_entry->successors[0] = b_cond_true;
    b_cond_entry->successors[1] = b_cond_false;
    b_cond_true->successors = malloc(sizeof(qisc_ir_block*));
    b_cond_true->num_successors = 1;
    b_cond_true->successors[0] = b_cond_merge;
    b_cond_false->successors = malloc(sizeof(qisc_ir_block*));
    b_cond_false->num_successors = 1;
    b_cond_false->successors[0] = b_cond_merge;

    qisc_value* cond_param_ops[] = { qisc_value_param(t_int, 0) };
    qisc_ir_inst* i_cond_param = qisc_ir_emit_inst(b_cond_entry, QISC_OP_NOP, t_int, cond_param_ops, 1);
    qisc_value* cond_cmp_ops[] = { qisc_value_inst(i_cond_param), qisc_value_int(0) };
    qisc_ir_inst* i_cond_cmp = qisc_ir_emit_inst(b_cond_entry, QISC_OP_CMP_EQ, t_int, cond_cmp_ops, 2);
    qisc_value* cond_br_ops[] = { qisc_value_inst(i_cond_cmp) };
    qisc_ir_emit_inst(b_cond_entry, QISC_OP_BR_COND, qisc_type_int(), cond_br_ops, 1);
    qisc_value* true_val_ops[] = { qisc_value_int(5) };
    qisc_ir_inst* i_cond_true_val = qisc_ir_emit_inst(b_cond_true, QISC_OP_NOP, t_int, true_val_ops, 1);
    qisc_ir_emit_inst(b_cond_true, QISC_OP_BR, qisc_type_int(), NULL, 0);
    qisc_value* false_val_ops[] = { qisc_value_int(10) };
    qisc_ir_inst* i_cond_false_val = qisc_ir_emit_inst(b_cond_false, QISC_OP_NOP, t_int, false_val_ops, 1);
    i_cond_false_val->id = i_cond_true_val->id;
    qisc_ir_emit_inst(b_cond_false, QISC_OP_BR, qisc_type_int(), NULL, 0);
    qisc_value* cond_ret_ops[] = { qisc_value_inst(i_cond_true_val) };
    qisc_ir_emit_inst(b_cond_merge, QISC_OP_RET, t_int, cond_ret_ops, 1);

    // Section 4 — SSA round-trip
    qisc_ir_compute_hash(mod);
    qisc_ssa_construct(mod);
    int phi_count = 0;
    for (qisc_ir_inst* i = b_cond_merge->first_inst; i; i = i->next) {
        if (i->opcode == QISC_OP_PHI) phi_count++;
    }
    printf("  PHI nodes in conditional_value: %d\n", phi_count);
    qisc_ir_compute_hash(mod);
    qisc_ssa_destruct(mod);
    int remaining_phis = 0;
    for (qisc_ir_block* b = f_cond->first_block; b; b = b->next) {
        for (qisc_ir_inst* i = b->first_inst; i; i = i->next) {
            if (i->opcode == QISC_OP_PHI) remaining_phis++;
        }
    }
    qisc_ir_compute_hash(mod);
    if (phi_count == 0) {
        printf("[FAIL] SSA: no PHI at join\n");
        return 1;
    }
    if (remaining_phis != 0) {
        printf("[FAIL] SSA: PHI survived destruct\n");
        return 1;
    }
    printf("[PASS] SSA round-trip with PHI\n");

    // Section 5 — Serialization round-trip
    qisc_ir_serialize(mod, "build/roundtrip.dat");
    qisc_ir_module* mod2 = qisc_ir_deserialize("build/roundtrip.dat");
    if (mod2 && qisc_ir_compute_hash(mod) == qisc_ir_compute_hash(mod2)) {
        printf("[PASS] serialization round-trip\n");
    } else {
        printf("[FAIL] serialization round-trip\n");
        return 1;
    }
    qisc_ir_destroy_module(mod2);
    FILE* rf = fopen("build/roundtrip.dat", "rb");
    if (!rf) {
        printf("[FAIL] serialization file missing\n");
        return 1;
    }
    fseek(rf, 0, SEEK_END);
    long sz = ftell(rf);
    fclose(rf);
    printf("  roundtrip.dat: %ld bytes\n", sz);
    if (sz < 200) {
        printf("[FAIL] serialization too small\n");
        return 1;
    }

    // Section 6 — Code generation and execution
    qisc_codegen_emit_elf(mod, "build/output.o");
    
    FILE* f = fopen("build/test_harness.c", "w");
    fprintf(f, "#include <stdio.h>\n#include <stdint.h>\n#include <string.h>\n");
    fprintf(f, "extern int64_t compute_value(void);\n");
    fprintf(f, "extern int64_t add_two(int64_t a, int64_t b);\n");
    fprintf(f, "extern int64_t factorial(int64_t n);\n");
    fprintf(f, "extern int64_t sub_test(void);\n");
    fprintf(f, "extern int64_t mul_test(void);\n");
    fprintf(f, "extern int64_t div_test(void);\n");
    fprintf(f, "extern int64_t cmp_test(void);\n");
    fprintf(f, "extern int64_t conditional_value(int64_t cond);\n");
    fprintf(f, "extern double float_test(void);\n");
    fprintf(f, "extern const char* string_test(void);\n");
    
    fprintf(f, "int main(void) {\n");
    fprintf(f, "int passed = 0;\n");
    fprintf(f, "if(compute_value() == 5) { passed++; printf(\"[PASS] compute_value = 5\\n\"); } else printf(\"[FAIL] compute_value\\n\");\n");
    fprintf(f, "if(add_two(3, 4) == 7) { passed++; printf(\"[PASS] add_two = 7\\n\"); } else printf(\"[FAIL] add_two\\n\");\n");
    fprintf(f, "if(factorial(5) == 120) { passed++; printf(\"[PASS] factorial = 120\\n\"); } else printf(\"[FAIL] factorial\\n\");\n");
    fprintf(f, "if(sub_test() == 7) { passed++; printf(\"[PASS] sub_test = 7\\n\"); } else printf(\"[FAIL] sub_test\\n\");\n");
    fprintf(f, "if(mul_test() == 42) { passed++; printf(\"[PASS] mul_test = 42\\n\"); } else printf(\"[FAIL] mul_test\\n\");\n");
    fprintf(f, "if(div_test() == 25) { passed++; printf(\"[PASS] div_test = 25\\n\"); } else printf(\"[FAIL] div_test\\n\");\n");
    fprintf(f, "if(cmp_test() == 1) { passed++; printf(\"[PASS] cmp_test = 1\\n\"); } else printf(\"[FAIL] cmp_test\\n\");\n");
    fprintf(f, "if(float_test() == 5.0) { passed++; printf(\"[PASS] float_test = 5.0\\n\"); } else printf(\"[FAIL] float_test\\n\");\n");
    fprintf(f, "if(strcmp(string_test(), \"Hello QISC\") == 0) { passed++; printf(\"[PASS] string_test\\n\"); } else printf(\"[FAIL] string_test\\n\");\n");
    fprintf(f, "printf(\"[PASS] %%d/9 execution tests\\n\", passed);\n");
    fprintf(f, "return passed == 9 ? 0 : 1;\n");
    fprintf(f, "}\n");
    fclose(f);
    
    int ret = system("gcc -std=c11 -Wall -Wextra build/test_harness.c build/output.o -o build/test_run && ./build/test_run");
    if (ret != 0) {
        printf("[FAIL] Executable failed.\n");
        return 1;
    }

    // Section 7 — Living Component smoke test
    qisc_component_registry* reg = qisc_registry_create();
    qisc_ir_function* f_comp = qisc_ir_create_function(mod, "comp_double", qisc_type_proc(t_int, NULL, 0), true);
    qisc_ir_block* b_comp = qisc_ir_create_block(f_comp, "entry", 1.0);
    qisc_ir_inst* iawait = qisc_ir_emit_await_data(b_comp, t_int);
    qisc_value* d_ops[] = { qisc_value_inst(iawait), qisc_value_int(2) };
    qisc_ir_inst* imul = qisc_ir_emit_inst(b_comp, QISC_OP_MUL, t_int, d_ops, 2);
    qisc_ir_emit_emit(b_comp, qisc_value_inst(imul));
    
    qisc_component* comp = qisc_component_create(reg, "Doubler", f_comp);
    qisc_component_set_compiled_fn(comp, comp_double_native);
    qisc_component_start(comp);
    
    int64_t val = 21;
    qisc_component_trigger(comp, &val, sizeof(val));
    
    sleep(1);
    
    pthread_mutex_lock(&comp->state_lock);
    if (comp->output_data_size == sizeof(int64_t) && comp->output_data && *(int64_t*)comp->output_data == 42) {
        printf("[PASS] Living Component triggered correctly\n");
    } else {
        printf("[FAIL] Living Component output mismatch\n");
        pthread_mutex_unlock(&comp->state_lock);
        qisc_registry_destroy(reg);
        qisc_ir_destroy_module(mod);
        return 1;
    }
    pthread_mutex_unlock(&comp->state_lock);

    qisc_registry_destroy(reg);

    // Section 8 — Bridge API test
    qisc_backend_options bridge_options = qisc_backend_default_options();
    bridge_options.max_optimization_cycles = 0;
    bridge_options.object_path = "build/bridge_add_two.o";
    qisc_bridge* bridge = qisc_bridge_create("bridge_sample", &bridge_options);
    int bridge_param_types[] = { QISC_TYPE_INT, QISC_TYPE_INT };
    qisc_ir_function* bridge_func = qisc_bridge_begin_function(bridge, "add_two", true, 2, bridge_param_types);
    qisc_ir_block* bridge_block = qisc_bridge_create_block(bridge, bridge_func, "entry", 1.0);
    qisc_ir_inst* bridge_p0 = qisc_bridge_emit_param(bridge_block, 0);
    qisc_ir_inst* bridge_p1 = qisc_bridge_emit_param(bridge_block, 1);
    qisc_ir_inst* bridge_add = qisc_bridge_emit_binary(bridge_block, QISC_OP_ADD, bridge_p0, bridge_p1);
    qisc_bridge_emit_ret(bridge_block, bridge_add);
    qisc_bridge_set_profile(bridge, bridge_func, 1000, true);
    bool bridge_compiled = qisc_bridge_compile(bridge, "build/bridge_add_two.o");
    qisc_bridge_destroy(bridge);
    uint64_t direct_hash = build_direct_add_two_object_hash("build/direct_add_two.o");
    uint64_t bridge_hash = file_hash("build/bridge_add_two.o");
    if (bridge_compiled && bridge_hash == direct_hash) {
        printf("[PASS] bridge API\n");
    } else {
        printf("[FAIL] bridge API\n");
        qisc_ir_destroy_module(mod);
        return 1;
    }

    qisc_ir_destroy_module(mod);
    
    return 0;
}
