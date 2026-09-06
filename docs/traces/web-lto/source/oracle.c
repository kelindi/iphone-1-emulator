#ifdef FC_ORACLE
#include "jit-runtime.c"
#else
#include "runtime-baseline.c"
#endif
#define original_try arm_wasm_jit_try
#define candidate_try arm_wasm_jit_try
#include "soc.h"
#include "snapshot.h"
#include <stdlib.h>
#include "arm_wasm_jit.h"
#include "jit-runtime.h"
#include <emscripten.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static s5l8900_t machine;
static bool initialized;
static uint32_t program[64],status;
EMSCRIPTEN_KEEPALIVE uintptr_t review_program(void){return (uintptr_t)program;}
EMSCRIPTEN_KEEPALIVE uintptr_t review_cpu(void){return (uintptr_t)&machine.cpu;}
EMSCRIPTEN_KEEPALIVE uintptr_t review_ram(void){return (uintptr_t)machine.ram;}
EMSCRIPTEN_KEEPALIVE int review_begin(unsigned count,unsigned thumb,uint32_t flags,uint32_t entry,unsigned alias){
 if(initialized)s5l8900_free(&machine);memset(&machine,0,sizeof machine);
 if(!s5l8900_init(&machine,0,1u<<20))return -1;initialized=true;
 s5l8900_set_direct_ram_writes(&machine,true);web_jit_bind(&machine);status=0;
 arm_cpu_t *c=&machine.cpu;c->cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F|flags;
 for(unsigned i=0;i<15;i++)c->r[i]=0x100u+i;
 if(alias){uint32_t desc=(3u<<10)|2u;memcpy(machine.ram+0x4000,&desc,4);memcpy(machine.ram+0x4004,&desc,4);c->cp15.ttbr0=0x4000;c->cp15.dacr=1;c->cp15.sctlr|=ARM_SCTLR_M|ARM_SCTLR_XP;arm_mmu_tlb_flush(c);}
 // Warm a genuine store mapping for the identity or aliased code page.
 uint32_t warm=0xe5810000u;memcpy(machine.ram+0x10000,&warm,4);c->r[0]=0;c->r[1]=alias?0x100000u:0;c->r[15]=0x10000;if(arm_step(c)!=ARM_OK)return -2;
 // Warm the fetch mapping with a harmless NOP, then install actual opcodes.
 uint32_t nop=thumb?0x46c0u:0xe1a00000u;memcpy(machine.ram+entry,&nop,thumb?2:4);c->r[15]=entry;if(thumb)c->cpsr|=ARM_CPSR_T;if(arm_step(c)!=ARM_OK)return -3;
 for(unsigned i=0;i<count;i++){if(thumb){uint16_t word=program[i];memcpy(machine.ram+i*2,&word,2);}else memcpy(machine.ram+i*4,&program[i],4);}
 for(unsigned i=0;i<15;i++)c->r[i]=0x100u+i;c->r[15]=entry;c->cycles=0;c->cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F|flags|(thumb?ARM_CPSR_T:0);return 0;
}
EMSCRIPTEN_KEEPALIVE void review_reg(unsigned r,uint32_t v){if(r<16)machine.cpu.r[r]=v;}
EMSCRIPTEN_KEEPALIVE unsigned review_run(unsigned count,unsigned compiled,unsigned cap){unsigned done=0;arm_wasm_jit_enabled=compiled;while(done<count){unsigned left=count-done;if(left>cap)left=cap;unsigned n=compiled?(compiled==1?original_try(&machine.cpu,left):candidate_try(&machine.cpu,left)):0;if(n)done+=n;else{status=arm_step(&machine.cpu);if(status!=ARM_OK)break;done++;}}return done;}
EMSCRIPTEN_KEEPALIVE unsigned review_try(unsigned budget){arm_wasm_jit_enabled=1;return candidate_try(&machine.cpu,budget);}
static uint8_t state[2048];
EMSCRIPTEN_KEEPALIVE unsigned review_state_size(void){size_t a=offsetof(arm_cpu_t,bus),b=offsetof(arm_cpu_t,tlb)-offsetof(arm_cpu_t,abort_pending),off=0;memcpy(state+off,&machine.cpu,a);off+=a;memcpy(state+off,&machine.cpu.abort_pending,b);off+=b;uint64_t counters[]={machine.cpu.tlb_hits,machine.cpu.tlb_misses,machine.cpu.tlb_flushes,machine.cpu.dread_hits,machine.cpu.dread_misses,machine.cpu.dwrite_hits,machine.cpu.dwrite_misses,status};memcpy(state+off,counters,sizeof counters);return off+sizeof counters;}
EMSCRIPTEN_KEEPALIVE uintptr_t review_state(void){return (uintptr_t)state;}

static bool choose_original;


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
