#include "ordering.h"
#include "original-contexts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,live_reads;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"permission:%u %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t forbidden(void*c,uint32_t a){(void)c;(void)a;live_reads++;return 0;}
static void put(uint8_t*r,unsigned o,uint32_t v){memcpy(r+o,&v,4);}
int main(void){
 arm_cpu_t*c=calloc(1,sizeof*c),*before=malloc(sizeof*c);uint8_t*ram=calloc(1,0x400000);order_log*l=calloc(1,sizeof*l);CHECK(c&&before&&ram&&l);
 arm_bus_t bus={.read32=forbidden};c->bus=&bus;c->cp15.sctlr=1;c->cp15.ttbr0=0x10000000;c->cp15.dacr=1;c->cpsr=ARM_MODE_USR;
 /* Client domain, AP3 user code/data; AP1 kernel driver. No manager bypass. */
 put(ram,0x312*4,0x10100c02);put(ram,0x400*4,0x10200c02);put(ram,0xc01*4,0x10300402);
 uint32_t pc=0x3123f7f0,driver=0xc0107000;
 for(unsigned i=0;i<6;i++)if(original_contexts[i].pc==pc)memcpy(ram+0x13f7f0,original_contexts[i].bytes,32);
 c->r[15]=pc;c->r[4]=0x40008000;c->r[13]=0x4000a000;c->cycles=100;
 put(ram,0x3070e0,3);put(ram,0x20a038,4);*before=*c;
 uint8_t data[32];memset(data,0xa5,sizeof data);
 CHECK(amc_peek_ram(c,ram,0x10000000,0x400000,pc,data,32));
 memset(data,0xa5,sizeof data);CHECK(!amc_peek_ram(c,ram,0x10000000,0x400000,driver+0xe0,data,4));CHECK(data[0]==0xa5);
 CHECK(!memcmp(c,before,sizeof*c)&&!live_reads);
 #ifdef EXPECT_OLD
 CHECK(order_begin(l,c,ram,0x10000000,0x400000,driver)==ORDER_REJECT);CHECK(!l->pending&&!l->count);
 #else
 CHECK(order_begin(l,c,ram,0x10000000,0x400000,driver)==ORDER_OK);CHECK(l->staged.flags==ORDER_KERNEL_FIELDS_UNAVAILABLE&&l->staged.state==0&&l->staged.output[0]==0&&l->staged.output[1]==0);
 CHECK(l->staged.stack_bytes==52&&l->staged.stack[48]==4);CHECK(!memcmp(c,before,sizeof*c)&&!live_reads);
 c->cycles++;c->r[15]+=4;CHECK(order_finish(l,c)==ORDER_OK&&l->records[0].retired);
 /* Denied actual user code/data must still fail, with no permission override. */
 c->r[15]=pc;put(ram,0x312*4,0x10100402);arm_mmu_tlb_flush(c);CHECK(order_begin(l,c,ram,0x10000000,0x400000,driver)==ORDER_REJECT);
 put(ram,0x312*4,0x10100c02);put(ram,0x400*4,0x10200402);arm_mmu_tlb_flush(c);CHECK(order_begin(l,c,ram,0x10000000,0x400000,driver)==ORDER_REJECT);
 #endif
 CHECK(!live_reads);free(c);free(before);free(ram);free(l);
 printf("user/kernel permission observer: %u PASS (%s); no guest\n",checks,
 #ifdef EXPECT_OLD
 "original correctly reproduced rejection"
 #else
 "corrected preserves real user permissions"
 #endif
 );
}
