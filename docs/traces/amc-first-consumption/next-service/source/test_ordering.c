#include "ordering.h"
#include "original-contexts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,device_reads;static order_log logstate,before_log;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"ordering:%u %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t forbidden(void*c,uint32_t a){(void)c;(void)a;device_reads++;return 0;}
static uint32_t offset(uint32_t va){if(va>=0xc0200000)return 0x400000+(va&0xfffff);if(va>=0xc0100000)return 0x300000+(va&0xfffff);if(va>=0x40000000)return 0x200000+(va&0xfffff);return va&0xfffff;}
static void put(uint8_t*r,unsigned o,uint32_t v){memcpy(r+o,&v,4);}
static void setup(arm_cpu_t*c,uint32_t pc){memset(c->r,0,sizeof c->r);c->cycles=1000+logstate.count*4;c->r[15]=pc;c->cpsr=pc==0xc01533c2?0xa0000033:pc==0xc0272224?0xa0000013:0xa0000010;c->r[4]=(pc==0x3123cdc8||pc==0x3123ce18)?0x40009000:0x40008000;c->r[0]=pc==0xc0272224?0x40007000:pc==0x3123f95c?0x40008000:0;c->r[1]=3;c->r[13]=0x4000a000;}
static void after(arm_cpu_t*c,uint32_t pc){c->cycles++;c->r[15]=pc==0x3123f95c?0x3123f2f8:pc+(pc==0xc01533c2?2:4);if(pc==0x3123f95c)c->r[14]=0x3123f960;}
int main(void){
 arm_cpu_t*c=calloc(1,sizeof*c),*before=malloc(sizeof*c);uint8_t*ram=calloc(1,0x500000);CHECK(c&&before&&ram);arm_bus_t bus={0};bus.read32=forbidden;c->bus=&bus;c->cp15.sctlr=1;c->cp15.ttbr0=0x10000000;c->cp15.dacr=3;
 put(ram,0x312*4,0x10000c02);put(ram,0x400*4,0x10200c02);put(ram,0xc01*4,0x10300c02);put(ram,0xc02*4,0x10400c02);
 put(ram,offset(0x400070e0),3);put(ram,offset(0x4000715c),0);put(ram,offset(0x40009050),0x40008000);put(ram,offset(0x40008008),0x123);put(ram,offset(0x4000a038),4);
 for(unsigned i=0;i<6;i++)memcpy(ram+offset(original_contexts[i].pc),original_contexts[i].bytes,32);
 for(unsigned i=0;i<6;i++){
  uint32_t pc=original_contexts[i].pc;setup(c,pc);*before=*c;
  CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_OK);CHECK(!memcmp(c,before,sizeof*c)&&!device_reads);CHECK(!memcmp(logstate.staged.code,original_contexts[i].bytes,32));
  if(pc==0x3123f7f0){CHECK(logstate.staged.object_bytes==144&&logstate.staged.stack_bytes==52);CHECK(logstate.staged.stack[48]==4);}
  if(pc==0x3123cdc8||pc==0x3123ce18)CHECK(logstate.staged.linked_address==0x40008000&&logstate.staged.linked_bytes==144);
  before_log=logstate;CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);CHECK(!memcmp(&logstate,&before_log,sizeof logstate));
  after(c,pc);CHECK(order_finish(&logstate,c)==ORDER_OK);CHECK(logstate.records[logstate.count-1].retired);
  /* A matching PC/opcode but wrong adjacent original code must be rejected. */
  setup(c,pc);ram[offset(pc)+31]^=1;before_log=logstate;*before=*c;
  CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);CHECK(!memcmp(c,before,sizeof*c));CHECK(!memcmp(&logstate,&before_log,sizeof logstate));ram[offset(pc)+31]^=1;
  c->cpsr^=0x20;CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);
  for(unsigned bad=0;bad<3;bad++){
   setup(c,pc);CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_OK);after(c,pc);
   if(bad==0)c->r[15]^=4;if(bad==1)c->cpsr^=1;if(bad==2){c->r[15]=0xffff0018;c->cpsr=0x92;}
   CHECK(order_finish(&logstate,c)==ORDER_OK);CHECK(!logstate.records[logstate.count-1].retired);
  }
 }
 setup(c,0x3123f95c);c->r[1]=2;CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);
 setup(c,0x3123f95c);CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_OK);after(c,0x3123f95c);c->r[14]^=4;CHECK(order_finish(&logstate,c)==ORDER_OK&&!logstate.records[logstate.count-1].retired);
 setup(c,0x3123f7f0);c->r[13]=0xffffffe0;CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);
 setup(c,0x3123f7f0);c->r[4]=0;CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);
 setup(c,0x3123cdc8);put(ram,offset(0x40009050),0xfffffff0);CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);put(ram,offset(0x40009050),0x40008000);
 setup(c,0xc0272224);c->r[0]=0x40007004;CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);
 setup(c,0x3123ce18);put(ram,0x312*4,0);CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);put(ram,0x312*4,0x10000c02);
 setup(c,0x3123ce18);CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_OK);before_log=logstate;CHECK(order_finish(&logstate,c)==ORDER_REJECT);CHECK(!memcmp(&logstate,&before_log,sizeof logstate));after(c,0x3123ce18);CHECK(order_finish(&logstate,c)==ORDER_OK);
 logstate.count=ORDER_CAP;before_log=logstate;CHECK(order_begin(&logstate,c,ram,0x10000000,0x500000,0x40007000)==ORDER_REJECT);CHECK(!memcmp(&logstate,&before_log,sizeof logstate));CHECK(!device_reads);
 free(c);free(before);free(ram);printf("MeCCA mapped-byte/ISA/return observation: %u synthetic checks passed; no guest execution\n",checks);
}
