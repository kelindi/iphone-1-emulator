/* Derived execution cache only: no guest state or snapshot schema is changed. */
#include "jit-runtime.h"
#include "arm_wasm_jit.h"
#include <emscripten.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define REGION_SLOTS_LOG2 12u
#define REGION_SLOTS (1u << REGION_SLOTS_LOG2)
#define REGION_CAP 1024u
#define LINK_SLOTS 4096u
#define REGION_BYTES 1024u
#define MASK_WORDS 16u
#define JIT_ABI_REVISION 3u

/* These details are embedded by compiler.cjs rather than represented by an
 * exported offset. Fail the build if a future cache-layout edit would leave a
 * same-size but incompatible seed looking ABI-compatible. */
_Static_assert(sizeof(void *) == 4u, "The seed requires wasm32 pointers");
_Static_assert(ARM_DREAD_ENTRIES == 1024u && ARM_DREAD_SETS == 512u,
               "Update seed cache set calculation");
#define CACHE_FIELD_OFFSET(cache, field) \
    (offsetof(arm_cpu_t, cache[0].field) - offsetof(arm_cpu_t, cache[0]))
_Static_assert(CACHE_FIELD_OFFSET(dread, host) == 0u &&
               CACHE_FIELD_OFFSET(dread, tag) == 4u &&
               CACHE_FIELD_OFFSET(dread, gen) == 8u &&
               CACHE_FIELD_OFFSET(dwrite, host) == 0u &&
               CACHE_FIELD_OFFSET(dwrite, tag) == 4u &&
               CACHE_FIELD_OFFSET(dwrite, gen) == 8u,
               "Update generated cache field accesses and ABI revision");
_Static_assert(sizeof ((arm_cpu_t *)0)->dread[0] ==
               sizeof ((arm_cpu_t *)0)->dwrite[0], "Seed cache strides differ");
#undef CACHE_FIELD_OFFSET

/* One exported seed runner enters region `index`; regions then continue
 * into each other inside the seed through the link table below. */
typedef unsigned (*runner_fn)(arm_cpu_t *, unsigned budget, unsigned index);
typedef struct {
    uint32_t base;
    unsigned thumb;
    unsigned fn; /* seed table index + 1; 0 while empty */
    uint32_t supported[MASK_WORDS];
    bool used;
} region_t;
static region_t regions[REGION_SLOTS], *last_region;
static s5l8900_t *bound_machine;
static runner_fn runner;
static unsigned region_count;
/* Direct-mapped (tag, index) pairs read by generated code when control leaves
 * a region: tag is base | thumb << 1 | 1, slot is (pc >> 10) % LINK_SLOTS. */
static uint32_t link_table[LINK_SLOTS * 2u];
static uint64_t generated_retired, generated_calls;
unsigned arm_wasm_jit_enabled;

EM_JS(void, expose_jit_memory, (), { Module.jitMemory = wasmMemory; });

static region_t *region_find(uint32_t pc, unsigned thumb, bool create) {
    uint32_t base = pc & ~(REGION_BYTES - 1u);
    unsigned slot = (((pc >> 10) * 2u + thumb) * 0x9e3779b1u) >> (32u - REGION_SLOTS_LOG2);
    region_t *empty = NULL;
    if (last_region && last_region->fn && last_region->base == base && last_region->thumb == thumb)
        return last_region;
    for (unsigned n = 0; n < 16u; n++) {
        region_t *r = &regions[(slot + n) & (REGION_SLOTS - 1u)];
        if (!r->fn) { if (!empty) empty = r; if (!r->used) break; continue; }
        if (r->base == base && r->thumb == thumb) { last_region = r; return r; }
    }
    if (create && empty) {
        empty->base = base; empty->thumb = thumb; empty->used = true;
        return empty;
    }
    return NULL;
}

static void link_update(const region_t *r, bool live) {
    uint32_t *e = &link_table[((r->base >> 10) & (LINK_SLOTS - 1u)) * 2u];
    uint32_t tag = r->base | r->thumb << 1 | 1u;
    if (live && (!e[0] || e[0] == tag)) { e[0] = tag; e[1] = r->fn - 1u; }
    else if (!live && e[0] == tag) e[0] = e[1] = 0;
}

EMSCRIPTEN_KEEPALIVE void emu_jit_reset(void) {
    arm_wasm_jit_enabled = 0;
    memset(regions, 0, sizeof regions);
    memset(link_table, 0, sizeof link_table);
    last_region = NULL;
    runner = NULL;
    region_count = 0;
    generated_retired = generated_calls = 0;
}

void web_jit_bind(s5l8900_t *machine) {
    emu_jit_reset();
    bound_machine = machine;
    if (machine) expose_jit_memory();
}

EMSCRIPTEN_KEEPALIVE void emu_jit_enable(unsigned enabled) {
    arm_wasm_jit_enabled = enabled && bound_machine ? 1u : 0u;
}

EMSCRIPTEN_KEEPALIVE unsigned emu_jit_runner(runner_fn fn) {
    if (!bound_machine || !fn) return 0;
    runner = fn;
    return 1;
}

EMSCRIPTEN_KEEPALIVE uintptr_t emu_jit_link(void) { return (uintptr_t)link_table; }

EMSCRIPTEN_KEEPALIVE unsigned emu_jit_install(uint32_t pc, unsigned thumb,
                                              unsigned index, const uint32_t *mask) {
    if (!bound_machine || !mask || index >= REGION_CAP ||
        (pc & (REGION_BYTES - 1u)) || thumb > 1u)
        return 0;
    region_t *r = region_find(pc, thumb, false);
    if ((!r || !r->fn) && region_count >= REGION_CAP) return 0;
    if (!r) r = region_find(pc, thumb, true);
    if (!r) return 0;
    if (r->fn) link_update(r, false);
    else region_count++;
    r->fn = index + 1u;
    memcpy(r->supported, mask, sizeof r->supported);
    link_update(r, true);
    return 1;
}

/* Fetch-cache refill for generated region-to-region transitions. Generated
 * code never changes the CPU mode, so the privilege is the entry privilege. */
EMSCRIPTEN_KEEPALIVE unsigned emu_jit_refill(arm_cpu_t *c, uint32_t va) {
    return arm_fetch_cache_try_refill(c, va, (c->cpsr & 31u) != ARM_MODE_USR);
}

EMSCRIPTEN_KEEPALIVE void emu_jit_stale(uint32_t pc, unsigned thumb) {
    region_t *r = region_find(pc, thumb, false);
    if (!r || !r->fn) return;
    link_update(r, false);
    r->fn = 0;
    region_count--;
}

unsigned arm_wasm_jit_try(arm_cpu_t *c, unsigned limit) {
    if (!arm_wasm_jit_enabled || !bound_machine || !runner ||
        c != &bound_machine->cpu || !limit)
        return 0;
    unsigned total = 0;
    while (total < limit) {
        unsigned thumb = (c->cpsr & ARM_CPSR_T) != 0;
        uint32_t pc = c->r[15];
        region_t *r = region_find(pc, thumb, false);
        unsigned bit = (pc & (REGION_BYTES - 1u)) >> (thumb ? 1 : 2);
        if (!r || !(r->supported[bit >> 5] & (1u << (bit & 31u)))) break;
        if (c->abort_pending || !arm_mode_is_valid(c->cpsr) ||
            (c->fiq_line && !(c->cpsr & ARM_CPSR_F)) ||
            (c->irq_line && !(c->cpsr & ARM_CPSR_I)) ||
            (pc & (thumb ? 1u : 3u)) ||
            !arm_fetch_cache_try_refill(c, pc, (c->cpsr & 31u) != ARM_MODE_USR))
            break;
        generated_calls++;
        unsigned n = runner(c, limit - total, r->fn - 1u);
        c->cycles += n;
        generated_retired += n;
        if (!n) break;
        total += n;
    }
    return total;
}

/* Retained import ABI for the seed generator. Current emitted accesses inline
 * these same cache checks, including a stricter physical code-alias guard. */
EMSCRIPTEN_KEEPALIVE uintptr_t emu_jit_span(arm_cpu_t *c, uint32_t va,
                                           unsigned bytes, unsigned write,
                                           unsigned transactions) {
    if (!bytes || (bytes >= 4 && (va & 3u)) || (bytes == 2 && (va & 1u)) ||
        (va & 1023u) + bytes > 1024u) return 0;
    bool priv = (c->cpsr & 31u) != ARM_MODE_USR;
    unsigned slot = 2u * (((va >> 10) + (priv ? ARM_DREAD_SETS / 2u : 0u)) & (ARM_DREAD_SETS - 1u));
    uint32_t tag = (va & ~1023u) | (priv ? 1u : 0u);
    if (write) {
        if (!c->bus->host_ram_write) return 0;
        if (c->dwrite[slot].tag != tag || c->dwrite[slot].gen != c->tlb_gen) slot++;
        if (!c->dwrite[slot].host ||
            c->dwrite[slot].tag != tag || c->dwrite[slot].gen != c->tlb_gen) return 0;
        c->dwrite_hits += transactions;
        return (uintptr_t)(c->dwrite[slot].host + (va & 1023u));
    }
    if (c->dread[slot].tag != tag || c->dread[slot].gen != c->tlb_gen) slot++;
    if (!c->dread[slot].host || c->dread[slot].tag != tag || c->dread[slot].gen != c->tlb_gen)
        return 0;
    c->dread_hits += transactions;
    return (uintptr_t)(c->dread[slot].host + (va & 1023u));
}

EMSCRIPTEN_KEEPALIVE unsigned emu_jit_abi(void) { return JIT_ABI_REVISION; }
EMSCRIPTEN_KEEPALIVE unsigned emu_jit_offset(unsigned n) {
    switch (n) {
    case 0: return offsetof(arm_cpu_t, r);
    case 1: return offsetof(arm_cpu_t, cpsr);
    case 2: return offsetof(arm_cpu_t, fetch_host);
    case 3: return offsetof(arm_cpu_t, fetch_blk);
    case 4: return offsetof(arm_cpu_t, dread);
    case 5: return offsetof(arm_cpu_t, dwrite);
    case 6: return offsetof(arm_cpu_t, tlb_gen);
    case 7: return offsetof(arm_cpu_t, dread_hits);
    case 8: return offsetof(arm_cpu_t, dwrite_hits);
    case 9: return offsetof(arm_cpu_t, bus);
    case 10: return offsetof(arm_bus_t, host_ram_write);
    case 11: return sizeof ((arm_cpu_t *)0)->dwrite[0];
    case 12: return offsetof(arm_cpu_t, vfp_s);
    case 13: return offsetof(arm_cpu_t, vfp_fpexc);
    case 14: return offsetof(arm_cpu_t, cp15.cpacr);
    case 15: return offsetof(arm_cpu_t, vfp_fpscr);
    default: return 0;
    }
}
EMSCRIPTEN_KEEPALIVE double emu_jit_stat(unsigned n) {
    return n == 0 ? (double)generated_retired : n == 1 ? (double)generated_calls : region_count;
}
