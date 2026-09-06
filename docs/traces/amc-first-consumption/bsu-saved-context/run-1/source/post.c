#include "post.h"
#include <string.h>
#include "post-original.h"
static uint32_t word(const uint8_t*b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
bool post_target(uint32_t pc){for(unsigned i=0;i<6;i++)if(post_original[i].pc==pc)return true;return false;}
bool post_fifo(uint32_t a,unsigned n){if(n!=1&&n!=2&&n!=4)return false;uint32_t base[]={0x3ca00010,0x3ca00038,0x3cd00010,0x3cd00038};for(unsigned i=0;i<4;i++)if((uint64_t)a<base[i]+4ull&&(uint64_t)a+n>base[i])return true;return false;}
bool post_begin(post_observer*o,const arm_cpu_t*c,const uint8_t*ram,uint32_t base,uint32_t size){
 if(!o||!c||o->pending||o->count>=POST_CAP||(c->cpsr&63)!=16||!post_target(c->r[15]))return false;
 post_record r={0};r.pc=c->r[15];r.cpsr=c->cpsr;r.cycles=c->cycles;memcpy(r.regs,c->r,64);
 if(!amc_peek_ram(c,ram,base,size,r.pc,r.code,32))return false;bool match=false;for(unsigned i=0;i<6;i++)if(post_original[i].pc==r.pc)match=!memcmp(r.code,post_original[i].bytes,32);if(!match)return false;
 r.expected_pc=r.pc+4;r.expected_mode=r.cpsr&63;
 if(amc_peek_ram(c,ram,base,size,c->r[13],r.stack,64))r.available|=1;
 uint32_t object=c->r[0];if(r.pc==0x312366a0)object=c->r[4];if(r.pc==0x31236734&&r.available&1)object=word(r.stack+8);if(r.pc==0x312367f8)object=c->r[6];
 if(object&&amc_peek_ram(c,ram,base,size,object,r.object,64))r.available|=2;
 if(r.pc==0x31236734&&(r.available&1)){uint32_t output=word(r.stack);if(output&&amc_peek_ram(c,ram,base,size,output,r.output,16))r.available|=4;}
 if(r.pc==0x312367f8){if(!(r.available&1))return false;uint32_t ret=word(r.stack+16);r.expected_pc=ret&~1u;r.expected_mode=16|(ret&1?32:0);if(!ret||(!(ret&1)&&(ret&3)))return false;}
 o->staged=r;o->pending=true;return true;
}
bool post_finish(post_observer*o,const arm_cpu_t*c){if(!o||!c||!o->pending||o->count>=POST_CAP||c->cycles!=o->staged.cycles+1)return false;post_record r=o->staged;r.after_pc=c->r[15];r.after_cpsr=c->cpsr;r.after_lr=c->r[14];r.after_cycles=c->cycles;uint32_t flags=(r.cpsr&~32u)|(r.expected_mode&32u);if(r.pc==0x312366a0)flags=(flags&0x0fffffffu)|0x20000000u|(r.regs[0]&0x80000000u)|(r.regs[0]?0:0x40000000u);r.retired=r.after_pc==r.expected_pc&&r.after_cpsr==flags;o->records[o->count++]=r;o->pending=false;return true;}
