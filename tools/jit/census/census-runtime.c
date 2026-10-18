/* Execution census build: replaces web/jit-runtime.c. Every instruction the
 * machine is about to interpret is counted by (pc, ISA, raw opcode); nothing
 * is ever executed here, so the guest runs on the interpreter alone. */
#include "jit-runtime.h"
#include "arm_wasm_jit.h"
#include <emscripten.h>
#include <stdint.h>
#include <string.h>

#define SLOTS_LOG2 22u
#define SLOTS (1u << SLOTS_LOG2)
typedef struct { uint32_t key, op; double count; } entry_t;
static entry_t table[SLOTS];
static unsigned used, overflow;
static s5l8900_t *bound_machine;
unsigned arm_wasm_jit_enabled;

void web_jit_bind(s5l8900_t *machine) {
    bound_machine = machine;
    arm_wasm_jit_enabled = 0;
}
EMSCRIPTEN_KEEPALIVE void census_enable(unsigned on) {
    arm_wasm_jit_enabled = on && bound_machine ? 1u : 0u;
}
EMSCRIPTEN_KEEPALIVE void census_clear(void) { memset(table, 0, sizeof table); used = overflow = 0; }
EMSCRIPTEN_KEEPALIVE uintptr_t census_table(void) { return (uintptr_t)table; }
EMSCRIPTEN_KEEPALIVE unsigned census_slots(void) { return SLOTS; }
EMSCRIPTEN_KEEPALIVE unsigned census_overflow(void) { return overflow; }

unsigned arm_wasm_jit_try(arm_cpu_t *c, unsigned limit) {
    (void)limit;
    if (c != &bound_machine->cpu) return 0;
    unsigned thumb = (c->cpsr & ARM_CPSR_T) != 0;
    uint32_t pc = c->r[15];
    if (pc & (thumb ? 1u : 3u)) return 0;
    if (!c->fetch_host || c->fetch_gen != c->tlb_gen || c->fetch_blk != (pc & ~1023u))
        if (!arm_fetch_cache_try_refill(c, pc, (c->cpsr & 31u) != ARM_MODE_USR)) return 0;
    const uint8_t *p = c->fetch_host + (pc & 1023u);
    uint32_t op = thumb ? (uint32_t)(p[0] | p[1] << 8)
                        : (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
    uint32_t key = pc | thumb;
    unsigned slot = ((key * 0x9e3779b1u) ^ (op * 0x85ebca6bu)) >> (32u - SLOTS_LOG2);
    for (unsigned n = 0; n < 64u; n++) {
        entry_t *e = &table[(slot + n) & (SLOTS - 1u)];
        if (e->count && e->key == key && e->op == op) { e->count++; return 0; }
        if (!e->count) { e->key = key; e->op = op; e->count = 1; used++; return 0; }
    }
    overflow++;
    return 0;
}
