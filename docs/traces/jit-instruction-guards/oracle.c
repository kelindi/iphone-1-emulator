#include "soc.h"
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
EMSCRIPTEN_KEEPALIVE unsigned review_run(unsigned count,unsigned compiled,unsigned cap){unsigned done=0;arm_wasm_jit_enabled=compiled;while(done<count){unsigned left=count-done;if(left>cap)left=cap;unsigned n=compiled?arm_wasm_jit_try(&machine.cpu,left):0;if(n)done+=n;else{status=arm_step(&machine.cpu);if(status!=ARM_OK)break;done++;}}return done;}
EMSCRIPTEN_KEEPALIVE unsigned review_try(unsigned budget){arm_wasm_jit_enabled=1;return arm_wasm_jit_try(&machine.cpu,budget);}
static uint8_t state[2048];
EMSCRIPTEN_KEEPALIVE unsigned review_state_size(void){size_t a=offsetof(arm_cpu_t,bus),b=offsetof(arm_cpu_t,tlb)-offsetof(arm_cpu_t,abort_pending),off=0;memcpy(state+off,&machine.cpu,a);off+=a;memcpy(state+off,&machine.cpu.abort_pending,b);off+=b;uint64_t counters[]={machine.cpu.tlb_hits,machine.cpu.tlb_misses,machine.cpu.tlb_flushes,machine.cpu.dread_hits,machine.cpu.dread_misses,machine.cpu.dwrite_hits,machine.cpu.dwrite_misses,status};memcpy(state+off,counters,sizeof counters);return off+sizeof counters;}
EMSCRIPTEN_KEEPALIVE uintptr_t review_state(void){return (uintptr_t)state;}
