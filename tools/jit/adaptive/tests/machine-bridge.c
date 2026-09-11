#include "adaptive-runtime.h"
#include "soc.h"
#include "snapshot.h"
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
static s5l8900_t machine;
static bool initialized;
static arm_adaptive_fetch_witness_t witnesses[8];
static uint32_t program[512], code_offset;
static arm_status_t last_status;
static uint8_t *saved;
static size_t saved_length;
EM_JS(void, expose_memory, (), {Module.jitMemory=wasmMemory;});
EMSCRIPTEN_KEEPALIVE uintptr_t test_program(void){return (uintptr_t)program;}
EMSCRIPTEN_KEEPALIVE uintptr_t test_cpu(void){return (uintptr_t)&machine.cpu;}
EMSCRIPTEN_KEEPALIVE uintptr_t test_ram(void){return (uintptr_t)machine.ram;}
EMSCRIPTEN_KEEPALIVE unsigned test_offset(unsigned n){return n?offsetof(arm_cpu_t,cpsr):offsetof(arm_cpu_t,r);}
EMSCRIPTEN_KEEPALIVE int test_begin(unsigned words){
 adaptive_bind(NULL);
 if(initialized)s5l8900_free(&machine);
 memset(&machine,0,sizeof machine);
 if(words>512||!s5l8900_init(&machine,0,1u<<20))return 0;
 s5l8900_set_direct_ram_writes(&machine,true);
 initialized=true;code_offset=0;machine.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
 memcpy(machine.ram,program,words*4);memset(witnesses,0,sizeof witnesses);
 adaptive_bind(&machine.cpu);last_status=ARM_OK;expose_memory();return 1;
}
EMSCRIPTEN_KEEPALIVE void test_mmu(unsigned alias){
 uint32_t descriptor=(3u<<10)|2u;
 if(alias){
  code_offset=0x10000;memcpy(machine.ram+code_offset,machine.ram,sizeof program);
  descriptor=0x8001;uint32_t page=code_offset|(3u<<4)|2u;
  memcpy(machine.ram+0x8000,&page,4);
 }
 memcpy(machine.ram+0x4000,&descriptor,4);
 machine.cpu.cp15.ttbr0=0x4000;machine.cpu.cp15.dacr=1;
 machine.cpu.cp15.sctlr|=ARM_SCTLR_M|ARM_SCTLR_XP;
 arm_mmu_tlb_flush(&machine.cpu);
}
/* Memory-boundary fixtures use the real MMU and bus, never mock helpers. */
EMSCRIPTEN_KEEPALIVE void test_alias_write(void){
 /* VA 0 and VA 1MiB share the same coarse table and physical code page. */
 uint32_t descriptor=0x8001;memcpy(machine.ram+0x4004,&descriptor,4);
 arm_mmu_tlb_flush(&machine.cpu);
}
EMSCRIPTEN_KEEPALIVE unsigned test_track(void){
 machine.bus.ram_written=adaptive_ram_written;
 return adaptive_track_ram(machine.ram,machine.ram_size);
}
EMSCRIPTEN_KEEPALIVE unsigned test_version(unsigned offset){return adaptive_code_version(machine.ram+offset);}
EMSCRIPTEN_KEEPALIVE void test_host_write(unsigned offset,unsigned value){s5l8900_load(&machine,offset,&value,4);}
EMSCRIPTEN_KEEPALIVE void test_bus_write(unsigned offset,unsigned value){machine.bus.write32(machine.bus.ctx,offset,value);}
EMSCRIPTEN_KEEPALIVE void test_thumb(void){machine.cpu.cpsr|=ARM_CPSR_T;}
EMSCRIPTEN_KEEPALIVE void test_alignment(void){machine.cpu.cp15.sctlr|=ARM_SCTLR_A;arm_mmu_tlb_flush(&machine.cpu);}
EMSCRIPTEN_KEEPALIVE unsigned test_fault(unsigned n){return n?machine.cpu.cp15.dfsr:machine.cpu.cp15.dfar;}
EMSCRIPTEN_KEEPALIVE unsigned test_register(unsigned n){return machine.cpu.r[n&15];}
EMSCRIPTEN_KEEPALIVE void test_timer(void){
 machine.cpu.cpsr &= ~ARM_CPSR_I;
 machine.bus.write32(machine.bus.ctx,S5L8900_TIMER_BASE+TIMER4_COUNTBUF,1);
 machine.bus.write32(machine.bus.ctx,S5L8900_TIMER_BASE+TIMER4_STATE,TIMER4_STATE_START|TIMER4_STATE_UPDATE);
 machine.bus.write32(machine.bus.ctx,S5L8900_VIC0_BASE+VIC_INTENABLE,1u<<S5L8900_IRQ_TIMER);
 s5l8900_tick(&machine,0);
}
EMSCRIPTEN_KEEPALIVE unsigned test_irq(unsigned n){
 return n==0?machine.cpu.r[0]:n==1?machine.cpu.r[2]:n==2?machine.cpu.cpsr:
        n==3?machine.timer.irqlatch:n==4?machine.cpu.r[14]:machine.cpu.r[15];
}
EMSCRIPTEN_KEEPALIVE int test_warm(unsigned pc,unsigned slot){
 if(slot>=8||pc>=(1u<<20))return 0;
 arm_cpu_t *cpu=&machine.cpu;uint32_t regs[16],flags=cpu->cpsr,word;
 uint64_t cycles=cpu->cycles;memcpy(regs,cpu->r,sizeof regs);
 memcpy(&word,machine.ram+pc+code_offset,4);uint32_t nop=(cpu->cpsr&ARM_CPSR_T)?0x46c0:0xe1a00000;
 memcpy(machine.ram+pc+code_offset,&nop,4);cpu->r[15]=pc;
 arm_status_t status=arm_step(cpu);
 memcpy(machine.ram+pc+code_offset,&word,4);memcpy(cpu->r,regs,sizeof regs);cpu->cpsr=flags;cpu->cycles=cycles;
 return status==ARM_OK&&adaptive_capture(&witnesses[slot]);
}
EMSCRIPTEN_KEEPALIVE int test_warm_write(unsigned va,unsigned value){
 arm_cpu_t *cpu=&machine.cpu;uint32_t regs[16],flags=cpu->cpsr,word,raw=0xe5812000;
 uint64_t cycles=cpu->cycles;memcpy(regs,cpu->r,sizeof regs);
 memcpy(&word,machine.ram+code_offset,4);memcpy(machine.ram+code_offset,&raw,4);
 cpu->r[15]=0;cpu->r[1]=va;cpu->r[2]=value;arm_status_t status=arm_step(cpu);
 bool ok=status==ARM_OK&&cpu->cpsr==flags&&!cpu->abort_pending;
 memcpy(machine.ram+code_offset,&word,4);memcpy(cpu->r,regs,sizeof regs);cpu->cpsr=flags;cpu->cycles=cycles;
 /* A WRITE through VA1MiB hashes to the same TLB slot as FETCH at VA0.
  * The current fetch pointer can survive that eviction; merely executing a
  * NOP at VA0 would then fail to repopulate its TLB entry. Drop only that
  * derived pointer so prepare's explicit fetch warmups rebuild both code
  * witnesses through the real MMU. Keep the warmed dwrite mapping intact. */
 cpu->fetch_host=NULL;
 return ok;
}
EMSCRIPTEN_KEEPALIVE void test_direct_writes(unsigned allowed){s5l8900_set_direct_ram_writes(&machine,allowed!=0);}
EMSCRIPTEN_KEEPALIVE unsigned test_write(unsigned va,unsigned width,unsigned privileged,unsigned value){return adaptive_write(va,width,privileged,value);}
EMSCRIPTEN_KEEPALIVE unsigned test_dwrite(unsigned misses){return (unsigned)(misses?machine.cpu.dwrite_misses:machine.cpu.dwrite_hits);}
EMSCRIPTEN_KEEPALIVE int test_install(uintptr_t fn,unsigned pc,unsigned count,unsigned epoch,unsigned bytes){
 return adaptive_install(pc,(machine.cpu.cpsr&ARM_CPSR_T)!=0,(arm_adaptive_fn)fn,witnesses,count,epoch,bytes);
}
EMSCRIPTEN_KEEPALIVE unsigned test_run(unsigned credits){return s5l8900_run(&machine,credits,&last_status);}
EMSCRIPTEN_KEEPALIVE unsigned test_dread(unsigned misses){return (unsigned)(misses?machine.cpu.dread_misses:machine.cpu.dread_hits);}
EMSCRIPTEN_KEEPALIVE unsigned test_tlb_hits(void){return (unsigned)machine.cpu.tlb_hits;}
EMSCRIPTEN_KEEPALIVE unsigned test_status(void){return last_status;}
EMSCRIPTEN_KEEPALIVE uintptr_t test_read(unsigned va,unsigned width,unsigned privileged){return adaptive_read(va,width,privileged);}
EMSCRIPTEN_KEEPALIVE void test_unaligned(unsigned modern){
 machine.cpu.cp15.sctlr&=~(ARM_SCTLR_U|ARM_SCTLR_A);
 if(modern)machine.cpu.cp15.sctlr|=ARM_SCTLR_U;
 arm_mmu_tlb_flush(&machine.cpu);
}
EMSCRIPTEN_KEEPALIVE unsigned test_guard(unsigned pc,unsigned raw,unsigned width){return adaptive_guard(pc,raw,width);}
EMSCRIPTEN_KEEPALIVE unsigned test_epoch(void){return adaptive_epoch();}
EMSCRIPTEN_KEEPALIVE void test_enable(unsigned enabled){adaptive_enable(enabled);}
EMSCRIPTEN_KEEPALIVE unsigned test_failed(void){return adaptive_failed();}
EMSCRIPTEN_KEEPALIVE uintptr_t test_snapshot(void){free(saved);saved=NULL;saved_length=0;if(snapshot_save_mem(&machine,&saved,&saved_length)!=SNAP_OK)return 0;return (uintptr_t)saved;}
EMSCRIPTEN_KEEPALIVE unsigned test_restore(uintptr_t bytes,unsigned length){return snapshot_load_mem(&machine,(const uint8_t *)bytes,length);}
EMSCRIPTEN_KEEPALIVE unsigned test_snapshot_size(void){return saved_length;}
EMSCRIPTEN_KEEPALIVE unsigned test_drain(uintptr_t output,unsigned cap){return adaptive_retired((arm_adaptive_fn*)output,cap);}
EMSCRIPTEN_KEEPALIVE void test_unbind(void){adaptive_bind(NULL);}
