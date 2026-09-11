#include "soc.h"
#include "snapshot.h"
#include "adaptive-runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static s5l8900_t m;
static unsigned checks,calls,misses;
static unsigned mapping_queries;
static uint8_t *(*original_host_ram)(void *,uint32_t,uint32_t);
static uint8_t *counted_host_ram(void *ctx,uint32_t address,uint32_t bytes){
 mapping_queries++;return original_host_ram(ctx,address,bytes);
}
static arm_adaptive_fetch_witness_t original[2];
static arm_cpu_t saved_cpu;
static const uint32_t entry=0x100000u,tail=0x100400u;
#define CHECK(x) do { ++checks;if(!(x)){ \
 fprintf(stderr,"refresh:%u: %s\n",__LINE__,#x);exit(1); \
} } while(0)
static void generated(arm_cpu_t *c,uint32_t budget,arm_adaptive_result_t *r){
 (void)budget;calls++;
 *r=(arm_adaptive_result_t){0,ADAPT_EXIT_BEFORE,ARM_OK,c->r[15],
   c->r[15],UINT32_MAX,0,0};
}
static void other(arm_cpu_t *c,uint32_t b,arm_adaptive_result_t *r){generated(c,b,r);}
static void observe(uint32_t pc,unsigned thumb){CHECK(pc==entry);CHECK(thumb==0);misses++;}
static void drain(void){arm_adaptive_fn f[ADAPTIVE_RETIRED_CAP];adaptive_retired(f,ADAPTIVE_RETIRED_CAP);}
static void warm(uint32_t pc){
 uint32_t pa;CHECK(!arm_mmu_translate(&m.cpu,pc,ARM_ACCESS_FETCH,
   (m.cpu.cpsr&ARM_CPSR_MODE_MASK)!=ARM_MODE_USR,&pa));
 CHECK(arm_fetch_cache_try_refill(&m.cpu,pc,
   (m.cpu.cpsr&ARM_CPSR_MODE_MASK)!=ARM_MODE_USR));
}
static void setup(bool tracked){
 drain();adaptive_bind(NULL);CHECK(!adaptive_failed());drain();
 m.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;m.cpu.abort_pending=false;
 m.cpu.irq_line=m.cpu.fiq_line=false;
 m.cpu.cp15.sctlr=ARM_SCTLR_M|ARM_SCTLR_XP;
 m.cpu.cp15.ttbr0=0x4000;m.cpu.cp15.ttbr1=m.cpu.cp15.ttbcr=0;
 m.cpu.cp15.dacr=1;m.cpu.cp15.context_id=m.cpu.cp15.fcse_pid=0;
 uint32_t section=(3u<<10)|2u;s5l8900_load(&m,0x4004,&section,4);
 arm_mmu_tlb_flush(&m.cpu);warm(entry);warm(tail);m.cpu.r[15]=entry;
 adaptive_bind(&m.cpu);CHECK(!adaptive_failed());
 if(tracked)CHECK(adaptive_track_ram(m.ram,m.ram_size));
 CHECK(adaptive_capture_at(entry,&original[0]));
 CHECK(adaptive_capture_at(tail,&original[1]));
 CHECK(adaptive_install(entry,0,generated,original,2,adaptive_epoch(),128));
 adaptive_enable(1);CHECK(adaptive_observe(observe));calls=misses=0;
}
static void attempt(bool executes){
 m.cpu.r[15]=entry;arm_cpu_t before=m.cpu;unsigned old=calls;
 arm_adaptive_result_t r;
 CHECK(arm_adaptive_try(&m.cpu,1,&r)==(executes?1:0));
 CHECK(calls==old+(executes?1u:0u));
 CHECK(!memcmp(&before,&m.cpu,sizeof before));CHECK(!adaptive_failed());
}
static void flush_warm(void){arm_mmu_tlb_flush(&m.cpu);warm(entry);warm(tail);}
static void stats(unsigned attempts,unsigned successes,unsigned refusals){
 CHECK(adaptive_stat(10)==attempts);CHECK(adaptive_stat(11)==successes);
 CHECK(adaptive_stat(12)==refusals);
}
int main(void){
 CHECK(s5l8900_init(&m,0,2u<<20));m.bus.ram_written=adaptive_ram_written;
 original_host_ram=m.bus.host_ram;m.bus.host_ram=counted_host_ram;
 uint32_t nop=0xe1a00000u;
 s5l8900_load(&m,0,&nop,4);s5l8900_load(&m,1024,&nop,4);
 s5l8900_load(&m,0x100000,&nop,4);s5l8900_load(&m,0x100400,&nop,4);
 setup(true);attempt(true);stats(0,0,0);
 flush_warm();CHECK(m.cpu.tlb_gen!=original[0].generation);
 unsigned before_queries=mapping_queries;
 attempt(true);stats(1,1,0);CHECK(misses==0&&adaptive_units()==1);
 CHECK(mapping_queries>before_queries); /* Successful refresh still maps. */
 /* Refresh never repairs a pending request, even after the installed unit
  * has reused exactly the same handle and dependencies successfully. */
 CHECK(!adaptive_install(entry,0,other,original,2,adaptive_epoch(),128));
 arm_adaptive_fn f;CHECK(!adaptive_retired(&f,1));attempt(true);stats(1,1,0);

 /* Last dependency cold: first staged witness must not leak into registry. */
 setup(true);saved_cpu=m.cpu;arm_mmu_tlb_flush(&m.cpu);warm(entry);
 attempt(false);stats(1,0,1);CHECK(misses==1&&adaptive_units()==1);
 arm_cpu_t cold=m.cpu;m.cpu=saved_cpu;
 attempt(true);stats(1,0,1); /* A partial commit would force another refresh. */
 m.cpu=cold;warm(tail);attempt(true);stats(2,1,1);CHECK(misses==1);
 CHECK(!adaptive_retired(&f,1));

 /* Same version number at a different physical host is not the same code. */
 setup(true);uint32_t section=0x100000u|(3u<<10)|2u;
 s5l8900_load(&m,0x4004,&section,4);flush_warm();attempt(false);stats(1,0,1);
 CHECK(adaptive_units()==1&&!adaptive_retired(&f,1));
 /* Permission and XN failures are cached refusals, never capture side effects. */
 for(unsigned xn=0;xn<2;xn++){
   setup(true);section=xn?((3u<<10)|16u|2u):2u;
   s5l8900_load(&m,0x4004,&section,4);arm_mmu_tlb_flush(&m.cpu);
   uint32_t pa;CHECK(arm_mmu_translate(&m.cpu,entry,ARM_ACCESS_FETCH,true,&pa)!=0);
   attempt(false);stats(1,0,1);CHECK(adaptive_units()==1);
 }
 /* Every CP15 field must match literally despite valid warmed mappings. */
 for(unsigned field=0;field<7;field++){
   setup(true);
   uint32_t *fields[]={&m.cpu.cp15.sctlr,&m.cpu.cp15.ttbr0,&m.cpu.cp15.ttbr1,
     &m.cpu.cp15.ttbcr,&m.cpu.cp15.dacr,&m.cpu.cp15.fcse_pid,&m.cpu.cp15.context_id};
   uint32_t changes[]={ARM_SCTLR_U,0x100u,0x4000u,0x20u,2u,0x02000000u,1u};
   *fields[field]^=changes[field];flush_warm();before_queries=mapping_queries;
   attempt(false);stats(1,0,1);CHECK(mapping_queries==before_queries);
   /* Refusal is not negatively cached; exact stats still count every try. */
   attempt(false);stats(2,0,2);CHECK(mapping_queries==before_queries);
   CHECK(adaptive_units()==1&&!adaptive_retired(&f,1));
 }
 setup(true);m.cpu.cpsr=(m.cpu.cpsr&~ARM_CPSR_MODE_MASK)|ARM_MODE_USR;
 flush_warm();before_queries=mapping_queries;attempt(false);stats(1,0,1);
 CHECK(mapping_queries==before_queries);
 /* ISA is part of entry lookup; toggling it cannot refresh/reuse ARM code. */
 setup(true);m.cpu.cpsr|=ARM_CPSR_T;
 CHECK(adaptive_observe(NULL));attempt(false);stats(0,0,0);CHECK(adaptive_units()==1);
 setup(false);flush_warm();attempt(false);stats(1,0,1);
 /* Changed versions retain existing immediate retirement, not re-admission. */
 setup(true);s5l8900_load(&m,0,&nop,4);flush_warm();attempt(false);
 stats(0,0,0);CHECK(!adaptive_units());
 CHECK(adaptive_retired(&f,1)==1&&f==generated);CHECK(!adaptive_retired(&f,1));
 /* Later dependency version change cannot partially refresh an earlier one. */
 setup(true);s5l8900_load(&m,1024,&nop,4);flush_warm();attempt(false);
 stats(1,0,1);CHECK(adaptive_units()==1&&!adaptive_retired(&f,1));

 /* Real restore revokes epoch/tracker/handle before payload application. */
 setup(true);uint8_t *snapshot=NULL,*after=NULL;size_t size=0,after_size=0;
 CHECK(snapshot_save_mem(&m,&snapshot,&size)==SNAP_OK);
 uint32_t epoch=adaptive_epoch();flush_warm();attempt(true);stats(1,1,0);
 CHECK(snapshot_load_mem(&m,snapshot,size)==SNAP_OK);
 CHECK(adaptive_epoch()!=epoch&&!adaptive_units()&&!adaptive_failed());
 CHECK(adaptive_retired(&f,1)==1&&f==generated);CHECK(!adaptive_retired(&f,1));
 CHECK(snapshot_save_mem(&m,&after,&after_size)==SNAP_OK);
 CHECK(size==after_size&&!memcmp(snapshot,after,size));free(snapshot);free(after);
 adaptive_bind(&m.cpu);CHECK(adaptive_track_ram(m.ram,m.ram_size));
 CHECK(!adaptive_install(entry,0,other,original,2,epoch,128));
 warm(entry);warm(tail);arm_adaptive_fetch_witness_t fresh[2];
 CHECK(adaptive_capture_at(entry,&fresh[0]));CHECK(adaptive_capture_at(tail,&fresh[1]));
 CHECK(adaptive_install(entry,0,generated,fresh,2,adaptive_epoch(),128));
 adaptive_enable(1);attempt(true);stats(0,0,0);
 adaptive_bind(NULL);CHECK(!adaptive_failed());
 CHECK(adaptive_retired(&f,1)==1&&f==generated);CHECK(!adaptive_retired(&f,1));
 s5l8900_free(&m);
 printf("refresh: %u checks PASS; atomic generation-only reuse, strict pending captures, mappings/context/version/restore\n",checks);
 return 0;
}
