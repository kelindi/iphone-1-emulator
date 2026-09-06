from pathlib import Path
import difflib
p=Path('.context/jit-continuation');old=(p/'original-runtime.c').read_text();start=old.index('unsigned arm_wasm_jit_try(');end=old.index('\n/* Retained import ABI',start)
new='''unsigned arm_wasm_jit_try(arm_cpu_t *c, unsigned limit) {
    if (!arm_wasm_jit_enabled || !bound_machine || c != &bound_machine->cpu || !limit)
        return 0;
    unsigned total = 0u;
    do {
        unsigned thumb = (c->cpsr & ARM_CPSR_T) != 0;
        region_t *r = region_find(c->r[15], thumb, false);
        unsigned bit = (c->r[15] & (REGION_BYTES - 1u)) >> (thumb ? 1 : 2);
        if (!r || !(r->supported[bit >> 5] & (1u << (bit & 31u)))) break;
        if (c->abort_pending || !arm_mode_is_valid(c->cpsr) ||
            (c->fiq_line && !(c->cpsr & ARM_CPSR_F)) ||
            (c->irq_line && !(c->cpsr & ARM_CPSR_I)) ||
            (c->r[15] & (thumb ? 1u : 3u))) break;
        const bool priv = (c->cpsr & 31u) != ARM_MODE_USR;
        if (!c->fetch_host || c->fetch_gen != c->tlb_gen ||
            c->fetch_priv != priv ||
            (c->r[15] & ~(REGION_BYTES - 1u)) != c->fetch_blk) {
            /* Only a committed cross-region prefix may continue. Never walk
             * page tables, touch MMIO, or admit a fault speculatively. */
            if (!total || !arm_fetch_cache_try_refill(c, c->r[15], priv)) break;
        }
        const uint32_t source = r->base;
        const unsigned source_thumb = r->thumb;
        generated_calls++;
        unsigned n = r->fn(c, limit - total);
        c->cycles += n;
        generated_retired += n;
        total += n;
        /* Same-region exits are real fallback reasons, not a retry request.
         * Every successful continuation retains the original device budget. */
        if (!n || ((c->r[15] & ~(REGION_BYTES - 1u)) == source &&
                   ((c->cpsr & ARM_CPSR_T) != 0) == source_thumb)) break;
    } while (total < limit);
    return total;
}
'''
s=old[:start]+new+old[end:];(p/'jit-runtime.c').write_text(s);(p/'runtime.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='web/jit-runtime.c',tofile='candidate/jit-runtime.c')))
