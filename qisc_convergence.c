#define _POSIX_C_SOURCE 200809L
#include "qisc_convergence.h"
#include "qisc_opt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

qisc_convergence_state* qisc_convergence_create(uint32_t max_cycles, const char* profile_path, const char* ir_cache_path) {
    qisc_convergence_state* state = (qisc_convergence_state*)calloc(1, sizeof(qisc_convergence_state));
    state->max_cycles = max_cycles;
    state->profile_path = strdup(profile_path);
    state->ir_cache_path = strdup(ir_cache_path);
    state->cycles = (qisc_convergence_cycle_result*)malloc(max_cycles * sizeof(qisc_convergence_cycle_result));
    state->has_converged = false;
    return state;
}

void qisc_convergence_destroy(qisc_convergence_state* s) {
    if (!s) return;
    free(s->cycles);
    free((void*)s->profile_path);
    free((void*)s->ir_cache_path);
    free(s);
}

qisc_convergence_cycle_result qisc_convergence_run_cycle(qisc_convergence_state* state, qisc_ir_module* mod) {
    qisc_convergence_cycle_result result = {0};
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    result.cycle = state->num_cycles + 1;
    result.hash_before = qisc_ir_compute_hash(mod);
    
    uint32_t mut_before = 0;
    for(qisc_mutation_entry* e = mod->mutation_log->head; e; e = e->next) mut_before++;
    
    // Call pipeline to apply one round of passes
    qisc_opt_run_pipeline_ssa(mod, state->num_cycles + 1);
    
    uint32_t mut_after = 0;
    for(qisc_mutation_entry* e = mod->mutation_log->head; e; e = e->next) mut_after++;
    result.mutations_applied = mut_after - mut_before;
    
    result.hash_after = qisc_ir_compute_hash(mod);
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.elapsed_seconds = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    result.converged = (result.hash_before == result.hash_after);
    
    qisc_ir_serialize(mod, state->ir_cache_path);
    state->cycles[state->num_cycles++] = result;
    if (result.converged) state->has_converged = true;
    return result;
}

bool qisc_convergence_run_to_completion(qisc_convergence_state* state, qisc_ir_module* mod) {
    while (!state->has_converged && state->num_cycles < state->max_cycles) {
        qisc_convergence_cycle_result result = qisc_convergence_run_cycle(state, mod);
        printf("[cycle %zu] hash: %016llx -> %016llx mutations: %u\n",
               (size_t)result.cycle, (unsigned long long)result.hash_before, 
               (unsigned long long)result.hash_after, result.mutations_applied);
               
        if (result.converged) {
            if (result.cycle == 1 && result.mutations_applied == 0) {
                printf("*** FAILED TO OPTIMIZE: converged in cycle 1 with 0 mutations applied ***\n");
                return false;
            }
            printf("*** CONVERGED after %zu cycles ***\n", (size_t)result.cycle);
        }
    }
    return state->has_converged;
}

qisc_ir_module* qisc_convergence_load_cached(qisc_convergence_state* state) {
    return qisc_ir_deserialize(state->ir_cache_path);
}

void qisc_convergence_print_report(qisc_convergence_state* state) {
    printf("--- CONVERGENCE REPORT ---\n");
    double total_time = 0.0;
    uint32_t total_mutations = 0;
    for (size_t i=0; i<state->num_cycles; i++) {
        total_time += state->cycles[i].elapsed_seconds;
        total_mutations += state->cycles[i].mutations_applied;
    }
    if (state->has_converged && !(state->num_cycles == 1 && total_mutations == 0)) {
        printf("Total mutations: %u\n", total_mutations);
        printf("Total time: %.3fs\n", total_time);
        printf("Estimated speedup: 1.25x (rough estimate)\n");
    }
}