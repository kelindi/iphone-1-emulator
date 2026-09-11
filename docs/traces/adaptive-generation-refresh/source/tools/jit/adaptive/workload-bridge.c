/* Isolated Stage 2 harness: use the real browser API and its private machine.
 * This translation unit replaces emu_api.c only in the qualification build.
 * ABI1 is deliberately absent; never link web/jit-runtime.c into this harness. */
#ifdef S5LBOX_WASM_JIT
#error "Adaptive workload qualification cannot mix uninstrumented ABI1 stores"
#endif
#ifndef S5LBOX_ADAPTIVE_JIT
#error "Adaptive workload qualification requires the real adaptive scheduler"
#endif
#include "../../../web/emu_api.c"
#include "../../../web/adaptive-runtime.h"

/* Stage 2 observation policy: eligible admission misses, keyed by exact PC,
 * ISA, CPU mode and TLB generation. Direct-map collisions restart the count.
 * One attempt per 1024 observations of a resident key; transient refusals
 * can retry. Installed units and binary bytes remain runtime bounded.
 * This synchronous qualification policy is not the Stage 3 background worker. */
#define WORKLOAD_HOT_SLOTS 16384u
typedef struct { uint32_t pc, generation, mode, thumb, count; bool used; } hot_entry_t;
static hot_entry_t hot_entries[WORKLOAD_HOT_SLOTS];
static uint64_t compile_attempts;
EM_JS(void, workload_compile, (uint32_t pc, unsigned thumb), {
    if (typeof Module.workloadCompile !== 'function') throw new Error('Missing workload compiler owner');
    Module.workloadCompile(pc >>> 0, !!thumb);
});
static void workload_miss(uint32_t pc, unsigned thumb) {
    uint32_t mode = machine.cpu.cpsr & ARM_CPSR_MODE_MASK;
    unsigned slot = ((pc >> 1) ^ (pc >> 15) ^ (thumb * 0x987u)) & (WORKLOAD_HOT_SLOTS - 1u);
    hot_entry_t *entry = &hot_entries[slot];
    if (!entry->used || entry->pc != pc || entry->thumb != thumb ||
        entry->mode != mode || entry->generation != machine.cpu.tlb_gen)
        *entry = (hot_entry_t){pc, machine.cpu.tlb_gen, mode, thumb, 0u, true};
    if (++entry->count == 1024u) {
        entry->count = 0u;
        /* Diagnostic saturation never disables future collection. */
        if (compile_attempts < UINT64_C(9007199254740991)) compile_attempts++;
        workload_compile(pc, thumb);
    }
}
EMSCRIPTEN_KEEPALIVE void workload_observe(void) { adaptive_observe(workload_miss); }
EMSCRIPTEN_KEEPALIVE double workload_attempts(void) { return (double)compile_attempts; }

EM_JS(void, workload_expose_memory, (), { Module.jitMemory = wasmMemory; });

/* The host drains retired handles after every restore/rebind, before installing
 * replacements. This function never removes JS function-table ownership. */
EMSCRIPTEN_KEEPALIVE unsigned workload_prepare(void) {
    if (!machine_ready) return 0u;
    adaptive_bind(&machine.cpu);
    memset(hot_entries, 0, sizeof hot_entries); compile_attempts = 0u;
    if (adaptive_failed()) return 0u;
    if (!adaptive_track_ram(machine.ram, machine.ram_size)) return 0u;
    machine.bus.ram_written = adaptive_ram_written;
    workload_expose_memory();
    return 1u;
}

/* ABI2-only layout; never interpret an ABI1 emu_jit_offset descriptor here. */
EMSCRIPTEN_KEEPALIVE uint32_t workload_layout(unsigned field) {
    switch (field) {
    case 0: return ARM_ADAPTIVE_ABI;
    case 1: return offsetof(arm_cpu_t, r);
    case 2: return offsetof(arm_cpu_t, cpsr);
    case 3: return sizeof(arm_cpu_t);
    case 4: return sizeof(arm_adaptive_fetch_witness_t);
    case 5: return offsetof(arm_adaptive_fetch_witness_t, host);
    case 6: return offsetof(arm_adaptive_fetch_witness_t, block);
    case 7: return offsetof(arm_adaptive_fetch_witness_t, code_version);
    case 8: return sizeof(arm_adaptive_result_t);
    case 9: return sizeof(void *);
    default: return UINT32_MAX;
    }
}
EMSCRIPTEN_KEEPALIVE uintptr_t workload_cpu(void) {
    return machine_ready ? (uintptr_t)&machine.cpu : 0u;
}
EMSCRIPTEN_KEEPALIVE uintptr_t workload_ram(void) {
    return machine_ready ? (uintptr_t)machine.ram : 0u;
}
EMSCRIPTEN_KEEPALIVE uint32_t workload_ram_size(void) {
    return machine_ready ? machine.ram_size : 0u;
}
