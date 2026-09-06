from pathlib import Path
p=Path('.context/jit-continuation');s=Path('.context/jit-instruction-guards/oracle.c').read_text()
s=s.replace('#include "soc.h"','#define arm_wasm_jit_try candidate_try\n#include "jit-runtime.c"\n#undef arm_wasm_jit_try\n#include "soc.h"\n#include "snapshot.h"\n#include <stdlib.h>')
old=(p/'original-runtime.c').read_text();a=old.index('unsigned arm_wasm_jit_try(');b=old.index('\n/* Retained import ABI',a);s=s.replace('static s5l8900_t machine;',old[a:b].replace('arm_wasm_jit_try','original_try')+'\nstatic s5l8900_t machine;')
s=s.replace('compiled?arm_wasm_jit_try(&machine.cpu,left):0','compiled?(compiled==1?original_try(&machine.cpu,left):candidate_try(&machine.cpu,left)):0')
s=s.replace('return arm_wasm_jit_try(&machine.cpu,budget);','return candidate_try(&machine.cpu,budget);')
s+='''
static bool choose_original;
unsigned arm_wasm_jit_try(arm_cpu_t *c,unsigned limit) {return choose_original?original_try(c,limit):candidate_try(c,limit);}

EMSCRIPTEN_KEEPALIVE unsigned review_prime(unsigned pc,unsigned thumb) {
 arm_cpu_t *c=&machine.cpu;uint32_t pa,old,savepc=c->r[15],savecpsr=c->cpsr;uint64_t cycles=c->cycles;
 uint32_t fault=arm_mmu_translate(c,pc,ARM_ACCESS_FETCH,true,&pa);if(fault)return 1;
 memcpy(&old,machine.ram+pa,4);uint32_t nop=thumb?0x46c0u:0xe1a00000u;memcpy(machine.ram+pa,&nop,thumb?2:4);
 c->r[15]=pc;c->cpsr=(c->cpsr&~ARM_CPSR_T)|(thumb?ARM_CPSR_T:0);unsigned result=arm_step(c);
 memcpy(machine.ram+pa,&old,4);c->r[15]=savepc;c->cpsr=savecpsr;c->cycles=cycles;return result!=ARM_OK;
}
static unsigned guard_matches;
EMSCRIPTEN_KEEPALIVE void review_guard(unsigned which) {
 guard_matches=0;
 arm_cpu_t *c=&machine.cpu;
 switch(which) {
 case 1:c->cp15.dacr=0;break; /* Stamp mutation: no speculative walk/flush. */
 case 2:arm_mmu_tlb_flush(c);break;
 case 3:c->irq_line=true;c->cpsr&=~ARM_CPSR_I;break;
 case 4:c->fiq_line=true;c->cpsr&=~ARM_CPSR_F;break;
 case 5:c->cpsr=(c->cpsr&~31u)|0u;break;
 case 6:c->abort_pending=true;break;
 case 7:c->fetch_priv=false;break;
 case 8:c->r[15]|=2u;break;
 case 9:c->r[15]|=1u;break;
 case 10:c->fetch_host=NULL;break;
 case 11:c->tlb_gen=0;break;
 case 12:c->cp15.ttbr0+=0x4000;break;
 case 14:machine.bus.host_ram=NULL;break;
 case 15:c->cpsr=(c->cpsr&~31u)|ARM_MODE_USR;break;
 case 13:for(unsigned i=0;i<ARM_TLB_ENTRIES;i++)if(((c->tlb[i].tag>>3)<<10)==0x400u){c->tlb[i].fsr=13u;guard_matches++;}break;
 case 16:{uint32_t desc;memcpy(&desc,machine.ram+0x4004,4);desc|=16u;memcpy(machine.ram+0x4004,&desc,4);c->tlb[(1024u+ARM_ACCESS_FETCH*(ARM_TLB_ENTRIES/4u))&(ARM_TLB_ENTRIES-1u)].gen=0;break;}
 }
}
EMSCRIPTEN_KEEPALIVE void review_timer(unsigned phase,unsigned ticks,unsigned unmask) {
 machine.cpu_hz=4;machine.tb_hz=1;machine.pmu.tick_hz=1;machine.tb_accum=phase;
 machine.bus.write32(machine.bus.ctx,S5L8900_TIMER_BASE+TIMER4_COUNTBUF,ticks);
 machine.bus.write32(machine.bus.ctx,S5L8900_TIMER_BASE+TIMER4_STATE,TIMER4_STATE_START|TIMER4_STATE_UPDATE);
 machine.bus.write32(machine.bus.ctx,S5L8900_VIC0_BASE+VIC_INTENABLE,1u<<S5L8900_IRQ_TIMER);
 if(unmask)machine.cpu.cpsr&=~ARM_CPSR_I;s5l8900_tick(&machine,0);
}
EMSCRIPTEN_KEEPALIVE unsigned review_machine_run(unsigned count,unsigned engine) {
 arm_status_t st=ARM_OK;unsigned done=0;arm_wasm_jit_enabled=engine!=0;choose_original=engine==1;
 if(engine)done=s5l8900_run(&machine,count,&st);
 else for(;done<count;done++){st=arm_step(&machine.cpu);if(st!=ARM_OK)break;s5l8900_tick(&machine,1);}
 status=st;return done;
}
static uint8_t *snapshot_bytes;static size_t snapshot_length;
EMSCRIPTEN_KEEPALIVE uintptr_t review_snapshot(void) {
 free(snapshot_bytes);snapshot_bytes=NULL;snapshot_length=0;
 if(snapshot_save_mem(&machine,&snapshot_bytes,&snapshot_length)!=SNAP_OK)return 0;
 return (uintptr_t)snapshot_bytes;
}
EMSCRIPTEN_KEEPALIVE unsigned review_snapshot_size(void){return snapshot_length;}
EMSCRIPTEN_KEEPALIVE unsigned review_value(unsigned n) {return n==0?machine.cpu.cp15.ifar:n==1?machine.cpu.cp15.ifsr:guard_matches;}
EMSCRIPTEN_KEEPALIVE unsigned review_control_try(unsigned budget) {arm_wasm_jit_enabled=1;return original_try(&machine.cpu,budget);}
'''
(p/'oracle.c').write_text(s)
