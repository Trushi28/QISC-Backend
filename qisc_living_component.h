#ifndef QISC_LIVING_COMPONENT_H
#define QISC_LIVING_COMPONENT_H

#include "qisc_ir.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    const char*       name;
    qisc_type*        type;
    int               transport;
    int               port;
    const char*       endpoint;
} qisc_component_port;

typedef struct qisc_component {
    const char*           name;
    qisc_ir_function*     body;
    qisc_component_port*  inputs;
    size_t                num_inputs;
    qisc_component_port*  outputs;
    size_t                num_outputs;
    
    qisc_component_state  state;
    pthread_mutex_t       state_lock;
    pthread_cond_t        data_ready;
    
    void*                 input_data;
    size_t                input_data_size;
    void*                 output_data;
    size_t                output_data_size;
    
    volatile bool         should_stop;
    pthread_t             thread_id;
    
    uint64_t              trigger_count;
    uint64_t              last_trigger_cycle;
    
    struct qisc_component** entangled;
    size_t                  num_entangled;
    struct qisc_component* next;
} qisc_component;

typedef struct {
    qisc_component* first;
    qisc_component* last;
    size_t          count;
} qisc_component_registry;

qisc_component_registry* qisc_registry_create(void);
void qisc_registry_destroy(qisc_component_registry* r);

qisc_component* qisc_component_create(
    qisc_component_registry* registry,
    const char* name,
    qisc_ir_function* body
);

void qisc_component_add_input(
    qisc_component* comp,
    const char* name,
    qisc_type* type,
    int transport,
    int port,
    const char* endpoint
);

void qisc_component_add_output(
    qisc_component* comp,
    const char* name,
    qisc_type* type,
    int transport,
    int port,
    const char* endpoint
);

void qisc_component_trigger(
    qisc_component* comp,
    void* data,
    size_t data_size
);

void* qisc_component_run_loop(void* arg);
pthread_t qisc_component_start(qisc_component* comp);

void qisc_registry_analyze_entanglement(
    qisc_component_registry* registry
);

typedef struct qisc_bytebuf qisc_bytebuf;

void qisc_codegen_emit_component_wrapper(
    qisc_component* comp,
    qisc_bytebuf* buf
);

#endif
