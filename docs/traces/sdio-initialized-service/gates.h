#ifndef PRIVATE_HANDOFF_GATES_H
#define PRIVATE_HANDOFF_GATES_H
#include <stdbool.h>
#include <stdint.h>
/* Original kernel driver gate is ARM SVC. Flag bits outside T/mode do not
 * define control flow here; never accept the same address in Thumb/IRQ/user. */
static bool gate_arm_svc(uint32_t cpsr){return (cpsr&0x3fu)==0x13u;}
typedef struct {bool have_low,complete;uint64_t low_completion;} gate_pair_t;
static void gate_pair_begin(gate_pair_t*p){*p=(gate_pair_t){0};}
static void gate_pair_read(gate_pair_t*p,unsigned reg,uint32_t response,bool successful,uint64_t completion){
 if(reg==0x34u){p->complete=false;p->have_low=successful&&(response&255u)==0xdcu;p->low_completion=completion;return;}
 bool ok=reg==0x35u&&successful&&(response&255u)==0xfeu&&p->have_low&&p->low_completion!=UINT64_MAX&&completion==p->low_completion+1;
 p->complete=ok;p->have_low=false;
}
#endif
