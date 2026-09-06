#include "ordering.h"
#include <string.h>
bool order_target(uint32_t pc){switch(pc){case 0xc027602c:case 0xc027619c:case 0xc02761c0:case 0xc02761c4:case 0xc0274778:case 0xc0274878:return true;default:return false;}}
static uint32_t u32(const uint8_t*b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
order_result order_begin(order_log*log,const arm_cpu_t*cpu,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t driver){
 if(!log||!cpu||!ram||log->pending||log->count>=ORDER_CAP||!order_target(cpu->r[15])||(cpu->cpsr&0x3fu)!=0x13u||(uint64_t)driver+0x24f>0x100000000ull)return ORDER_REJECT;
 unsigned actual=cpu->r[15]==0xc0274778?0:cpu->r[15]==0xc0274878?4:5;
 if(cpu->r[actual]!=driver)return ORDER_REJECT;
 if(cpu->r[15]==0xc0274778&&cpu->r[1]>2)return ORDER_REJECT;
 uint8_t state[4],output[8],flags[2];
 if(!amc_peek_ram(cpu,ram,base,size,driver+0xe0,state,sizeof state)||!amc_peek_ram(cpu,ram,base,size,driver+0x15c,output,sizeof output)||!amc_peek_ram(cpu,ram,base,size,driver+0x24d,flags,sizeof flags))return ORDER_REJECT;
 order_record record={0};record.pc=cpu->r[15];record.cpsr=cpu->cpsr;memcpy(record.regs,cpu->r,sizeof record.regs);record.driver=driver;record.state=u32(state);record.output[0]=u32(output);record.output[1]=u32(output+4);record.flags=flags[0]|(uint32_t)flags[1]<<8;record.cycles=cpu->cycles;
 log->staged=record;log->pending=true;return ORDER_OK;
}

order_result order_finish(order_log*log,const arm_cpu_t*cpu){
 if(!log||!cpu||!log->pending||log->count>=ORDER_CAP||cpu->cycles!=log->staged.cycles+1)return ORDER_REJECT;
 order_record record=log->staged;record.after_pc=cpu->r[15];record.after_cpsr=cpu->cpsr;record.after_cycles=cpu->cycles;
 uint32_t expected=record.pc==0xc02761c0u?0xc0277284u:record.pc+4;
 record.retired=(cpu->cpsr&0x3fu)==0x13u&&cpu->r[15]==expected;
 log->records[log->count++]=record;log->pending=false;return ORDER_OK;
}
