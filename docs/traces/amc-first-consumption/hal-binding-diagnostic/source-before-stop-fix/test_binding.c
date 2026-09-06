#include "post.h"
#include "post-original.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,live_reads;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"binding:%u %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t forbidden(void*p,uint32_t a){(void)p;(void)a;live_reads++;return 0;}
static uint32_t setup_read(void*p,uint32_t a){uint32_t value=0;if(a>=0x10000000&&a+4ull<=0x10200000)memcpy(&value,(uint8_t*)p+a-0x10000000,4);return value;}
static void*noalloc(size_t n){(void)n;return NULL;}
static void put(uint8_t*r,uint32_t offset,uint32_t value){memcpy(r+offset,&value,4);}
int main(void){
 uint8_t*ram=calloc(1,0x200000),*copyram=malloc(0x200000);arm_cpu_t*c=calloc(1,sizeof *c),*before=malloc(sizeof *c);post_observer*o=calloc(1,sizeof *o),*old=malloc(sizeof *o);C(ram&&copyram&&c&&before&&o&&old);
 arm_bus_t bus={.read32=forbidden};c->bus=&bus;c->cpsr=0x60000010;c->cp15.sctlr=1;c->cp15.ttbr0=0x10000000;c->cp15.dacr=1;c->r[15]=0x31236650;c->r[13]=0x31201000;c->r[0]=0x31202000;c->cycles=9874747384ull;
 put(ram,0x312*4,0x10100c02);memcpy(ram+0x136650,post_original[2].bytes,32);binding_diagnostic d;binding_walk walk;
 #define IMMUTABLE_BEFORE() do{*before=*c;memcpy(copyram,ram,0x200000);}while(0)
 #define IMMUTABLE_AFTER() C(!memcmp(c,before,sizeof *c)&&!memcmp(ram,copyram,0x200000)&&!live_reads)
 IMMUTABLE_BEFORE();C(post_begin_diagnosed(o,c,ram,0x10000000,0x200000,&d));C(!d.reason&&d.walk[0].available&&!memcmp(d.walk[0].code,post_original[2].bytes,32));IMMUTABLE_AFTER();o->pending=false;
 ram[0x136650]^=1;*old=*o;IMMUTABLE_BEFORE();C(!post_begin_diagnosed(o,c,ram,0x10000000,0x200000,&d));C(d.reason==BIND_CODE&&d.walk[0].available&&d.walk[1].available&&d.walk[2].available&&d.walk[3].available&&!memcmp(o,old,sizeof *o));IMMUTABLE_AFTER();ram[0x136650]^=1;
 for(unsigned kind=0;kind<4;kind++){
  put(ram,0x312*4,kind==0?0:kind==1?0x10100402:kind==2?0x20000c02:0x10100c02);if(kind==3)c->cp15.ttbr0=0x38500000;
  IMMUTABLE_BEFORE();C(!post_begin_diagnosed(o,c,ram,0x10000000,0x200000,&d));C(d.reason==(kind<2?BIND_FAULT:kind==2?BIND_PHYSICAL:BIND_TABLE));C(!d.walk[0].available&&!memcmp(d.walk[0].code,(uint8_t[32]){0},32));IMMUTABLE_AFTER();c->cp15.ttbr0=0x10000000;
 }
 put(ram,0x312*4,0x10100c12);c->cp15.sctlr=1|(1u<<23);IMMUTABLE_BEFORE();C(binding_read(&walk,c,ram,0x10000000,0x200000,c->r[15],ARM_ACCESS_READ,true));C(!binding_read(&walk,c,ram,0x10000000,0x200000,c->r[15],ARM_ACCESS_FETCH,true)&&walk.reason==BIND_FAULT&&walk.span[0].fault);IMMUTABLE_AFTER();c->cp15.sctlr=1;
 /* A pre-existing cached READ is deliberately stale; fresh clone must differ.
  * Live cache population is fixture setup, never the observer under test. */
 put(ram,0x312*4,0x10100c02);arm_bus_t fixture={.ctx=ram,.read32=setup_read};c->bus=&fixture;uint32_t pa=0;C(!arm_mmu_translate(c,c->r[15],ARM_ACCESS_READ,false,&pa)&&pa==0x10136650);c->bus=&bus;put(ram,0x312*4,0);
 IMMUTABLE_BEFORE();C(binding_read(&walk,c,ram,0x10000000,0x200000,c->r[15],ARM_ACCESS_READ,false)&&!walk.words);C(!binding_read(&walk,c,ram,0x10000000,0x200000,c->r[15],ARM_ACCESS_READ,true)&&walk.words&&walk.reason==BIND_FAULT);IMMUTABLE_AFTER();arm_mmu_tlb_flush(c);
 put(ram,0x312*4,0x10004001);put(ram,0x4000,0x10008000|2|(3u<<4));IMMUTABLE_BEFORE();C(!binding_read(&walk,c,ram,0x10000000,0x200000,0x312003f0,ARM_ACCESS_READ,false));C(walk.spans==2&&walk.span[0].available&&!walk.span[1].available&&!memcmp(walk.code,(uint8_t[32]){0},32));IMMUTABLE_AFTER();
 IMMUTABLE_BEFORE();C(!binding_read_with_allocator(&walk,c,ram,0x10000000,0x200000,c->r[15],ARM_ACCESS_READ,false,noalloc,free)&&walk.reason==BIND_ALLOC&&!walk.spans);C(!binding_read(&walk,c,ram,0x10000000,0x200000,UINT32_MAX-5,ARM_ACCESS_READ,false)&&walk.reason==BIND_ARGUMENT);IMMUTABLE_AFTER();
 for(unsigned i=0;i<4;i++){memset(o,0,sizeof *o);c->cpsr=0x60000010;c->r[15]=0x31236650;if(i==0)o->pending=true;if(i==1)o->count=POST_CAP;if(i==2)c->cpsr|=32;if(i==3)c->r[15]+=4;*old=*o;IMMUTABLE_BEFORE();C(!post_begin_diagnosed(o,c,ram,0x10000000,0x200000,&d));C(d.reason==(i==0?BIND_PENDING:i==1?BIND_CAP:i==2?BIND_MODE:BIND_TARGET)&&!memcmp(o,old,sizeof *o));IMMUTABLE_AFTER();}
 printf("PASS %u binding actual-window/permission/fetch/cached-vs-fresh/atomic-output/live-immutability checks; synthetic only\n",checks);free(ram);free(copyram);free(c);free(before);free(o);free(old);return 0;
}
