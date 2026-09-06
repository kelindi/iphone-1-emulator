#include "destination.h"
#include "reader-original.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,live;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"destination:%u %s\n",__LINE__,#x);exit(1);}}while(0)
static void put(uint8_t*r,uint32_t at,uint32_t v){memcpy(r+at,&v,4);}
static uint32_t forbidden(void*c,uint32_t a){(void)c;(void)a;live++;return 0;}
static uint8_t expected[4096];
int main(void){
 uint8_t*ram=calloc(1,0x500000),*saved=malloc(0x500000);arm_cpu_t*c=calloc(1,sizeof*c),*before=malloc(sizeof*c);CHECK(ram&&saved&&c&&before);
 arm_bus_t bus={.read32=forbidden};c->bus=&bus;c->cp15.sctlr=1;c->cp15.ttbr0=0x10000000;c->cp15.dacr=1;c->cpsr=0x60000010;c->cycles=100;
 /* User domain/AP3 code and data. Source maps AMC, outside ordinary RAM. */
 put(ram,0x300*4,0x10100c02);put(ram,0x400*4,0x10200c02);put(ram,0x500*4,0x22000c02);put(ram,0x600*4,0x10300c02);
 memcpy(ram+0x19a1bc,reader_prologue,32);memcpy(ram+0x19a20c,reader_load,32);
 c->r[15]=0x3009a20c;c->r[0]=0x40008000;c->r[1]=0x5000f220;c->r[2]=4032;c->r[7]=0x4000a00c;c->r[13]=0x40009ff0;c->r[14]=0x60000200;
 put(ram,0x20a000,c->r[0]);put(ram,0x20a010,c->r[14]);
 for(unsigned i=0;i<4096;i++)expected[i]=(uint8_t)(i*7+11);memcpy(ram+0x208000,expected,4096);
 destination_observer o={0};*before=*c;memcpy(saved,ram,0x500000);
 CHECK(destination_begin(&o,c,ram,0x10000000,0x500000));CHECK(!memcmp(c,before,sizeof*c)&&!memcmp(ram,saved,0x500000)&&!live);
 CHECK(o.captured&&o.caller_available&&o.caller_address==0x600001fc&&o.map_count==4);CHECK(o.regs[0]==0x40008000&&o.source_physical==0x2200f220);
 /* Actual current CPU may now be in an unrelated privileged context. */
 c->cpsr=0x13;c->cp15.ttbr0=0x10004000;*before=*c;
 CHECK(destination_finish(&o,ram,0x10000000,0x500000,expected,200));CHECK(!memcmp(o.bytes,expected,4096));CHECK(!memcmp(c,before,sizeof*c)&&!memcmp(ram,saved,0x500000)&&!live);CHECK(!destination_finish(&o,ram,0x10000000,0x500000,expected,200));destination_destroy(&o);
 c->cpsr=0x60000010;c->cp15.ttbr0=0x10000000;
 #define BEGIN() destination_begin(&o,c,ram,0x10000000,0x500000)
 #define RESET() do{destination_destroy(&o);memset(&o,0,sizeof o);}while(0)
 RESET();c->cpsr^=0x20;CHECK(!BEGIN());c->cpsr^=0x20;
 c->cp15.ttbr0=0x10004000;CHECK(!BEGIN());c->cp15.ttbr0=0x10000000;
 ram[0x19a20c+31]^=1;CHECK(!BEGIN());ram[0x19a20c+31]^=1;
 ram[0x19a1bc+3]^=1;CHECK(!BEGIN());ram[0x19a1bc+3]^=1;
 c->r[2]=4031;CHECK(!BEGIN());c->r[2]=4032;c->r[1]+=4;CHECK(!BEGIN());c->r[1]-=4;
 c->r[0]=0xfffff800;CHECK(!BEGIN());c->r[0]=0x40008000;c->r[7]=8;CHECK(!BEGIN());c->r[7]=0x4000a00c;
 put(ram,0x20a010,c->r[14]+4);CHECK(!BEGIN());put(ram,0x20a010,c->r[14]);
 put(ram,0x400*4,0x10200402);CHECK(!BEGIN());put(ram,0x400*4,0x10200c02);
 CHECK(BEGIN());put(ram,0x400*4,0x10300c02);CHECK(!destination_finish(&o,ram,0x10000000,0x500000,expected,200));put(ram,0x400*4,0x10200c02);
 ram[0x208000+123]^=1;CHECK(!destination_finish(&o,ram,0x10000000,0x500000,expected,200));ram[0x208000+123]^=1;
 CHECK(destination_finish(&o,ram,0x10000000,0x500000,expected,200));RESET();
 /* Raw saved LR remains valid evidence when caller image bytes unavailable. */
 put(ram,0x600*4,0);CHECK(BEGIN());CHECK(!o.caller_available&&o.caller_address==0x600001fc);for(unsigned i=0;i<32;i++)CHECK(!o.caller[i]);RESET();
 CHECK(!live);CHECK(!memcmp(ram+0x208000,expected,4096));free(c);free(before);free(ram);free(saved);
 printf("actual user copy destination/address-space: %u PASS; synthetic MMU only\n",checks);
}
