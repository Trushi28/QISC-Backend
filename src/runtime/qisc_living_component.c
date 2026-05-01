#define _POSIX_C_SOURCE 200809L
#include "qisc_living_component.h"
#include <stdlib.h>
#include <string.h>

extern void emit_prologue(qisc_bytebuf* buf, int frame_size);
extern void emit_epilogue(qisc_bytebuf* buf);
extern void emit_rex(qisc_bytebuf* buf, int w, int r, int x, int b);
extern void emit_modrm(qisc_bytebuf* buf, uint8_t mod, uint8_t reg, uint8_t rm);
extern size_t bytebuf_here(qisc_bytebuf* b);
extern void bytebuf_push(qisc_bytebuf* b, uint8_t byte);
extern void bytebuf_push_u32(qisc_bytebuf* b, uint32_t v);

static uint64_t global_cycle_counter = 1;

qisc_component_registry* qisc_registry_create(void) {
    return (qisc_component_registry*)calloc(1, sizeof(qisc_component_registry));
}

void qisc_registry_destroy(qisc_component_registry* r) {
    if (!r) return;
    qisc_component* curr = r->first;
    while (curr) {
        qisc_component* next = curr->next;
        pthread_mutex_lock(&curr->state_lock);
        curr->should_stop = true;
        pthread_cond_signal(&curr->data_ready);
        pthread_mutex_unlock(&curr->state_lock);
        if (curr->thread_started) pthread_join(curr->thread_id, NULL);
        
        pthread_mutex_destroy(&curr->state_lock);
        pthread_cond_destroy(&curr->data_ready);
        for (size_t i = 0; i < curr->num_inputs; i++) {
            free((void*)curr->inputs[i].name);
            free((void*)curr->inputs[i].endpoint);
        }
        for (size_t i = 0; i < curr->num_outputs; i++) {
            free((void*)curr->outputs[i].name);
            free((void*)curr->outputs[i].endpoint);
        }
        free(curr->inputs);
        free(curr->outputs);
        free(curr->input_data);
        free(curr->output_data);
        free(curr->entangled);
        free((void*)curr->name);
        free(curr);
        curr = next;
    }
    free(r);
}

qisc_component* qisc_component_create(qisc_component_registry* registry, const char* name, qisc_ir_function* body) {
    qisc_component* comp = (qisc_component*)calloc(1, sizeof(qisc_component));
    comp->name = strdup(name);
    pthread_mutex_init(&comp->state_lock, NULL);
    pthread_cond_init(&comp->data_ready, NULL);
    comp->state = QISC_STATE_DORMANT;
    comp->should_stop = false;
    comp->body = body;
    
    if (registry->last) {
        registry->last->next = comp;
        registry->last = comp;
    } else {
        registry->first = registry->last = comp;
    }
    registry->count++;
    return comp;
}

void qisc_component_add_input(qisc_component* comp, const char* name, qisc_type* type, int transport, int port, const char* endpoint) {
    comp->inputs = realloc(comp->inputs, sizeof(qisc_component_port) * (comp->num_inputs + 1));
    comp->inputs[comp->num_inputs].name = name ? strdup(name) : NULL;
    comp->inputs[comp->num_inputs].type = type;
    comp->inputs[comp->num_inputs].transport = transport;
    comp->inputs[comp->num_inputs].port = port;
    comp->inputs[comp->num_inputs].endpoint = endpoint ? strdup(endpoint) : NULL;
    comp->num_inputs++;
}

void qisc_component_add_output(qisc_component* comp, const char* name, qisc_type* type, int transport, int port, const char* endpoint) {
    comp->outputs = realloc(comp->outputs, sizeof(qisc_component_port) * (comp->num_outputs + 1));
    comp->outputs[comp->num_outputs].name = name ? strdup(name) : NULL;
    comp->outputs[comp->num_outputs].type = type;
    comp->outputs[comp->num_outputs].transport = transport;
    comp->outputs[comp->num_outputs].port = port;
    comp->outputs[comp->num_outputs].endpoint = endpoint ? strdup(endpoint) : NULL;
    comp->num_outputs++;
}

void qisc_component_trigger(qisc_component* comp, void* data, size_t data_size) {
    pthread_mutex_lock(&comp->state_lock);
    free(comp->input_data);
    comp->state = QISC_STATE_TRIGGERED;
    comp->input_data = malloc(data_size);
    if (data_size > 0 && data) memcpy(comp->input_data, data, data_size);
    comp->input_data_size = data_size;
    comp->trigger_count++;
    comp->last_trigger_cycle = global_cycle_counter++;
    pthread_cond_signal(&comp->data_ready);
    pthread_mutex_unlock(&comp->state_lock);
}

void qisc_component_set_compiled_fn(qisc_component* comp, qisc_component_compiled_fn compiled_fn) {
    if (!comp) return;
    pthread_mutex_lock(&comp->state_lock);
    comp->compiled_fn = compiled_fn;
    pthread_mutex_unlock(&comp->state_lock);
}

void* qisc_component_run_loop(void* arg) {
    qisc_component* comp = (qisc_component*)arg;
    while (1) {
        pthread_mutex_lock(&comp->state_lock);
        while (comp->state != QISC_STATE_TRIGGERED && !comp->should_stop) {
            pthread_cond_wait(&comp->data_ready, &comp->state_lock);
        }
        if (comp->should_stop) {
            pthread_mutex_unlock(&comp->state_lock);
            return NULL;
        }
        comp->state = QISC_STATE_EXECUTING;
        void* data = comp->input_data;
        size_t data_size = comp->input_data_size;
        qisc_component_compiled_fn compiled_fn = comp->compiled_fn;
        pthread_mutex_unlock(&comp->state_lock);
        
        free(comp->output_data);
        comp->output_data = NULL;
        comp->output_data_size = 0;
        if (compiled_fn) compiled_fn(comp, data, data_size);
        
        free(data);
        
        pthread_mutex_lock(&comp->state_lock);
        comp->state = QISC_STATE_RETURNING;
        comp->input_data = NULL;
        
        for (size_t i = 0; i < comp->num_entangled; i++) {
            if (comp->output_data_size > 0) {
                qisc_component_trigger(comp->entangled[i], comp->output_data, comp->output_data_size);
            }
        }
        
        comp->state = QISC_STATE_DORMANT;
        pthread_mutex_unlock(&comp->state_lock);
    }
    return NULL;
}

pthread_t qisc_component_start(qisc_component* comp) {
    if (pthread_create(&comp->thread_id, NULL, qisc_component_run_loop, comp) == 0) {
        comp->thread_started = true;
    }
    return comp->thread_id;
}

void qisc_registry_analyze_entanglement(qisc_component_registry* registry) {
    for (qisc_component* A = registry->first; A; A = A->next) {
        for (qisc_component* B = registry->first; B; B = B->next) {
            if (A == B) continue;
            uint64_t diff = A->last_trigger_cycle > B->last_trigger_cycle ? A->last_trigger_cycle - B->last_trigger_cycle : B->last_trigger_cycle - A->last_trigger_cycle;
            if (diff <= 1 && A->trigger_count > 10 && B->trigger_count > 10) {
                bool found_in_A = false;
                for(size_t i=0; i<A->num_entangled; i++) if (A->entangled[i] == B) found_in_A = true;
                if (!found_in_A) {
                    A->entangled = realloc(A->entangled, sizeof(qisc_component*) * (A->num_entangled + 1));
                    A->entangled[A->num_entangled++] = B;
                }
                bool found_in_B = false;
                for(size_t i=0; i<B->num_entangled; i++) if (B->entangled[i] == A) found_in_B = true;
                if (!found_in_B) {
                    B->entangled = realloc(B->entangled, sizeof(qisc_component*) * (B->num_entangled + 1));
                    B->entangled[B->num_entangled++] = A;
                }
            }
        }
    }
}

void qisc_codegen_emit_component_wrapper(qisc_component* comp, qisc_bytebuf* buf) {
    (void)comp;
    emit_prologue(buf, 0);
    size_t loop_label = bytebuf_here(buf);
    
    // test r14, r14
    emit_rex(buf, 1, 1, 0, 1);
    bytebuf_push(buf, 0x85);
    emit_modrm(buf, 3, 14&7, 14&7);
    
    // je loop_label
    bytebuf_push(buf, 0x0F);
    bytebuf_push(buf, 0x84);
    bytebuf_push_u32(buf, loop_label - (bytebuf_here(buf) + 6)); // Correct offset
    
    // mov rdi, r14
    emit_rex(buf, 1, 0, 0, 1);
    bytebuf_push(buf, 0x8B);
    emit_modrm(buf, 3, 7, 14&7);
    
    // call body
    bytebuf_push(buf, 0xE8);
    bytebuf_push_u32(buf, 0); // Reloc normally patched
    
    // xor r14, r14
    emit_rex(buf, 1, 1, 0, 1);
    bytebuf_push(buf, 0x33);
    emit_modrm(buf, 3, 14&7, 14&7);
    
    // jmp loop_label
    bytebuf_push(buf, 0xE9);
    bytebuf_push_u32(buf, loop_label - (bytebuf_here(buf) + 4));
    
    emit_epilogue(buf);
    bytebuf_push(buf, 0xC3);
}
