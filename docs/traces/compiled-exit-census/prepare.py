from pathlib import Path
p=Path('.context/exit-census')
s=Path('tools/jit/compiler.cjs').read_text()
# Only diagnostic labels change; every exit keeps its original branch.
assert s.count('fail(6)')==3
for r in [8,9,10]:s=s.replace('fail(6)',f'fail({r})',1)
needle="body+=`(if(i32.eq ${L('why')}${C(5)})"
assert needle in s
s=s.replace(needle,"body+=`(call $census ${L('c')}${C(e.index)}${L('why')}${L('n')}${L('budget')})`;"+needle)
(p/'compiler.cjs').write_text(s)
s=Path('web/jit-runtime.c').read_text()
idx=s.index('unsigned arm_wasm_jit_try(')
helper='''
#define CENSUS_SLOTS (1u << 19)
typedef struct { uint32_t used, source, pc, thumb, reason, raw, valid, count, retired, zero; } census_row;
static census_row census_rows[CENSUS_SLOTS];
static uint64_t census_totals[40][4];
static unsigned census_enabled, census_overflow;
EMSCRIPTEN_KEEPALIVE void emu_census_reset(unsigned enabled) {
 memset(census_rows,0,sizeof(census_rows)); memset(census_totals,0,sizeof(census_totals));
 census_overflow=0; census_enabled=enabled;
}
EMSCRIPTEN_KEEPALIVE uintptr_t emu_census_rows(void) { return (uintptr_t)census_rows; }
EMSCRIPTEN_KEEPALIVE unsigned emu_census_overflow(void) { return census_overflow; }
EMSCRIPTEN_KEEPALIVE double emu_census_total(unsigned r,unsigned k) {return r<40&&k<4?(double)census_totals[r][k]:0;}
EMSCRIPTEN_KEEPALIVE void emu_census_exit(arm_cpu_t*c,unsigned source,unsigned reason,unsigned n,unsigned budget) {
 if(!census_enabled)return;
 if(reason>=40){census_overflow++;return;}
 census_totals[reason][0]++;census_totals[reason][1]+=n;census_totals[reason][2]+=!n;census_totals[reason][3]+=(n==budget);
 uint32_t pc=c->r[15],thumb=!!(c->cpsr&ARM_CPSR_T),raw=0,valid=0;
 if(c->fetch_host && c->fetch_gen==c->tlb_gen && c->fetch_priv==((c->cpsr&31u)!=ARM_MODE_USR) &&
 (pc&~1023u)==c->fetch_blk && !(pc&(thumb?1u:3u))) {
  memcpy(&raw,(uint8_t*)c->fetch_host+(pc&1023u),thumb?2:4);valid=1;
 }
 unsigned slot=(pc^(pc>>13)^(source*2654435761u)^(reason*2246822519u)^raw^(thumb<<9))&(CENSUS_SLOTS-1);
 for(unsigned i=0;i<CENSUS_SLOTS;i++,slot=(slot+1)&(CENSUS_SLOTS-1)) {
  census_row*r=&census_rows[slot];
  if(!r->used){*r=(census_row){1,source,pc,thumb,reason,raw,valid,0,0,0};}
  if(r->source==source&&r->pc==pc&&r->thumb==thumb&&r->reason==reason&&r->raw==raw&&r->valid==valid){r->count++;r->retired+=n;r->zero+=!n;return;}
 }
 census_overflow++;
}
static unsigned census_reject(arm_cpu_t*c,unsigned reason,unsigned limit) {
 emu_census_exit(c,0xffffffffu,reason,0,limit);return 0;
}
'''
s=s[:idx]+helper+s[idx:]
a=s.index('unsigned arm_wasm_jit_try(');b=s.index('\n/* Retained import',a)
s=s[:a]+'''unsigned arm_wasm_jit_try(arm_cpu_t *c, unsigned limit) {
 if(!arm_wasm_jit_enabled||!bound_machine||c!=&bound_machine->cpu||!limit)return census_reject(c,20,limit);
 unsigned thumb=!!(c->cpsr&ARM_CPSR_T);region_t*r=region_find(c->r[15],thumb,false);
 unsigned bit=(c->r[15]&1023u)>>(thumb?1:2);
 if(!r)return census_reject(c,21,limit);
 if(!(r->supported[bit>>5]&(1u<<(bit&31u))))return census_reject(c,22,limit);
 if(c->abort_pending)return census_reject(c,23,limit);
 if(!arm_mode_is_valid(c->cpsr))return census_reject(c,24,limit);
 if(c->fiq_line&&!(c->cpsr&ARM_CPSR_F))return census_reject(c,25,limit);
 if(c->irq_line&&!(c->cpsr&ARM_CPSR_I))return census_reject(c,26,limit);
 if(!c->fetch_host)return census_reject(c,27,limit);
 if(c->fetch_gen!=c->tlb_gen)return census_reject(c,28,limit);
 if(c->fetch_priv!=((c->cpsr&31u)!=ARM_MODE_USR))return census_reject(c,29,limit);
 if((c->r[15]&~1023u)!=c->fetch_blk)return census_reject(c,30,limit);
 if(c->r[15]&(thumb?1u:3u))return census_reject(c,31,limit);
 generated_calls++;
 unsigned n=r->fn(c,limit);c->cycles+=n;generated_retired+=n;return n;
}
'''+s[b:]
(p/'jit-runtime-source.c').write_text(s)
s=Path('.context/connected-engine/build-runtime.py').read_text().replace(".context/connected-engine",".context/exit-census")
a=s.index("src=Path('web/jit-runtime.c').read_text()");b=s.index("(p/'runtime').mkdir(exist_ok=True)")
s=s[:a]+"src=(p/'jit-runtime-source.c').read_text()\n(p/'jit-runtime.c').write_text(src);"+s[b:]
(p/'build-runtime.py').write_text(s)
