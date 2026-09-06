#ifndef AMC_RELEASE_RETIREMENT_H
#define AMC_RELEASE_RETIREMENT_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {uint64_t cycles;uint32_t pc,cpsr;bool active,returned;} guest_step;
static bool release_opcode(uint32_t pc,uint32_t op,uint32_t cpsr){
 return (pc==0xc0272690&&op==0x01c120b0&&(cpsr&(1u<<30)))||
 ((pc==0xc02726a8||pc==0xc02726c4)&&op==0xe1c030b0);
}
static bool release_start_valid(const guest_step*s,uint64_t callback,uint32_t pc,uint32_t cpsr,uint32_t opcode){
 return s&&s->active&&!s->returned&&s->cycles!=UINT64_MAX&&callback==s->cycles+1&&pc==s->pc&&cpsr==s->cpsr&&
 (cpsr&0x3f)==0x13&&release_opcode(pc,opcode,cpsr);
}
static bool release_finish_valid(const guest_step*s,uint64_t callback,uint32_t pc,uint32_t cpsr,uint32_t opcode,
 uint64_t end,uint32_t after_pc,uint32_t after_cpsr,bool in_guest){
 if(!s||s->active||!s->returned||in_guest)return false;
 guest_step began=*s;began.active=true;began.returned=false;
 return release_start_valid(&began,callback,pc,cpsr,opcode)&&end==s->cycles+1&&after_pc==pc+4&&after_cpsr==cpsr;
}
#endif
