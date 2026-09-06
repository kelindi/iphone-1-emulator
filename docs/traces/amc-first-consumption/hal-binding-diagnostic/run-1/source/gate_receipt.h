#ifndef ISOLATED_GATE_RECEIPT_H
#define ISOLATED_GATE_RECEIPT_H
#include "retirement.h"
typedef struct {bool pending,retired;guest_step step;uint64_t callback,end;uint32_t pc,cpsr,opcode,address,value,after_pc,after_cpsr;} gate_receipt;
static bool gate_begin(gate_receipt*r,const guest_step*s,uint64_t callback,uint32_t pc,uint32_t cpsr,uint32_t opcode,uint32_t address,uint32_t value){
 if(!r||!s||r->pending||r->retired||!s->active||s->returned||s->pc!=pc||s->cpsr!=cpsr||(cpsr&63)!=0x13||callback!=s->cycles+1||(opcode&0x0c100000)!=0x04000000||(opcode>>28)!=14)return false;
 *r=(gate_receipt){.pending=true,.step=*s,.callback=callback,.pc=pc,.cpsr=cpsr,.opcode=opcode,.address=address,.value=value};return true;
}
static bool gate_finish(gate_receipt*r,const guest_step*s,uint64_t cycles,uint32_t pc,uint32_t cpsr,bool running){
 if(!r||!s||!r->pending||r->retired||running||s->active||!s->returned||s->cycles!=r->step.cycles||s->pc!=r->pc||s->cpsr!=r->cpsr||cycles!=r->callback||pc!=r->pc+4||cpsr!=r->cpsr)return false;
 r->pending=false;r->retired=true;r->end=cycles;r->after_pc=pc;r->after_cpsr=cpsr;return true;
}
#endif
