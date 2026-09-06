#include "ordering.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,device_reads;static order_log logstate,before_log;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"ordering:%u %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t forbidden(void*c,uint32_t a){(void)c;(void)a;device_reads++;return 0;}
int main(void){
 arm_cpu_t*cpu=calloc(1,sizeof*cpu),*before=malloc(sizeof*cpu);uint8_t*ram=calloc(1,0x200000);CHECK(cpu&&before&&ram);arm_bus_t bus={0};bus.read32=forbidden;cpu->bus=&bus;cpu->cp15.sctlr=1;cpu->cp15.ttbr0=0x10000000;cpu->cp15.dacr=3;cpu->cpsr=0xa0000013;cpu->cycles=100;
 uint32_t section=0x10000c02;memcpy(ram+0x400*4,&section,4);uint32_t driver=0x40008000;uint32_t values[3]={3,1,0};memcpy(ram+0x80e0,values,4);memcpy(ram+0x815c,values+1,8);ram[0x824d]=1;ram[0x824e]=0;
 uint32_t saved_return=0xc01400ed;memcpy(ram+0x9004,&saved_return,4);cpu->r[7]=0x40009000;
 uint32_t pcs[]={0xc027602c,0xc0276030,0xc0276040,0xc027604c,0xc0274778,0xc0274878};
 for(unsigned i=0;i<6;i++){
  cpu->cpsr=0x60000013;cpu->r[15]=pcs[i];cpu->r[0]=cpu->r[4]=cpu->r[5]=driver;cpu->r[1]=2;*before=*cpu;
  CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_OK);CHECK(logstate.pending&&logstate.count==i);CHECK(!memcmp(cpu,before,sizeof*cpu)&&!device_reads);
  CHECK(logstate.staged.state==3&&logstate.staged.output[0]==1&&logstate.staged.output[1]==0&&logstate.staged.flags==1);
  cpu->cycles++;cpu->r[15]=pcs[i]==0xc0276030?0xc0276040:pcs[i]==0xc027604c?saved_return&~1u:pcs[i]+4;if(pcs[i]==0xc027604c)cpu->cpsr=0x33;CHECK(order_finish(&logstate,cpu)==ORDER_OK);CHECK(logstate.records[i].retired&&logstate.count==i+1&&!logstate.pending);
 }
 cpu->r[15]=0xc0274778;cpu->r[0]=driver;before_log=logstate;
 cpu->cpsr=0x33;CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_REJECT);cpu->cpsr=0x12;CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_REJECT);cpu->cpsr=0x13;
 cpu->r[1]=3;CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_REJECT);cpu->r[1]=0;
 CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver+4)==ORDER_REJECT);CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x1000,driver)==ORDER_REJECT);CHECK(!memcmp(&logstate,&before_log,sizeof logstate));
 CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_OK);before_log=logstate;CHECK(order_finish(&logstate,cpu)==ORDER_REJECT);CHECK(!memcmp(&logstate,&before_log,sizeof logstate));
 cpu->cycles++;cpu->r[15]=0xffff0018;cpu->cpsr=0x92;CHECK(order_finish(&logstate,cpu)==ORDER_OK);CHECK(!logstate.records[6].retired&&logstate.count==7);

 for(unsigned which=0;which<2;which++){
  cpu->r[15]=0xc027604c;cpu->r[5]=driver;cpu->cpsr=0x13;
  CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_OK);
  cpu->cycles++;cpu->r[15]=which?saved_return&~1u:0xc0276050;cpu->cpsr=which?0x13:0x33;
  CHECK(order_finish(&logstate,cpu)==ORDER_OK);CHECK(!logstate.records[logstate.count-1].retired);
 }

 cpu->r[15]=0xc0276030;cpu->cpsr=0x13;CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_OK);cpu->cycles++;cpu->r[15]=0xc0276034;CHECK(order_finish(&logstate,cpu)==ORDER_OK&&logstate.records[logstate.count-1].retired);
 saved_return=0xc027619c;memcpy(ram+0x9004,&saved_return,4);cpu->r[15]=0xc027604c;cpu->cpsr=0x13;CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_OK);cpu->cycles++;cpu->r[15]=saved_return;CHECK(order_finish(&logstate,cpu)==ORDER_OK&&logstate.records[logstate.count-1].retired);
 cpu->r[15]=0xc027602c;cpu->r[7]=0xfffffffc;before_log=logstate;CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_REJECT);CHECK(!memcmp(&logstate,&before_log,sizeof logstate));cpu->r[7]=0x40009000;
 logstate.count=ORDER_CAP;before_log=logstate;cpu->r[15]=0xc0274778;cpu->cpsr=0x13;CHECK(order_begin(&logstate,cpu,ram,0x10000000,0x200000,driver)==ORDER_REJECT);CHECK(!memcmp(&logstate,&before_log,sizeof logstate));CHECK(!device_reads);
 free(cpu);free(before);free(ram);printf("ordering observations: %u checks passed; synthetic records only\n",checks);
}
