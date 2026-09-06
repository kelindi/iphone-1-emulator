#ifndef PRIVATE_RUNTIME_REQUEST_H
#define PRIVATE_RUNTIME_REQUEST_H
#include "gates.h"
#include <string.h>
#include <stddef.h>
typedef struct {
 bool prefix,reg6_error,reg6_return,reg6_overwrite,verify_return,setup,builder,frame,trigger;
 uint64_t prefix_cycles,reg6_cycles,return_cycles,overwrite_cycles,verify_cycles,setup_cycles,builder_cycles,frame_cycles,trigger_cycles;
 uint32_t reg6_value,ignored_return,opcode,sequence,argument,descriptor,source,block_length,block_count,card_block_size;
 uint32_t trigger_pc,trigger_cpsr,events,irq_pending;
 uint64_t completions,data_completions,data_starts;
 uint8_t virtual_bytes[320],physical_bytes[320];
} request_gate_t;
static uint16_t request_le16(const uint8_t*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static bool request_packet(const uint8_t*b,size_t n,uint32_t opcode,uint32_t sequence){
 if(!b||n!=320||sequence>65535||(opcode!=0x93&&opcode!=0x43))return false;
 if(request_le16(b)!=18||b[2]!=1||b[3]||request_le16(b+4)!=opcode||request_le16(b+6)!=14||request_le16(b+8)!=sequence||b[10]||b[11])return false;
 static const uint8_t pull[]={1,0,255,255,0,0},sleep[]={6,0,0,0,4,255};
 return !memcmp(b+12,opcode==0x93?pull:sleep,6);
}
static bool request_fields(uint32_t argument,uint32_t descriptor,uint32_t source,uint32_t length,uint32_t count,uint32_t card_size){
 return (argument>>31)==1&&((argument>>28)&7u)==1&&((argument>>27)&1u)==1&&((argument>>26)&1u)==0&&((argument>>9)&0x1ffffu)==0x10000u&&(argument&511u)==1&&descriptor==0x501f5u&&source!=0&&(source&3u)==0&&source<=UINT32_MAX-319u&&length==320&&count==1&&card_size==320;
}
/* No callback observation alone can pass. The caller supplies the actual
 * completed run count/status, preceding instruction and outside-run state. */
static bool request_retired(const request_gate_t*g,uint32_t before_pc,uint32_t before_cpsr,unsigned count,bool instruction_ok,uint32_t pc,uint32_t cpsr,uint64_t cycles,bool busy,uint64_t now,uint64_t deadline,uint64_t completions,uint64_t data_completions,uint64_t data_starts,bool armed,bool active,bool slots_empty,uint32_t events,uint32_t irq){
 if(!g||!g->prefix||!g->reg6_error||!g->reg6_return||!g->reg6_overwrite||!g->verify_return||!g->setup||!g->builder||!g->frame||!g->trigger)return false;
 if(!(g->prefix_cycles<g->reg6_cycles&&g->reg6_cycles<g->return_cycles&&g->return_cycles<g->overwrite_cycles&&g->overwrite_cycles<=g->verify_cycles&&g->verify_cycles<g->setup_cycles&&g->setup_cycles<=g->builder_cycles&&g->builder_cycles<g->frame_cycles&&g->frame_cycles<g->trigger_cycles&&g->trigger_cycles<cycles))return false;
 if(g->reg6_value!=0x1100||g->trigger_pc!=0xc0431524u||!gate_arm_svc(g->trigger_cpsr)||before_pc!=g->trigger_pc||!gate_arm_svc(before_cpsr)||count!=1||!instruction_ok||pc!=0xc0431528u||!gate_arm_svc(cpsr))return false;
 return request_fields(g->argument,g->descriptor,g->source,g->block_length,g->block_count,g->card_block_size)&&request_packet(g->virtual_bytes,320,g->opcode,g->sequence)&&!memcmp(g->virtual_bytes,g->physical_bytes,320)&&busy&&now<deadline&&completions==g->completions&&data_completions==504&&data_completions==g->data_completions&&data_starts==g->data_starts&&!armed&&!active&&slots_empty&&events==g->events&&irq==g->irq_pending;
}
#endif
