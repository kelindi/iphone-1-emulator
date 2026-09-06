#include "destination.h"
#include "reader-original.h"
#include <stdlib.h>
#include <string.h>
static uint32_t word(const uint8_t*b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static bool map_destination(const arm_cpu_t*c,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t va,uint32_t*maps,uint32_t*lengths,uint32_t*count){
 if((uint64_t)va+4096>0x100000000ull)return false;
 unsigned at=0,n=0;
 while(at<4096){uint32_t pa;unsigned part=1024-((va+at)&1023);if(part>4096-at)part=4096-at;
  if(n>=5||!amc_peek_translate(c,ram,base,size,va+at,&pa)||pa<base||(uint64_t)pa+part>(uint64_t)base+size)return false;
  maps[n]=pa;lengths[n++]=part;at+=part;
 }*count=n;return true;
}
bool destination_begin(destination_observer*o,const arm_cpu_t*c,const uint8_t*ram,uint32_t base,uint32_t size){
 if(!o||!c||!ram||o->captured||o->context||(c->cpsr&0x3f)!=0x10||c->r[15]!=0x3009a20c||c->r[2]!=4032||c->r[7]<12||(uint64_t)c->r[7]+8>0x100000000ull)return false;
 destination_observer v={0};v.context=malloc(sizeof *v.context);if(!v.context)return false;
 *v.context=*c;arm_mmu_tlb_flush(v.context); /* private context; real user permissions */
 if(!amc_peek_ram(v.context,ram,base,size,c->r[15],v.code,32)||memcmp(v.code,reader_load,32)||
 !amc_peek_ram(v.context,ram,base,size,0x3009a1bc,v.prologue,32)||memcmp(v.prologue,reader_prologue,32)||
 !amc_peek_translate(v.context,ram,base,size,c->r[1],&v.source_physical)||v.source_physical!=0x2200f220||
 !amc_peek_ram(v.context,ram,base,size,c->r[7]-12,v.frame,20)||word(v.frame)!=c->r[0]||word(v.frame+16)!=c->r[14]||
 !map_destination(v.context,ram,base,size,c->r[0],v.maps,v.lengths,&v.map_count))goto reject;
 /* The original prologue proves saved destination at r7-12 and LR at r7+4.
  * LR is an actual value, never a guessed image-relative pointer. Its image
  * identity is separate; unmapped caller bytes are explicitly unavailable. */
 uint32_t ret=c->r[14]&~1u;
 if(ret>=4){v.caller_address=ret-4;v.caller_available=amc_peek_ram(v.context,ram,base,size,v.caller_address,v.caller,32);}
 v.captured=true;v.cycles=c->cycles;memcpy(v.regs,c->r,sizeof v.regs);v.cpsr=c->cpsr;
 v.sctlr=c->cp15.sctlr;v.ttbr0=c->cp15.ttbr0;v.ttbr1=c->cp15.ttbr1;v.ttbcr=c->cp15.ttbcr;v.dacr=c->cp15.dacr;
 *o=v;return true;
reject:free(v.context);return false;
}
bool destination_finish(destination_observer*o,const uint8_t*ram,uint32_t base,uint32_t size,const uint8_t*expected,uint64_t cycles){
 if(!o||!o->captured||o->finished||!o->context||!expected||cycles<o->cycles)return false;
 arm_cpu_t*c=malloc(sizeof *c);if(!c)return false;*c=*o->context;arm_mmu_tlb_flush(c);
 uint32_t maps[5],lengths[5],count;uint8_t bytes[4096];
 bool ok=map_destination(c,ram,base,size,o->regs[0],maps,lengths,&count)&&count==o->map_count&&
 !memcmp(maps,o->maps,count*4)&&!memcmp(lengths,o->lengths,count*4)&&
 amc_peek_ram(c,ram,base,size,o->regs[0],bytes,sizeof bytes)&&!memcmp(bytes,expected,sizeof bytes);
 free(c);if(!ok)return false;
 memcpy(o->bytes,bytes,sizeof bytes);o->finished=true;o->finished_cycles=cycles;return true;
}
void destination_destroy(destination_observer*o){if(o){free(o->context);o->context=NULL;}}
