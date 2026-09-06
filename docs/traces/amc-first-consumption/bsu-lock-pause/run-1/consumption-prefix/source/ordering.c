#include "ordering.h"
#include <string.h>
#include "original-contexts.h"
/* Exact original instructions/ISA, independently checked against both Mach-Os.
 * The user VA alone is insufficient: read the actually mapped instruction from
 * RAM using a copied CPU/TLB before observing a return in that address space. */
static bool target(uint32_t pc,uint32_t*instruction,unsigned*bytes,uint32_t*mode){
 *bytes=4;*mode=0x10;
 switch(pc){
 case 0xc01533c2:*instruction=0x4642;*bytes=2;*mode=0x33;return true;
 case 0x3123f7f0:*instruction=0xe2508000;return true;
 case 0x3123cdc8:case 0x3123ce18:*instruction=0xe3500000;return true;
 case 0x3123f95c:*instruction=0xebfffe65;return true;
 case 0xc0272224:*instruction=0xe3a03000;*mode=0x13;return true;
 default:return false;
 }
}
bool order_target(uint32_t pc){uint32_t instruction,mode;unsigned bytes;return target(pc,&instruction,&bytes,&mode);}
static uint32_t u32(const uint8_t*b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
order_result order_begin(order_log*log,const arm_cpu_t*cpu,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t driver){
 uint32_t instruction,mode;unsigned bytes;
 if(!log||!cpu||!ram||log->pending||log->count>=ORDER_CAP||!target(cpu->r[15],&instruction,&bytes,&mode)||(cpu->cpsr&0x3f)!=mode||(uint64_t)driver+0x24f>0x100000000ull)return ORDER_REJECT;
 uint8_t actual[4]={0};
 if(!amc_peek_ram(cpu,ram,base,size,cpu->r[15],actual,bytes)||u32(actual)!=instruction)return ORDER_REJECT;
 if(cpu->r[15]==0xc0272224&&cpu->r[0]!=driver)return ORDER_REJECT;
 if(cpu->r[15]==0x3123f95c&&cpu->r[1]!=3)return ORDER_REJECT;
 uint8_t state[4]={0},output[8]={0},flags[2]={0};
 /* User records retain their real read permissions. These kernel fields are
  * explicitly unavailable there, not read with debugger privilege or guessed. */
 if(mode!=ARM_MODE_USR&&(!amc_peek_ram(cpu,ram,base,size,driver+0xe0,state,4)||!amc_peek_ram(cpu,ram,base,size,driver+0x15c,output,8)||!amc_peek_ram(cpu,ram,base,size,driver+0x24d,flags,2)))return ORDER_REJECT;
 order_record record={0};
 if(!amc_peek_ram(cpu,ram,base,size,cpu->r[15],record.code,sizeof record.code))return ORDER_REJECT;
 bool matched=false;for(unsigned i=0;i<sizeof original_contexts/sizeof original_contexts[0];i++)if(original_contexts[i].pc==cpu->r[15])matched=!memcmp(record.code,original_contexts[i].bytes,sizeof record.code);
 if(!matched)return ORDER_REJECT;
 record.pc=cpu->r[15];record.cpsr=cpu->cpsr;memcpy(record.regs,cpu->r,sizeof record.regs);record.driver=driver;record.state=u32(state);record.output[0]=u32(output);record.output[1]=u32(output+4);record.flags=flags[0]|(uint32_t)flags[1]<<8;if(mode==ARM_MODE_USR)record.flags|=ORDER_KERNEL_FIELDS_UNAVAILABLE;record.cycles=cpu->cycles;record.instruction=instruction;record.instruction_bytes=bytes;
 if(mode==0x10){
  bool decoder=record.pc==0x3123cdc8||record.pc==0x3123ce18;
  record.object_address=cpu->r[record.pc==0x3123f95c?0:4];record.object_bytes=decoder?0xbc:0x90;
  if(!record.object_address||!amc_peek_ram(cpu,ram,base,size,record.object_address,record.object,record.object_bytes))return ORDER_REJECT;
  if(decoder){record.linked_address=u32(record.object+0x50);record.linked_bytes=sizeof record.linked;
   if(!record.linked_address||!amc_peek_ram(cpu,ram,base,size,record.linked_address,record.linked,record.linked_bytes))return ORDER_REJECT;
  }
  if(record.pc==0x3123f7f0){
   if((uint64_t)cpu->r[13]+0x3c>0x100000000ull)return ORDER_REJECT;
   record.stack_address=cpu->r[13]+8;record.stack_bytes=0x34;
   if(!amc_peek_ram(cpu,ram,base,size,record.stack_address,record.stack,record.stack_bytes))return ORDER_REJECT;
  }
 }
 log->staged=record;log->pending=true;return ORDER_OK;
}
order_result order_finish(order_log*log,const arm_cpu_t*cpu){
 if(!log||!cpu||!log->pending||log->count>=ORDER_CAP||cpu->cycles!=log->staged.cycles+1)return ORDER_REJECT;
 order_record r=log->staged;r.after_pc=cpu->r[15];r.after_cpsr=cpu->cpsr;r.after_cycles=cpu->cycles;r.after_lr=cpu->r[14];
 uint32_t expected=r.pc+r.instruction_bytes;bool link=true;
 if(r.pc==0x3123f95c){expected=0x3123f2f8;link=r.after_lr==0x3123f960;}
 r.retired=(cpu->cpsr&0x3f)==(r.cpsr&0x3f)&&cpu->r[15]==expected&&link;
 log->records[log->count++]=r;log->pending=false;return ORDER_OK;
}
