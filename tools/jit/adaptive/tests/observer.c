#include "soc.h"
#include "adaptive-runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static s5l8900_t m;
static unsigned checks, notifications, generated, action, exit_reason;
static arm_adaptive_fn next_fn;
static uint8_t saved_ram[1u << 20];
#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr,"observer:%u: %s\n",__LINE__,#x); exit(1); \
} } while (0)
enum { INSTALL, IDLE, RECURSE, DISABLE, ENABLE, BIND, CHANGE, TRACK };

static void body(arm_cpu_t *cpu,uint32_t budget,arm_adaptive_result_t *r) {
 generated++;
 CHECK(adaptive_guard(cpu->r[15],0xe1a00000u,4));
 unsigned n=exit_reason==ADAPT_EXIT_BUDGET?budget:
   (exit_reason==ADAPT_EXIT_EDGE||exit_reason==ADAPT_EXIT_BOUNDARY)?1u:0u;
 uint32_t pc=cpu->r[15];cpu->r[15]+=4*n;
 *r=(arm_adaptive_result_t){n,exit_reason,ARM_OK,cpu->r[15],
   n?pc:cpu->r[15],UINT32_MAX,0,0};
}
static void first(arm_cpu_t *c,uint32_t b,arm_adaptive_result_t *r){body(c,b,r);}
static void second(arm_cpu_t *c,uint32_t b,arm_adaptive_result_t *r){body(c,b,r);}
static void observe(uint32_t pc,unsigned thumb) {
 notifications++;CHECK(pc==m.cpu.r[15]);CHECK(thumb==0);
 CHECK(adaptive_stat(2)>=1);CHECK(!adaptive_guard(pc,0xe1a00000u,4));
 CHECK(!adaptive_read(0,4,1));CHECK(!adaptive_write(0,4,1,0));
 arm_adaptive_result_t r;
 switch(action){
 case INSTALL: {
   arm_adaptive_fetch_witness_t w;CHECK(adaptive_capture_at(pc,&w));
   arm_adaptive_fn old[ADAPTIVE_RETIRED_CAP];
   unsigned n=adaptive_retired(old,ADAPTIVE_RETIRED_CAP);
   CHECK(n<=1);if(n)CHECK(old[0]==first||old[0]==second);
   CHECK(adaptive_install(pc,thumb,next_fn,&w,1,adaptive_epoch(),64));break;
 }
 case IDLE:break;
 case RECURSE:CHECK(arm_adaptive_try(&m.cpu,1,&r)==-1);break;
 case DISABLE:adaptive_enable(0);break;
 case ENABLE:adaptive_enable(1);break;
 case BIND:adaptive_bind(NULL);break;
 case CHANGE:CHECK(!adaptive_observe(NULL));break;
 case TRACK:CHECK(!adaptive_track_ram(m.ram,m.ram_size));break;
 }
}
static void drain(void){
 arm_adaptive_fn old[ADAPTIVE_RETIRED_CAP];
 (void)adaptive_retired(old,ADAPTIVE_RETIRED_CAP);
}
static void reset(void){
 drain();adaptive_bind(&m.cpu);CHECK(!adaptive_failed());drain();
 m.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
 m.cpu.r[15]=0;m.cpu.abort_pending=false;
 m.cpu.irq_line=m.cpu.fiq_line=false;
 CHECK(adaptive_track_ram(m.ram,m.ram_size));
 notifications=generated=0;action=INSTALL;next_fn=first;
 exit_reason=ADAPT_EXIT_BUDGET;
 adaptive_enable(1);CHECK(adaptive_observe(observe));
 for(unsigned i=0;i<ADAPTIVE_STAT_COUNT;i++)CHECK(adaptive_stat(i)==0);
 CHECK(adaptive_stat(100)==0);
}
static void pure_miss(int expected){
 arm_cpu_t before=m.cpu;memcpy(saved_ram,m.ram,sizeof saved_ram);
 arm_adaptive_result_t result;memset(&result,0xa5,sizeof result);
 arm_adaptive_result_t untouched=result;
 CHECK(arm_adaptive_try(&m.cpu,1,&result)==expected);
 CHECK(!memcmp(&before,&m.cpu,sizeof before));
 CHECK(!memcmp(saved_ram,m.ram,sizeof saved_ram));
 CHECK(!memcmp(&untouched,&result,sizeof result));
}
int main(void){
 CHECK(s5l8900_init(&m,0,sizeof saved_ram));
 uint32_t nop=0xe1a00000u;s5l8900_load(&m,0,&nop,4);
 m.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
 CHECK(arm_step(&m.cpu)==ARM_OK);m.cpu.r[15]=0;
 m.bus.ram_written=adaptive_ram_written;
 reset();pure_miss(0);
 CHECK(notifications==1&&generated==0&&adaptive_units()==1);
 CHECK(adaptive_stat(0)==0&&adaptive_stat(1)==0&&adaptive_stat(2)==1);
 uint64_t cycles=m.cpu.cycles;arm_adaptive_result_t result;
 CHECK(arm_adaptive_try(&m.cpu,1,&result)==1);
 CHECK(generated==1&&notifications==1&&m.cpu.cycles==cycles+1);
 CHECK(adaptive_stat(0)==1&&adaptive_stat(1)==1&&adaptive_stat(4)==1);

 /* Found entries rejected for context can synchronously be replaced. */
 reset();pure_miss(0);m.cpu.cp15.context_id^=1;next_fn=second;
 pure_miss(0);CHECK(notifications==2&&generated==0&&adaptive_units()==1);
 CHECK(adaptive_stat(2)==2);
 CHECK(arm_adaptive_try(&m.cpu,1,&result)==1&&generated==1);
 arm_adaptive_fn old;CHECK(adaptive_retired(&old,1)==1&&old==first);
 CHECK(!adaptive_retired(&old,1));
 /* Code-version retirement precedes observer, which can drain/reuse handle. */
 reset();pure_miss(0);s5l8900_load(&m,0,&nop,4);pure_miss(0);
 CHECK(notifications==2&&generated==0&&adaptive_units()==1);
 CHECK(!adaptive_retired(&old,1));
 CHECK(arm_adaptive_try(&m.cpu,1,&result)==1);

 /* Each normal exit counts once; invalid calls receive no retirement credit. */
 for(unsigned reason=0;reason<=ADAPT_EXIT_BOUNDARY;reason++){
   reset();pure_miss(0);exit_reason=reason;cycles=m.cpu.cycles;
   unsigned budget=reason==ADAPT_EXIT_BUDGET?1:2;
   CHECK(arm_adaptive_try(&m.cpu,budget,&result)==(reason?1:-1));
   CHECK(adaptive_stat(1)==1&&adaptive_stat(3+reason)==1);
   CHECK(adaptive_stat(0)==(reason?result.retired:0));
   CHECK(m.cpu.cycles==cycles+(reason?result.retired:0));
 }
 for(unsigned forbidden=RECURSE;forbidden<=TRACK;forbidden++){
   reset();action=forbidden;uint32_t epoch=adaptive_epoch();pure_miss(-1);
   CHECK(adaptive_failed()&&arm_adaptive_enabled&&adaptive_epoch()==epoch);
   CHECK(notifications==1&&generated==0&&adaptive_stat(1)==0);
   adaptive_enable(0);pure_miss(-1);CHECK(notifications==1);
 }
 /* Bind clears callback and counters, even while returned handles await drain. */
 reset();pure_miss(0);adaptive_bind(&m.cpu);CHECK(!adaptive_failed());
 CHECK(adaptive_stat(2)==0&&adaptive_units()==0);
 adaptive_enable(1);pure_miss(0);CHECK(notifications==1&&adaptive_stat(2)==1);
 CHECK(adaptive_retired(&old,1)==1&&old==first);
 CHECK(adaptive_observe(observe));action=IDLE;pure_miss(0);pure_miss(0);
 CHECK(notifications==3&&generated==0&&adaptive_stat(2)==3);
 CHECK(adaptive_observe(NULL));pure_miss(0);CHECK(notifications==3);

 /* Ineligible/zero-budget attempts are neither misses nor callbacks. */
 reset();action=IDLE;CHECK(arm_adaptive_try(&m.cpu,0,&result)==0);
 m.cpu.abort_pending=true;pure_miss(0);m.cpu.abort_pending=false;
 m.cpu.r[15]=2;pure_miss(0);m.cpu.r[15]=0;
 m.cpu.irq_line=true;m.cpu.cpsr&=~ARM_CPSR_I;pure_miss(0);
 m.cpu.irq_line=false;m.cpu.cpsr|=ARM_CPSR_I;
 CHECK(notifications==0&&adaptive_stat(2)==0);
 /* Full retirement queue refuses before observer or generated execution. */
 reset();arm_adaptive_fetch_witness_t w;CHECK(adaptive_capture_at(0,&w));
 for(unsigned i=0;i<=ADAPTIVE_RETIRED_CAP;i++)
   CHECK(adaptive_install(0,0,(arm_adaptive_fn)(uintptr_t)(i+1),
     &w,1,adaptive_epoch(),1));
 pure_miss(0);CHECK(!notifications&&!generated&&adaptive_stat(2)==0);
 drain();adaptive_bind(NULL);CHECK(!adaptive_failed());drain();
 s5l8900_free(&m);
 printf("observer: %u checks PASS; deferred install, recapture, ownership, fatal policy and exact stats\n",checks);
 return 0;
}
