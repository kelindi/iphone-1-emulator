#include "soc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static s5l8900_t m;
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while(0)
static void refused(uint32_t va,unsigned width,bool priv){
 arm_cpu_t before=m.cpu;
 CHECK(!arm_read_cache_try(&m.cpu,va,width,priv));
 CHECK(!memcmp(&before,&m.cpu,sizeof before));
}
static void hit(uint32_t va,unsigned width,bool priv){
 arm_cpu_t before=m.cpu;before.dread_hits++;
 CHECK(arm_read_cache_try(&m.cpu,va,width,priv)==m.ram+va);
 CHECK(!memcmp(&before,&m.cpu,sizeof before));
}
static void warm(uint32_t va,bool unprivileged){
 uint32_t raw=unprivileged?0xe4b10000:0xe5910000;
 s5l8900_load(&m,0,&raw,4);m.cpu.r[1]=va;m.cpu.r[15]=0;
 CHECK(arm_step(&m.cpu)==ARM_OK);
}
int main(void){
 CHECK(s5l8900_init(&m,0,1u<<20));
 m.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
 uint32_t word=0x12345678;s5l8900_load(&m,0x9000,&word,4);
 refused(0x9000,4,true);warm(0x9000,false);CHECK(m.cpu.r[0]==word);
 hit(0x9000,4,true);hit(0x9001,1,true);hit(0x9002,2,true);
 refused(0x9000,4,false);refused(0x9400,4,true);
 refused(0x93fe,4,true);refused(0x9000,0,true);refused(0x9000,3,true);
 m.cpu.cp15.sctlr&=~(ARM_SCTLR_A|ARM_SCTLR_U);
 refused(0x9001,4,true);refused(0x9001,2,true);
 m.cpu.cp15.sctlr|=ARM_SCTLR_U;
 hit(0x9001,4,true);hit(0x9001,2,true);refused(0x93ff,2,true);
 m.cpu.cp15.sctlr|=ARM_SCTLR_A;refused(0x9001,4,true);
 m.cpu.cp15.sctlr&=~ARM_SCTLR_A;
 warm(0x9000,true);hit(0x9000,4,false);
 arm_mmu_tlb_flush(&m.cpu);refused(0x9000,4,true);refused(0x9000,4,false);
 /* A real MMU walk populates the cache; privilege remains part of its key. */
 uint32_t descriptor=(3u<<10)|2u;s5l8900_load(&m,0x4000,&descriptor,4);
 m.cpu.cp15.ttbr0=0x4000;m.cpu.cp15.dacr=1;
 m.cpu.cp15.sctlr|=ARM_SCTLR_M|ARM_SCTLR_XP;arm_mmu_tlb_flush(&m.cpu);
 warm(0x9000,false);CHECK(m.cpu.r[0]==word);hit(0x9000,4,true);refused(0x9000,4,false);
 /* Missing mappings and devices cannot be discovered by the compiled helper. */
 refused(0x200000,4,true);refused(S5L8900_TIMER_BASE+TIMER4_COUNTBUF,4,true);
 const arm_bus_t *saved=m.cpu.bus;m.cpu.bus=NULL;refused(0x9000,4,true);m.cpu.bus=saved;
 s5l8900_free(&m);
 printf("read-cache: %u checks PASS; hits account once, refusals preserve complete CPU\n",checks);
}
