from pathlib import Path
import hashlib,json
root=Path.cwd(); out=root/'.context/jit-fallback-census'; proofs=[]
def project(src, dst, replacements, append=''):
    original=(root/src).read_text(); text=original
    for old,new,count in replacements:
        assert text.count(old)==count,(src,old,text.count(old),count)
        text=text.replace(old,new)
    text+=append
    restored=text[:-len(append)] if append else text
    for old,new,count in reversed(replacements):
        assert restored.count(new)==count,(src,new,restored.count(new),count)
        restored=restored.replace(new,old)
    assert restored==original
    (out/dst).write_text(text)
    proofs.append(dict(source=src,output=dst,sourceSha256=hashlib.sha256(original.encode()).hexdigest(),candidateSha256=hashlib.sha256(text.encode()).hexdigest(),inverseProjectionByteExact=True,replacements=len(replacements)))
# The complete existing function body and every return are preserved. The new
# wrapper observes only entry values, fetched locals, original outcomes.
project('core/src/arm/arm_interp.c','arm_interp.c',[
 ('arm_status_t arm_step(arm_cpu_t *c) {','#include "census.h"\nstatic arm_status_t census_original_arm_step(arm_cpu_t *c) {',1),
 ('        uint32_t tnext = pc + 2;','        fc_fetch(tinsn);\n        uint32_t tnext = pc + 2;',1),
 ('    uint32_t next = pc + 4;','    fc_fetch(insn);\n    uint32_t next = pc + 4;',1),
 ('        take_exception(c, ARM_VEC_FIQ, ARM_MODE_FIQ, pc + 4, true, &vec);','        fc_event(FC_FIQ);\n        take_exception(c, ARM_VEC_FIQ, ARM_MODE_FIQ, pc + 4, true, &vec);',1),
 ('        take_exception(c, ARM_VEC_IRQ, ARM_MODE_IRQ, pc + 4, false, &vec);','        fc_event(FC_IRQ);\n        take_exception(c, ARM_VEC_IRQ, ARM_MODE_IRQ, pc + 4, false, &vec);',1),
 ('take_pending_data_abort(c, pc);','fc_event(FC_DATA_ABORT); take_pending_data_abort(c, pc);',8),
 ('            c->cp15.ifsr = fetch_fsr;','            fc_event(FC_PREFETCH);\n            c->cp15.ifsr = fetch_fsr;',1),
],append='''\narm_status_t arm_step(arm_cpu_t *c) {
    fc_begin(c->r[15], (c->cpsr & ARM_CPSR_T) != 0);
    arm_status_t result = census_original_arm_step(c);
    fc_end((unsigned)result, result == ARM_OK);
    return result;
}
''')
# ARM_OK plus a fetched opcode is a completed interpreter step, not proof that
# an instruction committed without exception. Data aborts retain their event flag.
guard='''    if (c->abort_pending || !arm_mode_is_valid(c->cpsr) ||
        (c->fiq_line && !(c->cpsr & ARM_CPSR_F)) ||
        (c->irq_line && !(c->cpsr & ARM_CPSR_I)) ||
        !c->fetch_host || c->fetch_gen != c->tlb_gen ||
        c->fetch_priv != ((c->cpsr & 31u) != ARM_MODE_USR) ||
        (c->r[15] & ~(REGION_BYTES - 1u)) != c->fetch_blk ||
        (c->r[15] & (thumb ? 1u : 3u))) return 0;'''
newguard=guard[:-len('return 0;')]+'''{
        unsigned flags = (!!c->abort_pending) | ((!arm_mode_is_valid(c->cpsr)) << 1) |
          ((c->fiq_line && !(c->cpsr & ARM_CPSR_F)) << 2) |
          ((c->irq_line && !(c->cpsr & ARM_CPSR_I)) << 3) |
          ((!c->fetch_host) << 4) | ((c->fetch_gen != c->tlb_gen) << 5) |
          ((c->fetch_priv != ((c->cpsr & 31u) != ARM_MODE_USR)) << 6) |
          (((c->r[15] & ~(REGION_BYTES-1u)) != c->fetch_blk) << 7) |
          ((!!(c->r[15] & (thumb ? 1u : 3u))) << 8);
        fc_attempt(FC_RUNTIME_GUARD,c->r[15],thumb,flags); return 0;
    }'''
project('web/jit-runtime.c','jit-runtime.c',[
 ('#include "jit-runtime.h"','#include "jit-runtime.h"\n#include "census.h"',1),
 ('    arm_wasm_jit_enabled = 0;','    fc_reset();\n    arm_wasm_jit_enabled = 0;',1),
 ('    memcpy(r->supported, mask, sizeof r->supported);','    memcpy(r->supported, mask, sizeof r->supported);\n    fc_install(pc,thumb,mask);',1),
 ('    r->fn = NULL;','    fc_stale(pc,thumb);\n    r->fn = NULL;',1),
 ('    if (!arm_wasm_jit_enabled || !bound_machine || c != &bound_machine->cpu || !limit)\n        return 0;', '    if (!arm_wasm_jit_enabled || !bound_machine || c != &bound_machine->cpu || !limit) {\n        fc_attempt(FC_DISABLED,c->r[15],!!(c->cpsr & ARM_CPSR_T),0); return 0;\n    }',1),
 ('    if (!r || !(r->supported[bit >> 5] & (1u << (bit & 31u)))) return 0;', '    if (!r || !(r->supported[bit >> 5] & (1u << (bit & 31u)))) {\n        fc_attempt(r?FC_NO_BIT:FC_NO_REGION,c->r[15],thumb,0); return 0;\n    }',1),
 (guard,newguard,1),
 ('    generated_retired += n;','    generated_retired += n;\n    fc_attempt(n == limit ? FC_NONE : n ? FC_GENERATED_PREFIX : !r->fn ? FC_STALE_ZERO : FC_GENERATED_ZERO, c->r[15], !!(c->cpsr & ARM_CPSR_T), 0);',1),
],append='''\nEMSCRIPTEN_KEEPALIVE void emu_fc_start(void) { fc_start(); }
EMSCRIPTEN_KEEPALIVE void emu_fc_stop(void) { fc_stop(); }
EMSCRIPTEN_KEEPALIVE uintptr_t emu_fc_rows(void) { return (uintptr_t)fc_rows(); }
EMSCRIPTEN_KEEPALIVE uintptr_t emu_fc_summary(void) { return (uintptr_t)fc_summary(); }
EMSCRIPTEN_KEEPALIVE unsigned emu_fc_capacity(void) { return fc_capacity(); }
''')
(out/'projection.json').write_text(json.dumps(proofs,indent=2)+'\n')
