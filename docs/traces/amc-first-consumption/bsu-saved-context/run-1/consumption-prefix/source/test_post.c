#include "post.h"
#include "post-original.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned n,calls;
#define C(x) do{n++;if(!(x)){fprintf(stderr,"post:%u %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t no(void*p,uint32_t a){(void)p;(void)a;calls++;return 0;}
static void put(uint8_t*r,unsigned a,uint32_t v){memcpy(r+a,&v,4);}
int main(void){
 uint8_t*ram=calloc(1,0x200000),*old=malloc(0x200000);arm_cpu_t*c=calloc(1,sizeof*c),*before=malloc(sizeof*c);post_observer*o=calloc(1,sizeof*o);C(ram&&old&&c&&before&&o);arm_bus_t bus={.read32=no};c->bus=&bus;c->cpsr=0x60000010;c->cp15.sctlr=1;c->cp15.ttbr0=0x10000000;c->cp15.dacr=1;put(ram,0x312*4,0x10100c02);
 for(unsigned i=0;i<6;i++)memcpy(ram+0x100000+(post_original[i].pc&0xfffff),post_original[i].bytes,32);
 c->r[13]=0x31201000;c->r[0]=0x31202000;put(ram,0x101010,0x3120aaa8);memcpy(old,ram,0x200000);
 for(unsigned i=0;i<6;i++){
  memset(o,0,sizeof*o);c->r[15]=post_original[i].pc;c->cycles=100;*before=*c;C(post_begin(o,c,ram,0x10000000,0x200000));C(!memcmp(c,before,sizeof*c)&&!memcmp(ram,old,0x200000)&&!calls);c->cycles++;c->r[15]=o->staged.expected_pc;c->cpsr=(c->cpsr&~63u)|o->staged.expected_mode;if(i==3)c->cpsr=0x20000010;C(post_finish(o,c));C(o->records[0].retired);c->cpsr=0x60000010;
 }
 c->r[15]=0x312288f0;c->cycles=200;memset(o,0,sizeof*o);C(post_begin(o,c,ram,0x10000000,0x200000));c->cycles++;c->r[15]+=8;C(post_finish(o,c));C(!o->records[0].retired);
 c->r[15]=0x312288f0;c->cycles=200;C(post_begin(o,c,ram,0x10000000,0x200000));c->cycles++;c->r[15]=0x18;c->cpsr=0x12;C(post_finish(o,c));C(!o->records[1].retired);
 c->r[15]=0x312288f0;c->cycles=200;c->cpsr=0x60000010;C(post_begin(o,c,ram,0x10000000,0x200000));c->cycles++;c->r[15]+=4;c->cpsr^=32;C(post_finish(o,c));C(!o->records[2].retired);
 c->cpsr=0x60000010;c->r[15]=0x312288f0;C(post_begin(o,c,ram,0x10000000,0x200000));c->cycles++;c->r[15]+=4;c->cpsr^=0x80000000u;C(post_finish(o,c));C(!o->records[3].retired);
 c->cpsr=0x60000010;c->r[15]=0x312288f0;ram[0x1288f0]^=1;C(!post_begin(o,c,ram,0x10000000,0x200000));ram[0x1288f0]^=1;
 c->cpsr^=32;C(!post_begin(o,c,ram,0x10000000,0x200000));c->cpsr^=32;c->cp15.ttbr0+=0x4000;C(!post_begin(o,c,ram,0x10000000,0x200000));c->cp15.ttbr0-=0x4000;
 c->r[13]=0xfffffff0;c->r[0]=0;C(post_begin(o,c,ram,0x10000000,0x200000));C(!o->staged.available);o->pending=false;c->r[15]=0x312367f8;C(!post_begin(o,c,ram,0x10000000,0x200000));
 c->r[13]=0x31201000;put(ram,0x101010,0x3120aaab);C(post_begin(o,c,ram,0x10000000,0x200000));C(o->staged.expected_pc==0x3120aaaa&&o->staged.expected_mode==0x30);c->cycles++;c->r[15]=0x3120aaaa;c->cpsr=0x60000030;C(post_finish(o,c));C(o->records[o->count-1].retired);
 C(post_fifo(0x3ca00010,4));C(post_fifo(0x3ca00013,1));C(post_fifo(0x3cd00038,2));C(post_fifo(0x3ca0000f,2));C(!post_fifo(0x3ca00014,4));C(!post_fifo(0x10000010,4));C(!post_fifo(0x3ca00010,8));C(!calls);
 printf("post-PCM code/permission/retirement/FIFO checks %u PASS; synthetic only\n",n);free(ram);free(old);free(c);free(before);free(o);
}
