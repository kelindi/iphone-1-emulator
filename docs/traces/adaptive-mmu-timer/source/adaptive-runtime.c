/* Isolated ABI2 registry. No product binding or build policy is enabled here. */
#include "adaptive-runtime.h"
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define ADAPTIVE_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define ADAPTIVE_EXPORT
#endif

typedef struct {
    arm_adaptive_fn fn;
    uint32_t entry_pc, thumb, binary_bytes;
    unsigned count;
    arm_adaptive_fetch_witness_t witnesses[ADAPTIVE_WITNESS_CAP];
} adaptive_unit_t;

static arm_cpu_t *bound_cpu;
static adaptive_unit_t units[ADAPTIVE_UNIT_CAP], *active_unit;
static arm_adaptive_fn retired[ADAPTIVE_RETIRED_CAP];
static unsigned unit_count, retired_count, failed, epoch_exhausted;
static uint32_t epoch, installed_bytes;
unsigned arm_adaptive_enabled;

static void fail(void) {
    failed = 1u;
    /* Keep the fatal dispatcher selected: disabling it would silently route
     * the next machine run through the interpreter after partial effects. */
    arm_adaptive_enabled = 1u;
}

/* Check lifetime/context before the pure current-mapping query; neither step
 * reads a witness's instruction bytes. Capture callers are trusted C/host code,
 * not guest addresses. Bind is mandatory before any RAM lifetime change. */
static bool witness_current(const arm_adaptive_fetch_witness_t *w) {
    const arm_cpu_t *c = bound_cpu;
    return w && w->host && epoch && w->epoch == epoch &&
        !(w->block & 1023u) && arm_adaptive_cpu_eligible(c) &&
        w->generation == c->tlb_gen &&
        w->mode == (c->cpsr & ARM_CPSR_MODE_MASK) &&
        w->thumb == (c->cpsr & ARM_CPSR_T) &&
        w->sctlr == c->cp15.sctlr && w->ttbr0 == c->cp15.ttbr0 &&
        w->ttbr1 == c->cp15.ttbr1 && w->ttbcr == c->cp15.ttbcr &&
        w->dacr == c->cp15.dacr && w->fcse_pid == c->cp15.fcse_pid &&
        w->context_id == c->cp15.context_id &&
        arm_fetch_cache_peek(c, w->block, w->mode != ARM_MODE_USR) == w->host;
}

static bool retire_unit(adaptive_unit_t *unit) {
    if (!unit->fn) return true;
    if (retired_count == ADAPTIVE_RETIRED_CAP) return false;
    retired[retired_count++] = unit->fn;
    installed_bytes -= unit->binary_bytes;
    unit_count--;
    memset(unit, 0, sizeof *unit);
    return true;
}

ADAPTIVE_EXPORT void adaptive_bind(arm_cpu_t *cpu) {
    if (active_unit) { fail(); return; }
    arm_adaptive_enabled = 0u;
    if (epoch_exhausted || epoch == UINT32_MAX) {
        epoch_exhausted = 1u;
        fail();
        return;
    }
    /* Invalidate the lifetime even if lack of retirement capacity prevents
     * this bind. Never leave old code executable after a failed reset. */
    epoch++;
    if (unit_count > ADAPTIVE_RETIRED_CAP - retired_count) {
        fail();
        return;
    }
    for (unsigned i = 0; i < ADAPTIVE_UNIT_CAP; i++)
        (void)retire_unit(&units[i]);
    bound_cpu = cpu;
    failed = 0u;
}

ADAPTIVE_EXPORT uint32_t adaptive_epoch(void) { return epoch; }
ADAPTIVE_EXPORT unsigned adaptive_failed(void) { return failed; }
ADAPTIVE_EXPORT unsigned adaptive_units(void) { return unit_count; }

ADAPTIVE_EXPORT unsigned adaptive_capture(arm_adaptive_fetch_witness_t *out) {
    if (active_unit) { fail(); return 0u; }
    if (failed || !bound_cpu) return 0u;
    return arm_adaptive_capture_fetch(bound_cpu, epoch, out) ? 1u : 0u;
}

ADAPTIVE_EXPORT void adaptive_enable(unsigned enabled) {
    if (active_unit) { fail(); return; }
    if (failed) { arm_adaptive_enabled = 1u; return; }
    arm_adaptive_enabled = enabled && bound_cpu && !failed && epoch ? 1u : 0u;
}

ADAPTIVE_EXPORT unsigned adaptive_install(uint32_t entry_pc, unsigned thumb,
        arm_adaptive_fn fn, const arm_adaptive_fetch_witness_t *witnesses,
        unsigned count, uint32_t captured_epoch, uint32_t binary_bytes) {
    if (active_unit) { fail(); return 0u; }
    if (failed || !bound_cpu || !fn || !witnesses || !count ||
        count > ADAPTIVE_WITNESS_CAP || captured_epoch != epoch || !epoch ||
        thumb > 1u || (entry_pc & (thumb ? 1u : 3u)) ||
        !binary_bytes || binary_bytes > ADAPTIVE_BINARY_CAP)
        return 0u;
    bool has_entry = false;
    for (unsigned i = 0; i < count; i++) {
        const arm_adaptive_fetch_witness_t *w = &witnesses[i];
        if (!witness_current(w) || w->thumb != (thumb ? ARM_CPSR_T : 0u))
            return 0u;
        if (w->block == (entry_pc & ~1023u)) has_entry = true;
        for (unsigned j = 0; j < i; j++)
            if (witnesses[j].block == w->block) return 0u;
    }
    if (!has_entry) return 0u;
    adaptive_unit_t *replace = NULL, *empty = NULL;
    for (unsigned i = 0; i < ADAPTIVE_UNIT_CAP; i++) {
        adaptive_unit_t *u = &units[i];
        if (!u->fn) { if (!empty) empty = u; continue; }
        if (u->fn == fn) return 0u; /* one host slot has exactly one owner */
        if (u->entry_pc == entry_pc && u->thumb == thumb) replace = u;
    }
    for (unsigned i = 0; i < retired_count; i++)
        if (retired[i] == fn) return 0u; /* pending host removal still owns it */
    if ((!replace && (!empty || unit_count == ADAPTIVE_UNIT_CAP)) ||
        (replace && retired_count == ADAPTIVE_RETIRED_CAP)) return 0u;
    uint32_t retained_bytes = installed_bytes - (replace ? replace->binary_bytes : 0u);
    if (binary_bytes > ADAPTIVE_BINARY_CAP - retained_bytes) return 0u;
    adaptive_unit_t staged = {0};
    staged.fn = fn;
    staged.entry_pc = entry_pc;
    staged.thumb = thumb;
    staged.binary_bytes = binary_bytes;
    staged.count = count;
    memcpy(staged.witnesses, witnesses, count * sizeof *witnesses);
    adaptive_unit_t *destination = replace ? replace : empty;
    if (replace && !retire_unit(replace)) { fail(); return 0u; }
    *destination = staged;
    installed_bytes += binary_bytes;
    unit_count++;
    return 1u;
}

ADAPTIVE_EXPORT unsigned adaptive_retired(arm_adaptive_fn *out, unsigned capacity) {
    if (active_unit) { fail(); return 0u; }
    if (!out || !capacity) return 0u;
    unsigned n = capacity < retired_count ? capacity : retired_count;
    memcpy(out, retired, n * sizeof *out);
    retired_count -= n;
    memmove(retired, retired + n, retired_count * sizeof *retired);
    memset(retired + retired_count, 0, n * sizeof *retired);
    return n;
}

ADAPTIVE_EXPORT unsigned adaptive_guard(uint32_t pc, uint32_t raw, unsigned width) {
    if (!active_unit || failed || !bound_cpu) return 0u;
    for (unsigned i = 0; i < active_unit->count; i++) {
        const arm_adaptive_fetch_witness_t *w = &active_unit->witnesses[i];
        if (w->block == (pc & ~1023u)) {
            if (!arm_adaptive_guard_instruction(bound_cpu, w, epoch, pc, raw, width))
                return 0u;
            /* Reproduce the displaced interpreter fetch's cache/TLB accounting
             * only AFTER the pure mapping/literal check. This cannot walk or
             * fault. A following slow instruction sees this same warm fetch,
             * so its lookup does not double-count the hit. */
            return arm_fetch_cache_try_refill(bound_cpu, pc,
                (bound_cpu->cpsr & ARM_CPSR_MODE_MASK) != ARM_MODE_USR) ? 1u : 0u;
        }
    }
    return 0u;
}

static void invoke_pinned(arm_cpu_t *cpu, uint32_t budget,
                           arm_adaptive_result_t *result) {
    active_unit->fn(cpu, budget, result);
    /* A callback may have attempted a forbidden registry mutation. Make the
     * bridge reject before it credits cycles; never silently accept that call. */
    if (failed) result->reason = ADAPT_EXIT_INVALID;
}

int arm_adaptive_try(arm_cpu_t *cpu, uint32_t budget,
                     arm_adaptive_result_t *result) {
    if (active_unit) { fail(); return -1; }
    if (failed) return -1;
    if (!arm_adaptive_enabled || !cpu || cpu != bound_cpu || !budget) return 0;
    if (!result) { fail(); return -1; }
    if (!arm_adaptive_cpu_eligible(cpu)) return 0;
    unsigned thumb = (cpu->cpsr & ARM_CPSR_T) != 0u;
    if (cpu->r[15] & (thumb ? 1u : 3u)) return 0;
    /* Keep one retirement slot available for a possible STALE exit. A full
     * queue safely uses the reference tier until the owner drains it. */
    if (retired_count == ADAPTIVE_RETIRED_CAP) return 0;
    adaptive_unit_t *selected = NULL;
    for (unsigned i = 0; i < ADAPTIVE_UNIT_CAP; i++)
        if (units[i].fn && units[i].entry_pc == cpu->r[15] && units[i].thumb == thumb) {
            selected = &units[i];
            break;
        }
    if (!selected) return 0;
    for (unsigned i = 0; i < selected->count; i++)
        if (!witness_current(&selected->witnesses[i])) return 0;
    active_unit = selected;
    int status = arm_adaptive_invoke(cpu, invoke_pinned, budget, selected->count, result);
    active_unit = NULL;
    if (status != 1 || failed) { fail(); return -1; }
    if (result->reason == ADAPT_EXIT_STALE && !retire_unit(selected)) {
        fail();
        return -1;
    }
    return 1;
}
