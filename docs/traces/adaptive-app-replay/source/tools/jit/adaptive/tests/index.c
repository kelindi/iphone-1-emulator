#include "soc.h"
#include "adaptive-runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static s5l8900_t m;
static unsigned checks, called, stale;
static arm_adaptive_fn removed[ADAPTIVE_RETIRED_CAP];
#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr,"index:%u: %s\n",__LINE__,#x);exit(1); \
} } while(0)
static void answer(arm_cpu_t *c,arm_adaptive_result_t *r,unsigned id){
 called=id;
 *r=(arm_adaptive_result_t){0,stale?ADAPT_EXIT_STALE:ADAPT_EXIT_BEFORE,
   ARM_OK,c->r[15],c->r[15],UINT32_MAX,0,0};
}
static void a(arm_cpu_t *c,uint32_t b,arm_adaptive_result_t *r){(void)b;answer(c,r,1);}
static void b(arm_cpu_t *c,uint32_t bgt,arm_adaptive_result_t *r){(void)bgt;answer(c,r,2);}
static void c(arm_cpu_t *c0,uint32_t bgt,arm_adaptive_result_t *r){(void)bgt;answer(c0,r,3);}
static void d(arm_cpu_t *c0,uint32_t bgt,arm_adaptive_result_t *r){(void)bgt;answer(c0,r,4);}
/* Generate deliberate collisions against the published fixed hash design.
 * The oracle below is actual callback identity and exact retired ownership,
 * not a second lookup implementation. */
static unsigned bucket(uint32_t pc,unsigned thumb){
 uint32_t k=pc^(thumb?0x9e3779b9u:0);k^=k>>16;k*=0x7feb352du;
 k^=k>>15;k*=0x846ca68bu;k^=k>>16;
 return k&(ADAPTIVE_INDEX_BUCKETS-1);
}
static void reset(void){
 adaptive_retired(removed,ADAPTIVE_RETIRED_CAP);adaptive_bind(&m.cpu);
 CHECK(!adaptive_failed());adaptive_retired(removed,ADAPTIVE_RETIRED_CAP);
 m.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;m.cpu.r[15]=0;
 stale=called=0;adaptive_enable(1);
}
static unsigned install(uint32_t pc,unsigned thumb,arm_adaptive_fn fn){
 m.cpu.cpsr=(m.cpu.cpsr&~ARM_CPSR_T)|(thumb?ARM_CPSR_T:0);
 arm_adaptive_fetch_witness_t w;CHECK(adaptive_capture_at(pc,&w));
 return adaptive_install(pc,thumb,fn,&w,1,adaptive_epoch(),1);
}
static void invoke(uint32_t pc,unsigned thumb,unsigned expected){
 m.cpu.cpsr=(m.cpu.cpsr&~ARM_CPSR_T)|(thumb?ARM_CPSR_T:0);
 m.cpu.r[15]=pc;called=0;arm_cpu_t before=m.cpu;
 arm_adaptive_result_t r;
 CHECK(arm_adaptive_try(&m.cpu,1,&r)==(expected?1:0));
 CHECK(called==expected);CHECK(!memcmp(&before,&m.cpu,sizeof before));
 CHECK(!adaptive_failed());
}
int main(void){
 CHECK(s5l8900_init(&m,0,1u<<20));
 uint32_t pcs[4];unsigned found=0;
 for(uint32_t pc=0;pc<m.ram_size&&found<4;pc+=4)
   if(bucket(pc,0)==bucket(0,0))pcs[found++]=pc;
 CHECK(found==4);arm_adaptive_fn fns[]={a,b,c};
 /* Remove each of head/middle/tail, then drain and reuse that exact handle. */
 for(unsigned drop=0;drop<3;drop++){
   reset();for(unsigned i=0;i<3;i++)CHECK(install(pcs[i],0,fns[i]));
   for(unsigned i=0;i<3;i++)invoke(pcs[i],0,i+1);
   stale=1;invoke(pcs[drop],0,drop+1);stale=0;
   CHECK(adaptive_units()==2);invoke(pcs[drop],0,0);
   for(unsigned i=0;i<3;i++)if(i!=drop)invoke(pcs[i],0,i+1);
   CHECK(!install(pcs[drop],0,fns[drop])); /* Pending handle remains owned. */
   CHECK(adaptive_retired(removed,ADAPTIVE_RETIRED_CAP)==1);
   CHECK(removed[0]==fns[drop]);CHECK(install(pcs[drop],0,fns[drop]));
   for(unsigned i=0;i<3;i++)invoke(pcs[i],0,i+1);
 }
 reset();for(unsigned i=0;i<3;i++)CHECK(install(pcs[i],0,fns[i]));
 CHECK(install(pcs[1],0,d));CHECK(adaptive_units()==3);
 invoke(pcs[0],0,1);invoke(pcs[1],0,4);invoke(pcs[2],0,3);
 CHECK(adaptive_retired(removed,ADAPTIVE_RETIRED_CAP)==1&&removed[0]==b);
 CHECK(install(pcs[3],0,b));invoke(pcs[3],0,2);
 /* Same PC in both ISAs remains two independently owned entries. */
 reset();CHECK(install(0,0,a));CHECK(install(0,1,b));
 invoke(0,0,1);invoke(0,1,2);stale=1;invoke(0,1,2);stale=0;
 invoke(0,0,1);invoke(0,1,0);
 CHECK(adaptive_retired(removed,ADAPTIVE_RETIRED_CAP)==1&&removed[0]==b);
 /* A version-stale entry retires from the index before a generated call. */
 reset();CHECK(adaptive_track_ram(m.ram,m.ram_size));
 m.bus.ram_written=adaptive_ram_written;CHECK(install(0,0,a));
 uint32_t value=0;s5l8900_load(&m,0,&value,4);invoke(0,0,0);
 CHECK(adaptive_units()==0&&adaptive_stat(1)==0);
 CHECK(adaptive_retired(removed,ADAPTIVE_RETIRED_CAP)==1&&removed[0]==a);
 CHECK(install(0,0,a));invoke(0,0,1);
 /* A full registry still finds the last allocated slot, handles misses and
  * replaces in place without losing any of the 2048 owned handles. Fake
  * handles below are never invoked. */
 reset();
 for(unsigned i=0;i<ADAPTIVE_UNIT_CAP-1;i++)
   CHECK(install(i*4,0,(arm_adaptive_fn)(uintptr_t)(i+1)));
 uint32_t last=(ADAPTIVE_UNIT_CAP-1)*4;
 CHECK(install(last,0,a));CHECK(adaptive_units()==ADAPTIVE_UNIT_CAP);
 invoke(last,0,1);invoke(last+4,0,0);CHECK(!install(last+4,0,d));
 CHECK(install(last,0,b));invoke(last,0,2);
 CHECK(adaptive_retired(removed,ADAPTIVE_RETIRED_CAP)==1&&removed[0]==a);
 adaptive_bind(&m.cpu);CHECK(!adaptive_failed()&&!adaptive_units());
 CHECK(adaptive_retired(removed,ADAPTIVE_RETIRED_CAP)==ADAPTIVE_UNIT_CAP);
 for(unsigned i=0;i<ADAPTIVE_UNIT_CAP-1;i++)
   CHECK(removed[i]==(arm_adaptive_fn)(uintptr_t)(i+1));
 CHECK(removed[ADAPTIVE_UNIT_CAP-1]==b);
 CHECK(!adaptive_retired(removed,ADAPTIVE_RETIRED_CAP));
 adaptive_enable(1);invoke(last,0,0);CHECK(install(0,0,a));invoke(0,0,1);
 invoke(last,0,0);adaptive_bind(NULL);CHECK(!adaptive_failed());
 CHECK(adaptive_retired(removed,ADAPTIVE_RETIRED_CAP)==1&&removed[0]==a);
 s5l8900_free(&m);
 printf("index: %u checks PASS; collision PCs %u/%u/%u/%u, removal/replacement/ISA/capacity/ownership\n",
   checks,pcs[0],pcs[1],pcs[2],pcs[3]);
 return 0;
}
