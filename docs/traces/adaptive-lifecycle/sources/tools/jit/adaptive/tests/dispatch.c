#include "arm_adaptive_dispatch.h"
#include "soc.h"
#include "snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned arm_adaptive_enabled;
/* This protocol-only fixture has no bound registry or captured lifetime. */
int arm_adaptive_forget(arm_cpu_t *cpu){(void)cpu;return 1;}
static unsigned policy,calls,fatal_r0,fatal_pc;
static uint64_t fatal_cycles;
static void prefix(arm_cpu_t *cpu,uint32_t budget,arm_adaptive_result_t *r){
 unsigned n=policy==ADAPT_EXIT_BUDGET?budget:1;
 if(policy>=ADAPT_EXIT_BEFORE&&policy<=ADAPT_EXIT_CONTEXT&&budget==1)n=0;
 cpu->r[0]+=n;cpu->r[15]+=4*n;
 *r=(arm_adaptive_result_t){n,policy,ARM_OK,cpu->r[15],cpu->r[15],0,0,0};
}
int arm_adaptive_try(arm_cpu_t *cpu,uint32_t budget,arm_adaptive_result_t *r){
 if(!budget&&policy!=99)return 0;
 calls++;
 if(policy==99){fatal_r0=cpu->r[0];fatal_pc=cpu->r[15];fatal_cycles=cpu->cycles;return -1;}
 if(!policy)return 0;
 return arm_adaptive_invoke(cpu,prefix,budget,1,r);
}
static s5l8900_t machine;
static void setup(void){
 memset(&machine,0,sizeof machine);assert(s5l8900_init(&machine,0,1u<<20));
 machine.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
 for(unsigned i=0;i<1024;i++){uint32_t raw=0xe2800001;memcpy(machine.ram+4*i,&raw,4);}
 calls=0;
}
static uint8_t *snapshot(size_t *length){uint8_t *bytes=NULL;assert(snapshot_save_mem(&machine,&bytes,length)==SNAP_OK);return bytes;}
int main(void){
 arm_status_t status;size_t length;
 arm_adaptive_enabled=0;setup();assert(s5l8900_run(&machine,32,&status)==32);assert(status==ARM_OK);
 uint8_t *expected=snapshot(&length);assert(machine.cpu.r[0]==32);s5l8900_free(&machine);
 for(unsigned p=0;p<=6;p++){
  policy=p;arm_adaptive_enabled=1;setup();
  assert(s5l8900_run(&machine,32,&status)==32);assert(status==ARM_OK);
  assert(machine.cpu.r[0]==32&&machine.cpu.r[15]==128&&machine.cpu.cycles==32);
  assert(calls>0);if(p==ADAPT_EXIT_EDGE)assert(calls>=31); // No mandatory interpreter instruction at edges.
  size_t got_length;uint8_t *got=snapshot(&got_length);
  if(got_length!=length||memcmp(got,expected,length)){
   // Snapshot excludes host-derived execution counters; mismatch is substantive.
   fprintf(stderr,"Full snapshot mismatch, policy %u\n",p);abort();
  }
  free(got);s5l8900_free(&machine);
 }
 free(expected);
 policy=99;setup();unsigned done=s5l8900_run(&machine,32,&status);assert(status==ARM_HALT);assert(done==fatal_cycles);assert(machine.cpu.r[0]==fatal_r0&&machine.cpu.r[15]==fatal_pc&&machine.cpu.cycles==fatal_cycles);assert(calls==1);s5l8900_free(&machine);
 puts("PASS: real scheduler snapshot equality across budget/edge/refusal/boundary policies; fatal exit stops without replay");
}
