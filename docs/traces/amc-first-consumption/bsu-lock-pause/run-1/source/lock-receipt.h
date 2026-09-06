#ifndef AMC_LOCK_RECEIPT_H
#define AMC_LOCK_RECEIPT_H
#include "retirement.h"
typedef struct {guest_step start;uint64_t callback,end;uint32_t pc,cpsr,opcode,address,value,after_pc,after_cpsr;bool pending,retired;} lock_receipt;
static uint32_t lock_opcode(uint32_t pc){return pc==0xc0277ce8?0xe5823400:pc==0xc0278218?0xe5832000:pc==0xc0278220?0xe5832a00:0;}
static bool lock_begin(lock_receipt*r,const guest_step*s,uint64_t cycles,uint32_t pc,uint32_t cpsr,uint32_t op,uint32_t address,uint32_t value){
 uint32_t expected=lock_opcode(pc);if(!r||r->pending||r->retired||!s||!s->active||s->returned||s->cycles==UINT64_MAX||cycles!=s->cycles+1||pc!=s->pc||cpsr!=s->cpsr||(cpsr&63)!=0x13||!expected||op!=expected)return false;
 if((pc==0xc0277ce8&&(address!=0x38500400||value!=1))||(pc==0xc0278218&&(address!=0x38501000||value))||(pc==0xc0278220&&(address!=0x38500a00||value)))return false;
 *r=(lock_receipt){.start=*s,.callback=cycles,.pc=pc,.cpsr=cpsr,.opcode=op,.address=address,.value=value,.pending=true};return true;
}
static bool lock_finish(lock_receipt*r,const guest_step*s,uint64_t cycles,uint32_t pc,uint32_t cpsr,bool in_guest){
 if(!r||!r->pending||!s||s->active||!s->returned||in_guest||s->cycles!=r->start.cycles||s->pc!=r->pc||s->cpsr!=r->cpsr||cycles!=r->callback||cycles!=r->start.cycles+1||pc!=r->pc+4||cpsr!=r->cpsr)return false;
 r->end=cycles;r->after_pc=pc;r->after_cpsr=cpsr;r->pending=false;r->retired=true;return true;
}
#endif
