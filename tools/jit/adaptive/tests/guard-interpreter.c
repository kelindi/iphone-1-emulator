#include "arm_adaptive_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t ram[2u<<20];
static unsigned reads;
static uint32_t r32(void *ctx,uint32_t a){(void)ctx;uint32_t x;assert(a+4<=sizeof ram);reads++;memcpy(&x,ram+a,4);return x;}
static uint16_t r16(void *ctx,uint32_t a){(void)ctx;uint16_t x;assert(a+2<=sizeof ram);reads++;memcpy(&x,ram+a,2);return x;}
static uint8_t r8(void *ctx,uint32_t a){(void)ctx;assert(a<sizeof ram);reads++;return ram[a];}
static void w32(void *ctx,uint32_t a,uint32_t x){(void)ctx;assert(a+4<=sizeof ram);memcpy(ram+a,&x,4);}
static void w16(void *ctx,uint32_t a,uint16_t x){(void)ctx;assert(a+2<=sizeof ram);memcpy(ram+a,&x,2);}
static void w8(void *ctx,uint32_t a,uint8_t x){(void)ctx;assert(a<sizeof ram);ram[a]=x;}
static uint8_t *host(void *ctx,uint32_t a,uint32_t n){(void)ctx;return (uint64_t)a+n<=sizeof ram?ram+a:NULL;}
static arm_bus_t bus={.read32=r32,.read16=r16,.read8=r8,.write32=w32,.write16=w16,.write8=w8,.host_ram=host};
static arm_cpu_t cpu,before;
static void unchanged_guard(const arm_adaptive_fetch_witness_t *w,uint64_t epoch,uint32_t pc,bool expected){
 before=cpu;unsigned prior=reads;
 assert(arm_adaptive_guard_instruction(&cpu,w,epoch,pc,0xe3a00007,4)==expected);
 assert(!memcmp(&before,&cpu,sizeof cpu));assert(reads==prior);
}
int main(void){
 arm_adaptive_fetch_witness_t w;
 for(unsigned mmu=0;mmu<2;mmu++){
  memset(ram,0,sizeof ram);arm_reset(&cpu,&bus);cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
  uint32_t pc=mmu?0x100000:0;w32(NULL,0,0xe3a00007);
  if(mmu){w32(NULL,0x4004,(3u<<10)|2u);cpu.cp15.ttbr0=0x4000;cpu.cp15.dacr=1;cpu.cp15.sctlr|=ARM_SCTLR_M|ARM_SCTLR_XP;arm_mmu_tlb_flush(&cpu);}
  assert(!arm_adaptive_capture_fetch(&cpu,1,&w));cpu.r[15]=pc;
  assert(arm_step(&cpu)==ARM_OK);assert(cpu.r[0]==7);assert(cpu.fetch_host==ram);
  assert(arm_adaptive_capture_fetch(&cpu,1,&w));unchanged_guard(&w,1,pc,true);
  w32(NULL,0,0xe3a00008);unchanged_guard(&w,1,pc,false);w32(NULL,0,0xe3a00007);
  unchanged_guard(&w,2,pc,false);
  if(mmu){
   // Refill after a genuine colliding fetch, without changing tlb_gen.
   // Identical instruction bytes at a different PA must not validate an old witness.
   uint32_t generation=cpu.tlb_gen;
   w32(NULL,0x100000,0xe3a00007);
   w32(NULL,0x4004,0x100000|(3u<<10)|2u);
   w32(NULL,0x4014,(3u<<10)|2u);
   cpu.r[15]=0x500000;assert(arm_step(&cpu)==ARM_OK);
   unchanged_guard(&w,1,pc,false);
   cpu.r[15]=pc;assert(arm_step(&cpu)==ARM_OK);
   assert(cpu.tlb_gen==generation&&cpu.fetch_host==ram+0x100000);
   unchanged_guard(&w,1,pc,false);
   assert(arm_adaptive_capture_fetch(&cpu,1,&w));unchanged_guard(&w,1,pc,true);
   // Revoke execution in a real section descriptor and flush as the guest
   // would. The guard refuses without raising a fault; arm_step delivers it.
   w32(NULL,0x4004,(3u<<10)|2u|16u);arm_mmu_tlb_flush(&cpu);
   unchanged_guard(&w,1,pc,false);cpu.r[15]=pc;
   assert(arm_step(&cpu)==ARM_OK);assert(cpu.cp15.ifar==pc);assert((cpu.cpsr&31)==ARM_MODE_ABT);
   assert(!arm_adaptive_capture_fetch(&cpu,1,&w));
  }
 }
 // A device-like fetch path has no host RAM mapping and must not be captured.
 bus.host_ram=NULL;arm_reset(&cpu,&bus);w32(NULL,0,0xe3a00007);cpu.r[15]=0;
 assert(arm_step(&cpu)==ARM_OK);assert(cpu.r[0]==7);assert(!arm_adaptive_capture_fetch(&cpu,1,&w));
 puts("PASS: interpreter-warmed identity/alias mappings, literal mutation, XN fault handoff and device-like fetch rejection");
}
