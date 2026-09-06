#include "prefetch.h"
#include "post.h"
#include "post-original.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned checks,reads,badreads;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"prefetch:%u %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t rd(void*p,uint32_t a){reads++;uint32_t v=0;if(a>=0x10000000&&a+4ull<=0x10200000)memcpy(&v,(char*)p+a-0x10000000,4);else badreads++;return v;}
static void put(uint8_t*r,unsigned a,uint32_t v){memcpy(r+a,&v,4);}
static void setup(arm_cpu_t*c,uint8_t*r,arm_bus_t*b){memset(c,0,sizeof *c);memset(r,0,0x200000);c->bus=b;c->cpsr=0x60000010;c->cp15.sctlr=1;c->cp15.ttbr0=0x10000000;c->cp15.dacr=1;c->r[15]=0x31236650;c->r[13]=0x31201000;c->r[0]=0x31202000;c->cp15.ifsr=5;c->cp15.ifar=0x12345678;c->cycles=9874747384ull;put(r,0x312*4,0x10004001);}
static void diag(binding_diagnostic*d,arm_cpu_t*c,uint8_t*r){post_observer o={0};post_binding_probe(&o,c,r,0x10000000,0x200000,d);if(d->reason==BIND_OK)for(unsigned i=1;i<4;i++)binding_read(&d->walk[i],c,r,0x10000000,0x200000,c->r[15],i&1?ARM_ACCESS_FETCH:ARM_ACCESS_READ,i>=2);}
int main(void){
 uint8_t*r=calloc(1,0x200000),*ramcopy=malloc(0x200000);arm_cpu_t*c=calloc(1,sizeof*c),*before=malloc(sizeof*c),*after=malloc(sizeof*c);fetch_observer*o=calloc(1,sizeof*o),*old=malloc(sizeof*o);C(r&&ramcopy&&c&&before&&after&&o&&old);arm_bus_t b={.ctx=r,.read32=rd};binding_diagnostic d;
 for(unsigned scenario=0;scenario<8;scenario++){
  setup(c,r,&b);memset(o,0,sizeof*o);c->cp15.sctlr|=(scenario&1?ARM_SCTLR_V:0)|(scenario&2?ARM_SCTLR_EE:0);if(scenario>=4)c->irq_line=1;if(scenario>=6)c->fiq_line=1;diag(&d,c,r);*before=*c;memcpy(ramcopy,r,0x200000);reads=badreads=0;
  C(fetch_prepare(o,c,&d)==FETCH_ALLOW);C(!memcmp(c,before,sizeof*c)&&!memcmp(r,ramcopy,0x200000)&&!reads);
  guest_step step={.cycles=c->cycles,.pc=c->r[15],.cpsr=c->cpsr,.returned=true};C(arm_step(c)==ARM_OK);C(!badreads&&!memcmp(r,ramcopy,0x200000));C(reads==(scenario<4?2:0));*after=*c;*old=*o;
  for(unsigned wrong=0;wrong<8;wrong++){
   *c=*after;guest_step s=step;switch(wrong){case 0:c->r[15]^=4;break;case 1:c->cpsr^=32;break;case 2:c->r[14]^=4;break;case 3:c->spsr[scenario<4?ARM_BANK_ABT:scenario<6?ARM_BANK_IRQ:ARM_BANK_FIQ]^=1;break;case 4:c->cp15.ifar^=4;break;case 5:s.returned=false;break;case 6:c->cycles++;break;case 7:s.active=true;break;}
   C(!fetch_finish(o,c,&s,false)&&!memcmp(o,old,sizeof*o));
  }
  *c=*after;C(!fetch_finish(o,c,&step,true)&&!memcmp(o,old,sizeof*o));C(fetch_finish(o,c,&step,false));C(o->count==1&&o->faults==(scenario<4)&&o->records[0].kind==(scenario<4?1:scenario<6?2:3));
 }
 for(unsigned wrong=0;wrong<16;wrong++){
  setup(c,r,&b);memset(o,0,sizeof*o);diag(&d,c,r);
  switch(wrong){case 0:c->fetch_host=r;c->fetch_blk=c->r[15]&~0x3ffu;c->fetch_gen=c->tlb_gen;c->fetch_priv=0;break;case 1:d.cpsr|=32;break;case 2:break;default:break;}
  /* Each negative changes one actual admission input. */
  if(wrong==2)d.ttbr0^=0x4000;if(wrong==3)d.walk[2].span[0].fault=5;if(wrong==4)d.walk[3].table[1].value=2;if(wrong==5)d.walk[0].available=1;if(wrong==6)d.regs[0]++;if(wrong==7)d.reason=BIND_CODE;if(wrong==8)o->count=8;if(wrong==9)o->pending=true;if(wrong==10)d.walk[3].fresh=0;if(wrong==11)d.expected_available=0;if(wrong==12)d.walk[1].table[1].available=0;if(wrong==13)d.walk[1].words=1;if(wrong==14)d.walk[1].span[0].va+=4;if(wrong==15)d.walk[1].span[0].pa=0x10008000;
  *before=*c;*old=*o;memcpy(ramcopy,r,0x200000);reads=0;C(fetch_prepare(o,c,&d)==FETCH_REFUSE);C(!memcmp(o,old,sizeof*o)&&!memcmp(c,before,sizeof*c)&&!memcmp(r,ramcopy,0x200000)&&!reads);
 }
 /* A matching live fast-fetch entry must never be silently flushed. */
 setup(c,r,&b);c->fetch_host=r;c->fetch_blk=c->r[15]&~0x3ffu;c->fetch_gen=c->tlb_gen;c->fetch_priv=0;C(fetch_cache_hit(c));c->fetch_blk^=0x400;C(!fetch_cache_hit(c));c->fetch_blk^=0x400;c->fetch_gen++;C(!fetch_cache_hit(c));c->fetch_gen--;c->fetch_priv=1;C(!fetch_cache_hit(c));
 /* Test-owned page-in AFTER a real core abort, never a guest page-in claim. */
 setup(c,r,&b);memset(o,0,sizeof*o);diag(&d,c,r);*before=*c;C(fetch_prepare(o,c,&d)==FETCH_ALLOW);guest_step s={.cycles=c->cycles,.pc=c->r[15],.cpsr=c->cpsr,.returned=true};C(arm_step(c)==ARM_OK&&fetch_finish(o,c,&s,false));
 *c=*before;c->cycles+=100;put(r,0x4000+((c->r[15]>>10)&0x3fc),0x10100ff2);memcpy(r+0x100650,post_original[2].bytes,32);diag(&d,c,r);C(d.reason==BIND_OK);*before=*c;*old=*o;memcpy(ramcopy,r,0x200000);C(fetch_prepare(o,c,&d)==FETCH_MAPPED&&o->mapped&&!o->pending);C(!memcmp(c,before,sizeof*c)&&!memcmp(r,ramcopy,0x200000));
 for(unsigned wrong=0;wrong<6;wrong++){*o=*old;binding_diagnostic x=d;if(wrong==0)x.walk[0].code[0]^=1;if(wrong==1)x.walk[3].available=0;if(wrong==2){x.cycles=c->cycles= o->initial.cycles+1000001;}if(wrong==3)x.regs[0]^=1;if(wrong==4)o->faults=0;if(wrong==5)x.context_id^=1;fetch_observer snapshot=*o;C(fetch_prepare(o,c,&x)==FETCH_REFUSE&&!memcmp(o,&snapshot,sizeof*o));*c=*before;}
 printf("PASS %u real core ABT/IRQ/FIQ, live fetch-cache refusal, immutable diagnostic and test-owned mapped-retry checks\n",checks);free(r);free(ramcopy);free(c);free(before);free(after);free(o);free(old);return 0;
}
