#define _POSIX_C_SOURCE 200809L
#include "qisc_backend.h"
#include "qisc_codegen.h"
#include "qisc_opt.h"

qisc_backend_options qisc_backend_default_options(void) {
    qisc_backend_options options;
    options.context = QISC_BACKEND_CONTEXT_CLI;
    options.max_optimization_cycles = 8;
    options.emit_object = true;
    options.object_path = "output.o";
    return options;
}

bool qisc_backend_compile_module(qisc_ir_module* mod, const qisc_backend_options* options) {
    if (!mod) return false;

    qisc_backend_options local = options ? *options : qisc_backend_default_options();
    qisc_pass_pipeline* pipeline = qisc_pipeline_create();
    if (!pipeline) return false;

    bool changed = true;
    uint64_t cycle = 1;
    while (changed && cycle <= local.max_optimization_cycles) {
        changed = qisc_pipeline_run_once(pipeline, mod, cycle);
        cycle++;
    }
    qisc_pipeline_destroy(pipeline);

    if (!local.emit_object) return true;
    return qisc_codegen_emit_elf(mod, local.object_path ? local.object_path : "output.o");
}
