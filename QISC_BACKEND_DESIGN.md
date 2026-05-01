# QISC Backend — New Features Design Document

> This document covers all new features and architectural decisions discussed
> for the QISC compiler backend. The existing codebase (qisc_ir, qisc_cfg,
> qisc_ssa, qisc_opt, qisc_codegen, qisc_living_component, qisc_convergence)
> is the foundation. Everything here extends it.

---

## 1. Dual-Mode IR Architecture (TAC + SSA)

The core architectural decision. The IR operates in two distinct modes:

**TAC mode** (default, `is_in_ssa_form = false`):
- Persistent storage format
- Used for: Living IR mutations, cloning, outlining, serialization,
  profile-driven transforms, convergence cycles
- Multiple definitions of the same logical variable allowed
- No phi nodes required
- Rule: ALL structural CFG mutations happen here

**SSA mode** (temporary, `is_in_ssa_form = true`):
- Entered via `qisc_ssa_construct()` (Braun 2013)
- Used for: constant folding, DCE, copy propagation
- Every instruction produces exactly one value
- Phi nodes resolve control flow merges
- Exited via `qisc_ssa_destruct()` before codegen

**The pipeline:**
```
TAC (profile/mutate) 
  → qisc_ssa_construct 
  → optimize (const fold, DCE, copy prop) 
  → qisc_ssa_destruct 
  → codegen (x86-64 ELF)
```

**Hard rule:** NEVER mutate CFG while `is_in_ssa_form = true`.
The structural passes (`qisc_opt_inline`, `qisc_opt_outline_cold_blocks`,
`qisc_opt_specialize_constants`) must assert `!func->is_in_ssa_form` on entry.

---

## 2. Pending Fixes for Current Backend

These are known bugs in the current codebase that must be fixed:

### 2a. IDIV Register Clobber (critical miscompilation)
CQO silently overwrites RDX, destroying any live value in RDX.

Fix:
- Remove RAX (id=0) and RDX (id=2) from the allocatable register pool
- New allocatable set: `{RCX=1, RSI=6, RDI=7, R8=8, R9=9, R10=10, R11=11}`
- For `QISC_OP_DIV`: check if any live interval has register == RDX at
  the current instruction index. If live, emit spill before CQO, reload after.
- Add instruction constraint flags to `qisc_ir_inst`:
  - `requires_rax` — DIV, RET
  - `clobbers_rdx` — DIV (via CQO)
  - `clobbers_rax` — DIV result in RAX

### 2b. Trivial PHI Elimination
PHIs where all incoming values are identical must be eliminated:
```
%3 = PHI [%1 from block_a, %1 from block_b]  →  replace all uses of %3 with %1
```
Run inside `qisc_ssa_construct` after all PHIs are inserted, looping until
no more trivial PHIs exist.

### 2c. CFG Invalidation After Mutation
Add `bool is_valid` to `qisc_cfg`. Set false whenever a structural mutation
occurs. Assert `cfg->is_valid` before any dominance computation.

### 2d. SSA Analysis Pass Guards
Const fold, copy propagation, and DCE must check `func->is_in_ssa_form == true`
before operating. Skip with a warning if called on TAC functions.

### 2e. Thread Hang in Living Component
Add `volatile bool should_stop` and `pthread_t thread_id` to `qisc_component`.
In `qisc_registry_destroy`: set `should_stop = true`, signal the condition
variable, then `pthread_join`. Program must exit cleanly under 5 seconds.

### 2f. Serialization Still Hollow
Current `qisc_ir_serialize` writes only 12 bytes (header only).
Full serialization is required for convergence to survive between
compiler invocations. Must implement complete binary format including
all functions, blocks, instructions, operands, and round-trip hash verification.

---

## 3. Library Absorption System (Sandbox)

The most ambitious new feature. QISC absorbs foreign libraries through
behavioral observation rather than source translation.

**Core concept:** Instead of calling PyTorch/axum as a black box, the Living IR
observes every instruction that executes inside the sandbox, profiles it across
runs, and replicates the behavior as native QISC IR. The dependency shrinks
until eliminated.

**Architecture:**
```
Foreign Library → Sandbox Backend → Living IR observes → 
Convergence replicates → Native QISC functions replace sandbox calls
```

**The custom backend as sandbox:** The new backend (TAC-based, no LLVM) is the
sandbox execution environment. Foreign code runs on it so the Living IR can
observe every instruction natively — no black box, no instrumentation bolted on.

**Sandbox observation API** (new file: `qisc_sandbox.h/.c`):
```c
// Called for every instruction executed in sandbox
typedef void (*qisc_sandbox_observe_fn)(
    qisc_ir_inst* inst,
    qisc_value*   result,
    uint64_t      cycle
);

typedef struct {
    qisc_sandbox_observe_fn observer;
    qisc_ir_module*          observed_module;
    uint64_t                 observation_count;
    bool                     replication_ready;  // enough data to replicate
} qisc_sandbox;
```

**Phases:**
1. Phase 1 (Observation only, no isolation) — run library normally with Living IR
   instrumentation hooks watching it. Profile without sandboxing.
2. Phase 2 (Soft boundary) — use `ptrace`/`LD_PRELOAD` to intercept
   malloc/free and syscalls without a full VM.
3. Phase 3 (Real sandbox) — full isolation once observation data defines
   exactly what the sandbox needs to support.

**GC stripping through convergence:** Python's GC makes runtime decisions.
But if the Living IR has observed enough runs it knows exactly when objects
are freed, what allocations are always the same size, what data never outlives
a function. Static lifetime decisions replace GC decisions.

**Import syntax (future):**
```qisc
import rust:axum
import python:pytorch
import c:openssl
```

---

## 4. Living Components

Reactive execution model. A component sits dormant until data arrives,
processes it, emits output, returns to dormant.

**State machine:** `DORMANT → TRIGGERED → EXECUTING → RETURNING → DORMANT`

**Pragma definition:**
```qisc
#pragma component:reactive
#pragma input:json<Request>
#pragma input:http:8080
#pragma output:json<Response>
#pragma output:https:api.service.com
```

No async/await, no explicit threading. The compiler figures out scheduling
from the component's data dependencies observed through the Living IR.

**Entanglement:** Components that always trigger together get co-scheduled.
The Living IR observes `last_trigger_cycle` across multiple runs. If two
components' trigger cycles are always within 1 of each other, they are
marked entangled and the runtime pre-activates the partner when one is triggered.

**Disentanglement:** The Living IR also detects when previously correlated
components diverge in behavior and separates their scheduling, cleaning up
false dependencies.

**Codegen wrapper** (per component, in `.text.hot`):
```
loop:
  test r14, r14       ; check if data pointer set (R14 = QISC_REG_COMP_DATA)
  je loop             ; spin if null
  mov rdi, r14        ; arg0 = data pointer
  call body_function  ; execute component body
  xor r14, r14        ; clear data pointer
  jmp loop            ; return to dormant
```

**Pending:** The component body call is currently commented out. The pthread
loop exists and the state machine works, but `compiled_fn` is never invoked.
This is the next thing to wire up after serialization.

---

## 5. Context-Aware Compilation

`#pragma context:X` produces genuinely different native backend configurations
for the custom C backend. There is no LLVM dependency in the backend pipeline.

| Context | Optimization Focus | Key Passes |
|---|---|---|
| `server` | Throughput, long-running | aggressive TAC/SSA pipeline, hot block layout, inliner threshold 375 |
| `cli` | Fast startup, small binary | balanced TAC/SSA pipeline, host x86-64 emission |
| `embedded` | Minimize size + energy | size-focused pipeline, no vector expansion, inliner threshold 50 |
| `notebook` | Fast compile, interactive | shallow optimization pipeline, no loop unrolling |
| `web` | Size optimized | size-focused native object emission |

**Future additions:**
- `context:server` + async: implicit concurrency detection by the Living IR.
  Independent I/O operations in the same function get automatically parallelized
  — no `async`/`await` syntax, the compiler discovers it.
- `context:embedded`: `#pragma no_std` equivalent, direct memory access,
  interrupt handler support, inline assembly.

---

## 6. Multi-Syntax Support

Three syntax modes, switched by pragma, same IR output:

**Classic (brace) style** — default:
```qisc
proc factorial(int n) gives int {
    if n < 1 { give 1; }
    give n * factorial(n - 1);
}
```

**Python style** (`#pragma style:python`):
```qisc
#pragma style:python
proc factorial(int n) gives int:
    if n < 1:
        give 1
    give n * factorial(n - 1)
```

**Expression style** (`#pragma style:expression`):
```qisc
#pragma style:expression
proc factorial(int n) gives int => n < 1 ? 1 : n * factorial(n - 1)
```

**Implementation:** The parser has a single `#pragma` router at the top that
selects which parsing functions to call. Not two separate parsers — one parser
with two block-parsing implementations (brace vs indent) selected at entry.
The pragma detection uses content analysis, not just the pragma declaration.

---

## 7. Compile-Time Memory Management via Convergence

No GC, no borrow checker. The Living IR observes memory patterns across runs
and after convergence collapses to static lifetime decisions.

**What the Living IR observes:**
- This component always allocates exactly X bytes for this request type
- These two fields are always accessed together (layout optimization)
- This data is never needed after this point (early free)
- This allocation is always freed before the component goes dormant (stack allocation)

**Native backend hooks for this:**
- TAC lifetime markers for precise allocation/free boundaries
- Backend-local escape analysis to detect non-escaping allocations
- `alloca` vs heap decisions based on observed sizes

**For the custom backend:**
- Per-component arena allocation: each Living Component gets an arena.
  When the component finishes processing a request the entire arena drops.
  No GC needed, no cycle problem.
- Convergence extends the arena sizes to exactly what profiling observed.

---

## 8. ML/AI Performance via Library Absorption

When PyTorch is absorbed through the sandbox:

**What gets eliminated:**
- Python GIL overhead
- Dynamic typing checks per operation
- Python/C++ boundary crossings
- Generic tensor kernels (replaced with model-specific specializations)
- GC pauses during training

**What the Living IR generates instead:**
- Kernels specialized for the exact tensor shapes in your model
- Fused operations (what PyTorch's `torch.compile` tries to do manually)
- Static memory lifetimes for tensors (known from profiling)
- CUDA kernel specialization via sandbox observation of CUDA calls

**Conservative estimate vs vanilla PyTorch:**
- Inference: 6-10x (pattern stability makes convergence fast and complete)
- Training: 2-4x (actual math dominates, but framework overhead eliminated)

This makes the hardware demand story: same model, less GPU, less power.
The frontier pretraining ceiling (attention is O(n²)) doesn't change,
but the 95% of workloads below that ceiling get dramatically cheaper.

---

## 9. OSDev Support

Bare metal target via `context:embedded`:

**What's needed (not yet implemented):**
- `#pragma no_std` — disable runtime, no libc
- Inline assembly support in the IR and codegen
- Direct memory address access (cast int to pointer, write to hardware registers)
- Interrupt handler function attribute (preserves all registers, uses IRET)
- Custom linker script support in ELF emitter
- Multi-architecture targets beyond x86-64 (ARM, RISC-V)

**What already works:** The custom backend emits relocatable ELF objects
with hot/cold section separation. The context system sets optimization flags.
The foundation is there.

---

## 10. Package Manager (Future, Not Now)

Not redundant — necessary eventually. But building it now would consume months
while the compiler itself is incomplete.

**Minimal viable approach when the time comes:**
- `qisc.toml` for project metadata and dependencies
- Dependencies resolved from GitHub URLs initially (like early Cargo)
- `qisc init` scaffolds a new project
- `qisc fmt` auto-formatter (especially important with three syntax modes)
- LSP server for VS Code integration (highest adoption impact)
- `qisc test` test runner

**Not needed:** GUI framework support. QISC's identity is pipelines, streams,
server context, Unix composition. GUI is the opposite of everything the
language optimizes for.

---

## 11. Interoperability

**Rust interop (most feasible):**
Both Rust and QISC eventually lower to the same custom QISC IR
via the absorption system. Cross-boundary optimization is possible because
the Living IR can see both sides.

**C interop:**
Already works via the C ABI. Any C library callable today.

**Python interop:**
Harder — requires the sandbox (CPython has its own memory model and GC).
Phase 1: call as subprocess with structured data via Living Components HTTP pragma.
Phase 2: sandbox observation.

**The key unlock:** If QISC can import Rust crates natively and C libraries
transparently, people don't need to abandon existing code. Write the hot path
in QISC, keep everything else. That's how adoption happens.

---

## 12. Backend Replacement Readiness Checklist

What the custom backend needs before it can serve as the main compiler backend:

- [ ] Fix IDIV register clobber
- [ ] Complete serialization (currently 12 bytes)
- [ ] Wire Living Component body call
- [ ] String support (`.rodata` section, RIP-relative addressing)
- [ ] Struct field access (LOAD/STORE with byte offset)
- [ ] Float support (XMM registers, movsd/addsd/mulsd)
- [ ] Closure environment (R13 reserved but never used)
- [ ] Stream runtime helpers (`__qisc_stream_map` etc)
- [ ] Try/catch runtime (`__qisc_try_begin`, `__qisc_fail`)
- [ ] Global variables (`.data` section in ELF emitter)
- [ ] Module/import system awareness
- [ ] `qisc_backend_bridge.c` — AST→IR layer connecting QISC frontend to backend
- [ ] `.note.GNU-stack` section to suppress executable stack warning
- [ ] Multi-architecture support for OSDev targets

**Current status:** 9/9 execution tests pass for int arithmetic and recursion.
The foundation is solid. Estimated 3-4 months of focused work to full feature
parity with what the compiler frontend requires for QISC.

---

## 13. The Quantum Inspiration (Marketing + Reality)

The naming is mechanistically accurate, not just metaphor:

- **Superposition** — multiple possible implementations exist before convergence
- **Collapse** — convergence collapses to the single optimal binary
- **Entanglement** — correlated Living Components are co-scheduled based on
  observed trigger patterns
- **Disentanglement** — Living IR removes false correlations between components

**The pitch:**
> Every run is a different quantum state. Convergence collapses it to the one
> optimal reality. Write reactive components. QISC figures out the rest.
> No async, no threads, no GC. Just define what comes in and what goes out.

---

*Document generated from design discussions. Reflects intended architecture,
not current implementation status. See checklist in Section 12 for current
implementation gaps.*
