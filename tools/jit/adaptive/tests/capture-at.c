#include "soc.h"
#include "adaptive-runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static s5l8900_t m;
static unsigned checks,bus_reads,map_limit,calls;
static uint8_t (*read8)(void *,uint32_t);
static uint16_t (*read16)(void *,uint32_t);
static uint32_t (*read32)(void *,uint32_t);
static uint8_t *(*host_ram)(void *,uint32_t,uint32_t);
#define CHECK(x) do { ++checks; if(!(x)) { \
 fprintf(stderr,"capture-at:%u: %s\n",__LINE__,#x); exit(1); \
} } while(0)
static uint8_t r8(void *ctx,uint32_t a){bus_reads++;return read8(ctx,a);}
static uint16_t r16(void *ctx,uint32_t a){bus_reads++;return read16(ctx,a);}
static uint32_t r32(void *ctx,uint32_t a){bus_reads++;return read32(ctx,a);}
static uint8_t *mapping(void *ctx,uint32_t a,uint32_t n){
 return n>map_limit?NULL:host_ram(ctx,a,n);
}

static arm_adaptive_fetch_witness_t capture(uint32_t pc,bool expected,
                                            uint32_t physical) {
 arm_cpu_t before=m.cpu;unsigned reads=bus_reads;
 arm_adaptive_fetch_witness_t out={0};
 CHECK(adaptive_capture_at(pc,&out)==(unsigned)expected);
 CHECK(!memcmp(&before,&m.cpu,sizeof before));
 CHECK(bus_reads==reads);
 if(expected){
  CHECK(out.host==m.ram+physical);
  CHECK(out.block==(pc&~1023u));CHECK(out.epoch==adaptive_epoch());
  CHECK(out.generation==m.cpu.tlb_gen);
  CHECK(out.mode==(m.cpu.cpsr&ARM_CPSR_MODE_MASK));
  CHECK(out.thumb==(m.cpu.cpsr&ARM_CPSR_T));
 }
 return out;
}
static void warm(uint32_t pc){
 m.cpu.r[15]=pc;CHECK(arm_step(&m.cpu)==ARM_OK);
 CHECK(m.cpu.r[15]==pc+4 && !m.cpu.abort_pending);
}
static void blocked_active(arm_cpu_t *cpu,uint32_t budget,
                            arm_adaptive_result_t *r){
 (void)budget;calls++;
 arm_cpu_t before=*cpu;unsigned reads=bus_reads;
 arm_adaptive_fetch_witness_t w;
 CHECK(!adaptive_capture_at(cpu->r[15],&w));
 CHECK(adaptive_failed());CHECK(!memcmp(&before,cpu,sizeof before));
 CHECK(bus_reads==reads);
 *r=(arm_adaptive_result_t){0,ADAPT_EXIT_BEFORE,ARM_OK,
                          cpu->r[15],cpu->r[15],0,0,0};
}

int main(void){
 CHECK(s5l8900_init(&m,0,2u<<20));
 read8=m.bus.read8;read16=m.bus.read16;read32=m.bus.read32;host_ram=m.bus.host_ram;
 m.bus.read8=r8;m.bus.read16=r16;m.bus.read32=r32;
 m.bus.host_ram=mapping;map_limit=UINT32_MAX;
 m.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
 uint32_t nop=0xe1a00000u;
 s5l8900_load(&m,0,&nop,4);s5l8900_load(&m,1024,&nop,4);
 adaptive_bind(&m.cpu);
 /* Existing capture still requires the current fetch cache. New MMU-off
  * capture can prove a full identity RAM span without executing a fetch. */
 arm_adaptive_fetch_witness_t old;
 CHECK(!adaptive_capture(&old));capture(0,true,0);CHECK(!adaptive_capture(&old));
 capture(2,false,0);capture(0x3e200000,false,0);capture(0x200000,false,0);
 map_limit=512;capture(0,false,0);map_limit=UINT32_MAX;
 m.cpu.cpsr|=ARM_CPSR_T;
 capture(2,true,0);capture(1,false,0);m.cpu.cpsr&=~ARM_CPSR_T;

 uint32_t section=(3u<<10)|2u,privileged=(1u<<10)|2u,xn=section|16u;
 s5l8900_load(&m,0x4004,&section,4);s5l8900_load(&m,0x4008,&section,4);
 s5l8900_load(&m,0x400c,&privileged,4);s5l8900_load(&m,0x4010,&xn,4);
 s5l8900_load(&m,0x4014,&section,4);
 m.cpu.cp15.ttbr0=0x4000;m.cpu.cp15.dacr=1;
 m.cpu.cp15.sctlr|=ARM_SCTLR_M|ARM_SCTLR_XP;arm_mmu_tlb_flush(&m.cpu);
 capture(0x100000,false,0); /* Valid page table is insufficient: cold FETCH. */
 uint32_t pa;
 CHECK(!arm_mmu_translate(&m.cpu,0x500000,ARM_ACCESS_READ,true,&pa));
 capture(0x500000,false,0); /* Warm READ is not executable provenance. */
 warm(0x100000);CHECK(adaptive_capture(&old));
 arm_adaptive_fetch_witness_t current=capture(0x100000,true,0);
 CHECK(current.block==old.block&&current.host==old.host);
 /* Current-fetch fast path must not attach new context metadata to an old
  * host. Deliberately omit the architectural setter's flush in each case. */
 uint32_t *context_fields[]={&m.cpu.cp15.sctlr,&m.cpu.cp15.ttbr0,
   &m.cpu.cp15.ttbr1,&m.cpu.cp15.ttbcr,&m.cpu.cp15.dacr,
   &m.cpu.cp15.context_id};
 for(unsigned i=0;i<sizeof context_fields/sizeof context_fields[0];i++){
  uint32_t saved=*context_fields[i];*context_fields[i]^=0x100u;
  CHECK(m.cpu.fetch_host==m.ram);capture(0x100000,false,0);
  *context_fields[i]=saved;capture(0x100000,true,0);
 }
 /* Raw MMU disable must use the identity address, not the translated host. */
 m.cpu.cp15.sctlr&=~ARM_SCTLR_M;capture(0x100000,true,0x100000);
 m.cpu.cp15.sctlr|=ARM_SCTLR_M;
 warm(0x100400);CHECK(m.cpu.fetch_blk==0x100400);
 capture(0x100000,true,0); /* Noncurrent but resident FETCH TLB entry. */
 CHECK(m.cpu.fetch_blk==0x100400&&m.cpu.r[15]==0x100404);
 map_limit=512;capture(0x100000,false,0);map_limit=UINT32_MAX;
 warm(0x200000);capture(0x100000,true,0);capture(0x200000,true,0);
 CHECK(adaptive_track_ram(m.ram,m.ram_size));m.bus.ram_written=adaptive_ram_written;
 CHECK(capture(0x100000,true,0).code_version==1);
 s5l8900_load(&m,0,&nop,4); /* Same bytes, new physical version shared by aliases. */
 CHECK(capture(0x100000,true,0).code_version==2);
 CHECK(capture(0x200000,true,0).code_version==2);
 capture(0x100002,false,0);capture(0x100800,false,0);
 m.cpu.cpsr=(m.cpu.cpsr&~ARM_CPSR_MODE_MASK)|ARM_MODE_USR;
 capture(0x100000,false,0); /* Privileged cached mapping cannot serve user. */
 warm(0x100000);capture(0x100000,true,0);
 CHECK(arm_mmu_translate(&m.cpu,0x300000,ARM_ACCESS_FETCH,false,&pa)!=0);
 capture(0x300000,false,0); /* Cached permission fault. */
 CHECK(arm_mmu_translate(&m.cpu,0x400000,ARM_ACCESS_FETCH,false,&pa)!=0);
 capture(0x400000,false,0); /* Cached XN fault. */
 m.cpu.cpsr|=ARM_CPSR_T;capture(0x100002,true,0);m.cpu.cpsr&=~ARM_CPSR_T;
 m.cpu.abort_pending=true;capture(0x100000,false,0);m.cpu.abort_pending=false;
 m.cpu.cpsr&=~ARM_CPSR_I;m.cpu.irq_line=true;capture(0x100000,false,0);
 m.cpu.irq_line=false;m.cpu.cpsr|=ARM_CPSR_I;
 m.cpu.cpsr&=~ARM_CPSR_F;m.cpu.fiq_line=true;capture(0x100000,false,0);
 m.cpu.fiq_line=false;m.cpu.cpsr|=ARM_CPSR_F;
 uint32_t flags=m.cpu.cpsr;m.cpu.cpsr&=~ARM_CPSR_MODE_MASK;capture(0x100000,false,0);m.cpu.cpsr=flags;
 arm_cpu_t before=m.cpu;CHECK(!adaptive_capture_at(0x100000,NULL));CHECK(!memcmp(&before,&m.cpu,sizeof before));
 CHECK(!arm_adaptive_capture_fetch_at(NULL,1,0,&old));
 CHECK(!arm_adaptive_capture_fetch_at(&m.cpu,0,0x100000,&old));

 m.cpu.r[15]=0x100000;current=capture(0x100000,true,0);
 CHECK(adaptive_install(0x100000,0,blocked_active,&current,1,adaptive_epoch(),64));
 adaptive_enable(1);before=m.cpu;
 arm_adaptive_result_t result;CHECK(arm_adaptive_try(&m.cpu,1,&result)==-1);
 CHECK(calls==1&&adaptive_failed());CHECK(!memcmp(&before,&m.cpu,sizeof before));
 CHECK(arm_adaptive_try(&m.cpu,1,&result)==-1&&calls==1);
 adaptive_bind(NULL);CHECK(!adaptive_failed());
 arm_adaptive_fn removed;CHECK(adaptive_retired(&removed,1)==1&&removed==blocked_active);
 CHECK(!adaptive_retired(&removed,1));
 capture(0x100000,false,0);s5l8900_free(&m);
 printf("capture-at: %u checks PASS; pure current/noncurrent/alias captures and refusal boundaries\n",checks);
 return 0;
}
