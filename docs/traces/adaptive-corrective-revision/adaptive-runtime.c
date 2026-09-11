/* Isolated ABI2 registry. No product binding or build policy is enabled here. */
#include "adaptive-runtime.h"
#include "arm_code_versions.h"
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
    unsigned count, index_next;
    arm_adaptive_fetch_witness_t witnesses[ADAPTIVE_WITNESS_CAP];
} adaptive_unit_t;

static arm_cpu_t *bound_cpu;
static adaptive_unit_t units[ADAPTIVE_UNIT_CAP], *active_unit;
static unsigned entry_buckets[ADAPTIVE_INDEX_BUCKETS];
_Static_assert((ADAPTIVE_INDEX_BUCKETS & (ADAPTIVE_INDEX_BUCKETS - 1u)) == 0,
               "entry index bucket count must be a power of two");
static arm_adaptive_fn retired[ADAPTIVE_RETIRED_CAP];
static unsigned unit_count, retired_count, failed, epoch_exhausted, active_code_written;
static uint32_t epoch, installed_bytes;
static arm_code_versions_t code_versions;
static adaptive_observer_fn miss_observer;
static unsigned observing;
static uint64_t statistics[ADAPTIVE_STAT_COUNT];
unsigned arm_adaptive_enabled;

static unsigned entry_bucket(uint32_t pc, unsigned thumb) {
    uint32_t key = pc ^ (thumb ? UINT32_C(0x9e3779b9) : 0u);
    key ^= key >> 16; key *= UINT32_C(0x7feb352d);
    key ^= key >> 15; key *= UINT32_C(0x846ca68b);
    key ^= key >> 16;
    return key & (ADAPTIVE_INDEX_BUCKETS - 1u);
}
static adaptive_unit_t *entry_find(uint32_t pc, unsigned thumb) {
    unsigned link = entry_buckets[entry_bucket(pc, thumb)];
    while (link) {
        adaptive_unit_t *unit = &units[link - 1u];
        if (unit->entry_pc == pc && unit->thumb == thumb) return unit;
        link = unit->index_next;
    }
    return NULL;
}
static void entry_insert(adaptive_unit_t *unit) {
    unsigned bucket = entry_bucket(unit->entry_pc, unit->thumb);
    unit->index_next = entry_buckets[bucket];
    entry_buckets[bucket] = (unsigned)(unit - units) + 1u;
}
static void entry_remove(adaptive_unit_t *unit) {
    unsigned *link = &entry_buckets[entry_bucket(unit->entry_pc, unit->thumb)];
    unsigned target = (unsigned)(unit - units) + 1u;
    while (*link != target) link = &units[*link - 1u].index_next;
    *link = unit->index_next;
}

static void fail(void) {
    failed = 1u;
    /* Keep the fatal dispatcher selected: disabling it would silently route
     * the next machine run through the interpreter after partial effects. */
    arm_adaptive_enabled = 1u;
}

static bool stat_add(unsigned index, uint32_t n) {
    const uint64_t maximum = UINT64_C(9007199254740991);
    if (statistics[index] > maximum - n) { fail(); return false; }
    statistics[index] += n;
    return true;
}
ADAPTIVE_EXPORT double adaptive_stat(unsigned index) {
    return index < ADAPTIVE_STAT_COUNT ? (double)statistics[index] : 0.0;
}
ADAPTIVE_EXPORT unsigned adaptive_observe(adaptive_observer_fn fn) {
    if (active_unit || observing) { fail(); return 0u; }
    if (failed || !bound_cpu || !epoch) return 0u;
    miss_observer = fn;
    return 1u;
}

ADAPTIVE_EXPORT unsigned adaptive_track_ram(uint8_t *ram, uint32_t bytes) {
    if (active_unit || observing) { fail(); return 0u; }
    if (failed || !bound_cpu || !epoch || unit_count || code_versions.versions)
        return 0u;
    return arm_code_versions_init(&code_versions, ram, bytes) ? 1u : 0u;
}
ADAPTIVE_EXPORT uint32_t adaptive_code_version(const uint8_t *host) {
    return arm_code_version(&code_versions, host);
}
ADAPTIVE_EXPORT void adaptive_ram_written(void *ctx, const uint8_t *host,
                                          uint32_t bytes) {
    if (observing) { fail(); return; }
    if (!code_versions.versions) return;
    if (!bound_cpu || !bound_cpu->bus || ctx != bound_cpu->bus->ctx ||
        !arm_code_versions_written(&code_versions, host, bytes)) fail();
}
static bool code_current(const arm_adaptive_fetch_witness_t *w) {
    return code_versions.versions ? w->code_version &&
        w->code_version == arm_code_version(&code_versions, w->host)
        : w->code_version == 0u;
}

/* Check lifetime/context before the pure current-mapping query; neither step
 * reads a witness's instruction bytes. Capture callers are trusted C/host code,
 * not guest addresses. Bind is mandatory before any RAM lifetime change. */
static bool witness_context_current(const arm_adaptive_fetch_witness_t *w) {
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
static bool witness_current(const arm_adaptive_fetch_witness_t *w) {
    return w && w->host && epoch && w->epoch == epoch && code_current(w) &&
        witness_context_current(w);
}

static bool refresh_installed(adaptive_unit_t *unit) {
    if (!stat_add(10u, 1u)) return false;
    arm_adaptive_fetch_witness_t staged[ADAPTIVE_WITNESS_CAP];
    if (!code_versions.versions) goto refused;
    /* Generation is the only permitted change. Reject incompatible immutable
     * context before asking for any fresh mapping, even if a later dependency
     * is the one that differs. Keep the complete fresh-witness comparison
     * below: this preflight changes cost, never re-admission policy. */
    const arm_cpu_t *cpu = bound_cpu;
    for (unsigned i = 0; i < unit->count; i++) {
        const arm_adaptive_fetch_witness_t *old = &unit->witnesses[i];
        if (old->epoch != epoch ||
            old->mode != (cpu->cpsr & ARM_CPSR_MODE_MASK) ||
            old->thumb != (cpu->cpsr & ARM_CPSR_T) ||
            old->sctlr != cpu->cp15.sctlr || old->ttbr0 != cpu->cp15.ttbr0 ||
            old->ttbr1 != cpu->cp15.ttbr1 || old->ttbcr != cpu->cp15.ttbcr ||
            old->dacr != cpu->cp15.dacr || old->fcse_pid != cpu->cp15.fcse_pid ||
            old->context_id != cpu->cp15.context_id) goto refused;
    }
    for (unsigned i = 0; i < unit->count; i++) {
        const arm_adaptive_fetch_witness_t *old = &unit->witnesses[i];
        arm_adaptive_fetch_witness_t *fresh = &staged[i];
        if (!old->code_version || !adaptive_capture_at(old->block, fresh) ||
            old->host != fresh->host || old->block != fresh->block ||
            old->epoch != fresh->epoch || old->mode != fresh->mode ||
            old->thumb != fresh->thumb || old->sctlr != fresh->sctlr ||
            old->ttbr0 != fresh->ttbr0 || old->ttbr1 != fresh->ttbr1 ||
            old->ttbcr != fresh->ttbcr || old->dacr != fresh->dacr ||
            old->fcse_pid != fresh->fcse_pid ||
            old->context_id != fresh->context_id ||
            old->code_version != fresh->code_version) goto refused;
    }
    if (!stat_add(11u, 1u)) return false;
    memcpy(unit->witnesses, staged, unit->count * sizeof *staged);
    return true;
refused:
    (void)stat_add(12u, 1u);
    return false;
}

static bool retire_unit(adaptive_unit_t *unit) {
    if (!unit->fn) return true;
    if (retired_count == ADAPTIVE_RETIRED_CAP) return false;
    entry_remove(unit);
    retired[retired_count++] = unit->fn;
    installed_bytes -= unit->binary_bytes;
    unit_count--;
    memset(unit, 0, sizeof *unit);
    return true;
}

ADAPTIVE_EXPORT void adaptive_bind(arm_cpu_t *cpu) {
    if (active_unit || observing) { fail(); return; }
    arm_adaptive_enabled = 0u;
    if (epoch_exhausted || epoch == UINT32_MAX) {
        epoch_exhausted = 1u;
        fail();
        return;
    }
    /* Invalidate the lifetime even if lack of retirement capacity prevents
     * this bind. Never leave old code executable after a failed reset. */
    epoch++;
    arm_code_versions_clear(&code_versions);
    if (unit_count > ADAPTIVE_RETIRED_CAP - retired_count) {
        fail();
        return;
    }
    for (unsigned i = 0; i < ADAPTIVE_UNIT_CAP; i++)
        (void)retire_unit(&units[i]);
    memset(entry_buckets, 0, sizeof entry_buckets);
    bound_cpu = cpu;
    active_code_written = 0u;
    miss_observer = NULL;
    memset(statistics, 0, sizeof statistics);
    failed = 0u;
}

int arm_adaptive_forget(arm_cpu_t *cpu) {
    if (!cpu || cpu != bound_cpu) return 1;
    adaptive_bind(NULL);
    return failed ? 0 : 1;
}

ADAPTIVE_EXPORT uint32_t adaptive_epoch(void) { return epoch; }
ADAPTIVE_EXPORT unsigned adaptive_failed(void) { return failed; }
ADAPTIVE_EXPORT unsigned adaptive_units(void) { return unit_count; }

ADAPTIVE_EXPORT unsigned adaptive_capture(arm_adaptive_fetch_witness_t *out) {
    if (active_unit) { fail(); return 0u; }
    if (failed || !bound_cpu) return 0u;
    if (!arm_adaptive_capture_fetch(bound_cpu, epoch, out)) return 0u;
    if (code_versions.versions) {
        out->code_version = arm_code_version(&code_versions, out->host);
        if (!out->code_version) return 0u;
    }
    return 1u;
}

ADAPTIVE_EXPORT unsigned adaptive_capture_at(uint32_t pc,
        arm_adaptive_fetch_witness_t *out) {
    if (active_unit) { fail(); return 0u; }
    if (failed || !bound_cpu) return 0u;
    if (!arm_adaptive_capture_fetch_at(bound_cpu, epoch, pc, out)) return 0u;
    if (code_versions.versions) {
        out->code_version = arm_code_version(&code_versions, out->host);
        if (!out->code_version) return 0u;
    }
    return 1u;
}

ADAPTIVE_EXPORT void adaptive_enable(unsigned enabled) {
    if (active_unit || observing) { fail(); return; }
    if (failed) { arm_adaptive_enabled = 1u; return; }
    if (!enabled) {
        /* Another tier may perform stores without version notifications. Drop
         * captured and installed ownership before allowing that transition. */
        /* Captured work can exist outside this registry even with no unit or
         * version table yet. Every disable of a bound CPU cancels that epoch. */
        if (bound_cpu) adaptive_bind(bound_cpu);
        return;
    }
    arm_adaptive_enabled = bound_cpu && epoch ? 1u : 0u;
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
    entry_insert(destination);
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
    if (!active_unit || active_code_written || failed || !bound_cpu) return 0u;
    for (unsigned i = 0; i < active_unit->count; i++) {
        const arm_adaptive_fetch_witness_t *w = &active_unit->witnesses[i];
        if (w->block == (pc & ~1023u)) {
            if (!code_current(w) ||
                !arm_adaptive_guard_instruction(bound_cpu, w, epoch, pc, raw, width))
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

ADAPTIVE_EXPORT uintptr_t adaptive_read(uint32_t va, unsigned width,
                                        unsigned privileged) {
    if (!active_unit || active_code_written || failed || !bound_cpu || privileged > 1u ||
        !arm_adaptive_cpu_eligible(bound_cpu)) return 0;
    /* A user-mode unit cannot request a privileged lookup. Privileged code
     * may explicitly use the unprivileged LDRT/LDRBT form. */
    if (privileged && (bound_cpu->cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR)
        return 0;
    return (uintptr_t)arm_read_cache_try(bound_cpu, va, width, privileged != 0u);
}

ADAPTIVE_EXPORT unsigned adaptive_write(uint32_t va, unsigned width,
                                         unsigned privileged, uint32_t value) {
    if (!active_unit || active_code_written || failed || !bound_cpu ||
        privileged > 1u || !arm_adaptive_cpu_eligible(bound_cpu) ||
        (privileged && (bound_cpu->cpsr & ARM_CPSR_MODE_MASK) == ARM_MODE_USR))
        return 0u;
    const uint8_t *host = arm_write_cache_try(bound_cpu, va, width,
                                             privileged != 0u, value);
    if (!host) return 0u;
    if (failed) return 2u; /* committed effect, never a retryable refusal */
    uintptr_t first = (uintptr_t)host, last = first + width - 1u;
    for (unsigned i = 0; i < active_unit->count; i++) {
        uintptr_t code = (uintptr_t)active_unit->witnesses[i].host;
        if (first <= code + 1023u && last >= code) {
            active_code_written = 1u;
            return 2u;
        }
    }
    return 1u;
}

static void invoke_pinned(arm_cpu_t *cpu, uint32_t budget,
                           arm_adaptive_result_t *result) {
    active_unit->fn(cpu, budget, result);
    /* A callback may have attempted a forbidden registry mutation. Make the
     * bridge reject before it credits cycles; never silently accept that call. */
    if (failed || (active_code_written && (!result->retired ||
                  (result->reason != ADAPT_EXIT_BOUNDARY &&
                   result->reason != ADAPT_EXIT_STALE))))
        result->reason = ADAPT_EXIT_INVALID;
}

static int observe_miss(arm_cpu_t *cpu, unsigned thumb) {
    if (retired_count == ADAPTIVE_RETIRED_CAP) return 0;
    if (!stat_add(2u, 1u)) return -1;
    if (miss_observer) {
        observing = 1u;
        miss_observer(cpu->r[15], thumb);
        observing = 0u;
    }
    return failed ? -1 : 0;
}

int arm_adaptive_try(arm_cpu_t *cpu, uint32_t budget,
                     arm_adaptive_result_t *result) {
    if (active_unit || observing) { fail(); return -1; }
    if (failed) return -1;
    if (!arm_adaptive_enabled || !cpu || cpu != bound_cpu || !budget) return 0;
    if (!result) { fail(); return -1; }
    if (!arm_adaptive_cpu_eligible(cpu)) return 0;
    unsigned thumb = (cpu->cpsr & ARM_CPSR_T) != 0u;
    if (cpu->r[15] & (thumb ? 1u : 3u)) return 0;
    /* Keep one retirement slot available for a possible STALE exit. A full
     * queue safely uses the reference tier until the owner drains it. */
    if (retired_count == ADAPTIVE_RETIRED_CAP) return 0;
    adaptive_unit_t *selected = entry_find(cpu->r[15], thumb);
    if (!selected) return observe_miss(cpu, thumb);
    for (unsigned i = 0; i < selected->count; i++) {
        if (!code_current(&selected->witnesses[i])) {
            if (!retire_unit(selected)) { fail(); return -1; }
            return observe_miss(cpu, thumb);
        }
        /* code_current just succeeded, with no intervening callbacks/writes. */
        if (!witness_context_current(&selected->witnesses[i])) {
            if (!refresh_installed(selected))
                return failed ? -1 : observe_miss(cpu, thumb);
            break; /* The staged refresh checked every dependency. */
        }
    }
    if (!stat_add(1u, 1u)) return -1;
    active_unit = selected;
    active_code_written = 0u;
    int status = arm_adaptive_invoke(cpu, invoke_pinned, budget, selected->count, result);
    active_unit = NULL;
    unsigned code_written = active_code_written;
    active_code_written = 0u;
    if (status != 1 || failed) {
        (void)stat_add(3u + ADAPT_EXIT_INVALID, 1u);
        fail(); return -1;
    }
    if (!stat_add(0u, result->retired) ||
        !stat_add(3u + result->reason, 1u)) return -1;
    if ((result->reason == ADAPT_EXIT_STALE || code_written) && !retire_unit(selected)) {
        fail();
        return -1;
    }
    return 1;
}
