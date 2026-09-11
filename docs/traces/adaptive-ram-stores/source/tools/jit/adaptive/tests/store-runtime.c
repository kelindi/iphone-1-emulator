/* Native runtime contracts using real interpreter-warmed RAM caches.
 * Admitted native callbacks stand in for generated code; no browser workload. */
#include "soc.h"
#include "adaptive-runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,calls,notifications,action,width,tracked;
static s5l8900_t m;
static uint8_t expected_ram[1u<<20];
#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr,"store-runtime:%u: %s\n",__LINE__,#x); exit(1); \
} } while(0)
enum { REFUSE, NORMAL, CODE, OBSERVER_FATAL, WRONG_EXIT, ZERO_STALE };

static void observer(void *ctx,const uint8_t *host,uint32_t bytes) {
 CHECK(ctx==&m);CHECK(bytes==width);notifications++;
 if(tracked)adaptive_ram_written(ctx,host,bytes);
 if(action==OBSERVER_FATAL)adaptive_enable(0);
}

static void callback(arm_cpu_t *cpu,uint32_t budget,arm_adaptive_result_t *r) {
 (void)budget;calls++;
 CHECK(adaptive_guard(0,0xe1a00000u,4));
 uint32_t va=action==REFUSE?0x9400u:
             (action==CODE||action==WRONG_EXIT||action==ZERO_STALE)?0:0x9000u;
 /* Same-value code write must still cause a boundary, even untracked. */
 uint32_t value=(action==CODE||action==WRONG_EXIT||action==ZERO_STALE)?0xe1a00000u:0x87654321u;
 arm_cpu_t before=*cpu;memcpy(expected_ram,m.ram,sizeof expected_ram);
 unsigned status=adaptive_write(va,width,1,value);
 if(action==REFUSE) {
  CHECK(status==0);CHECK(notifications==0);
  CHECK(!memcmp(&before,cpu,sizeof before));
  CHECK(!memcmp(expected_ram,m.ram,sizeof expected_ram));
  *r=(arm_adaptive_result_t){0,ADAPT_EXIT_BEFORE,ARM_OK,0,0,0,0,0};
  return;
 }
 CHECK(status==(action==NORMAL?1u:2u));
 CHECK(notifications==1);
 memcpy(expected_ram+va,&value,width);
 CHECK(!memcmp(expected_ram,m.ram,sizeof expected_ram));
 before.dwrite_hits++;
 CHECK(!memcmp(&before,cpu,sizeof before));
 if(action!=NORMAL) {
  arm_cpu_t after=*cpu;
  CHECK(adaptive_guard(0,0xe1a00000u,4)==0);
  CHECK(adaptive_read(0x9000,4,1)==0); /* The read mapping really is warm. */
  CHECK(adaptive_write(0x9000,4,1,0xffffffff)==0);
  CHECK(notifications==1);
  CHECK(!memcmp(&after,cpu,sizeof after));
  CHECK(!memcmp(expected_ram,m.ram,sizeof expected_ram));
 }
 if(action==ZERO_STALE) {
  *r=(arm_adaptive_result_t){0,ADAPT_EXIT_STALE,ARM_OK,0,0,0,0,0};
  return;
 }
 cpu->r[15]=4;
 unsigned reason=(action==CODE||action==OBSERVER_FATAL)?ADAPT_EXIT_BOUNDARY:
                  ADAPT_EXIT_BUDGET;
 *r=(arm_adaptive_result_t){1,reason,ARM_OK,4,0,0,0,0};
}

static void setup(unsigned which,unsigned bytes,bool tracking) {
 adaptive_bind(NULL);CHECK(!adaptive_failed());
 arm_adaptive_fn removed[ADAPTIVE_UNIT_CAP];
 (void)adaptive_retired(removed,ADAPTIVE_UNIT_CAP);
 CHECK(s5l8900_init(&m,0,sizeof expected_ram));
 CHECK(s5l8900_set_direct_ram_writes(&m,true));
 m.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
 uint32_t ops[]={0xe5810000u,0xe5912000u};
 s5l8900_load(&m,0x1000,ops,sizeof ops);
 for(unsigned i=0;i<2;i++) {
  m.cpu.r[0]=i?0x12345678u:0xe1a00000u;
  m.cpu.r[1]=i?0x9000u:0;
  m.cpu.r[15]=0x1000;
  CHECK(arm_step(&m.cpu)==ARM_OK);
 }
 m.cpu.r[15]=0x1004;CHECK(arm_step(&m.cpu)==ARM_OK);
 CHECK(m.cpu.r[2]==0x12345678u);
 m.cpu.r[15]=0;CHECK(arm_step(&m.cpu)==ARM_OK);m.cpu.r[15]=0;
 action=which;width=bytes;tracked=tracking;calls=notifications=0;
 adaptive_bind(&m.cpu);CHECK(!adaptive_failed());
 if(tracked)CHECK(adaptive_track_ram(m.ram,m.ram_size));
 m.bus.ram_written=observer;
 arm_adaptive_fetch_witness_t witness;CHECK(adaptive_capture(&witness));
 CHECK(adaptive_install(0,0,callback,&witness,1,adaptive_epoch(),64));
 adaptive_enable(1);
 /* Outside active code, warm entries confer no authority to the helper. */
 arm_cpu_t before=m.cpu;memcpy(expected_ram,m.ram,sizeof expected_ram);
 CHECK(adaptive_write(0x9000,width,1,0)==0);
 CHECK(!memcmp(&before,&m.cpu,sizeof before));
 CHECK(!memcmp(expected_ram,m.ram,sizeof expected_ram));
}

static void retired_once(void) {
 arm_adaptive_fn fn[2]={NULL,NULL};
 CHECK(adaptive_retired(fn,2)==1);CHECK(fn[0]==callback);
 CHECK(adaptive_retired(fn,2)==0);
}

static void run(unsigned which,unsigned bytes,bool tracking) {
 setup(which,bytes,tracking);
 uint64_t cycles=m.cpu.cycles,hits=m.cpu.dwrite_hits;
 arm_adaptive_result_t r;
 int status=arm_adaptive_try(&m.cpu,1,&r);
 CHECK(calls==1);
 if(which==OBSERVER_FATAL||which==WRONG_EXIT||which==ZERO_STALE) {
  CHECK(status==-1 && adaptive_failed() && arm_adaptive_enabled);
  CHECK(m.cpu.cycles==cycles && m.cpu.dwrite_hits==hits+1);
  CHECK(notifications==1 && adaptive_units()==1);
  arm_cpu_t after=m.cpu;
  CHECK(arm_adaptive_try(&m.cpu,1,&r)==-1 && calls==1);
  arm_status_t step_status=ARM_OK;
  CHECK(s5l8900_run(&m,1,&step_status)==0 && step_status==ARM_HALT);
  CHECK(!memcmp(&after,&m.cpu,sizeof after));
  CHECK(!memcmp(expected_ram,m.ram,sizeof expected_ram));
  CHECK(notifications==1 && calls==1);
  adaptive_bind(NULL);CHECK(!adaptive_failed());retired_once();
 } else {
  CHECK(status==1 && !adaptive_failed());
  CHECK(r.retired==(which==REFUSE?0u:1u));
  CHECK(m.cpu.cycles==cycles+r.retired);
  CHECK(m.cpu.dwrite_hits==hits+r.retired);
  CHECK(notifications==r.retired);
  if(which==CODE) {
   CHECK(r.reason==ADAPT_EXIT_BOUNDARY && r.resume_pc==4);
   CHECK(adaptive_units()==0);retired_once();
   CHECK(adaptive_code_version(m.ram)==(tracking?2u:0u));
   m.cpu.r[15]=0;
   CHECK(arm_adaptive_try(&m.cpu,1,&r)==0 && calls==1);
  } else {
   CHECK(adaptive_units()==1);
   adaptive_bind(NULL);CHECK(!adaptive_failed());retired_once();
  }
 }
 adaptive_bind(NULL);CHECK(!adaptive_failed());s5l8900_free(&m);
}

int main(void) {
 for(unsigned tracking=0;tracking<2;tracking++) {
  run(REFUSE,4,tracking);run(NORMAL,1,tracking);run(NORMAL,4,tracking);
  run(CODE,4,tracking);run(OBSERVER_FATAL,4,tracking);run(WRONG_EXIT,4,tracking);run(ZERO_STALE,4,tracking);
 }
 printf("store-runtime: %u checks PASS (14 cases)\n",checks);
 return 0;
}
