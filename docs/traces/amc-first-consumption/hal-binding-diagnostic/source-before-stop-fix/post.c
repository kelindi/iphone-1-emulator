#include "post.h"
#include <string.h>
#include "post-original.h"
static uint32_t word(const uint8_t*b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
bool post_target(uint32_t pc){for(unsigned i=0;i<6;i++)if(post_original[i].pc==pc)return true;return false;}
bool post_fifo(uint32_t a,unsigned n){if(n!=1&&n!=2&&n!=4)return false;uint32_t base[]={0x3ca00010,0x3ca00038,0x3cd00010,0x3cd00038};for(unsigned i=0;i<4;i++)if((uint64_t)a<base[i]+4ull&&(uint64_t)a+n>base[i])return true;return false;}
bool post_begin_diagnosed(post_observer*o,const arm_cpu_t*c,const uint8_t*ram,uint32_t base,uint32_t size,binding_diagnostic*d){
 if(d){memset(d,0,sizeof *d);d->attempted=1;if(c){d->pc=c->r[15];d->cpsr=c->cpsr;memcpy(d->regs,c->r,64);d->cycles=c->cycles;d->sctlr=c->cp15.sctlr;d->ttbr0=c->cp15.ttbr0;d->ttbr1=c->cp15.ttbr1;d->ttbcr=c->cp15.ttbcr;d->dacr=c->cp15.dacr;d->context_id=c->cp15.context_id;d->dfsr=c->cp15.dfsr;d->ifsr=c->cp15.ifsr;d->dfar=c->cp15.dfar;d->ifar=c->cp15.ifar;d->target=post_target(c->r[15]);}if(o){d->count=o->count;d->pending=o->pending;}}
 if(!o||!c||o->pending||o->count>=POST_CAP||(c->cpsr&63)!=16||!post_target(c->r[15])){if(d)d->reason=!o||!c?BIND_ARGUMENT:o->pending?BIND_PENDING:o->count>=POST_CAP?BIND_CAP:(c->cpsr&63)!=16?BIND_MODE:BIND_TARGET;return false;}
 post_record r={0};r.pc=c->r[15];r.cpsr=c->cpsr;r.cycles=c->cycles;memcpy(r.regs,c->r,64);
 bool available;if(d){for(unsigned i=0;i<6;i++)if(post_original[i].pc==r.pc){memcpy(d->expected,post_original[i].bytes,32);d->expected_available=1;}available=binding_read(&d->walk[0],c,ram,base,size,r.pc,ARM_ACCESS_READ,false);if(available)memcpy(r.code,d->walk[0].code,32);}else available=amc_peek_ram(c,ram,base,size,r.pc,r.code,32);
 bool match=false;if(available)for(unsigned i=0;i<6;i++)if(post_original[i].pc==r.pc)match=!memcmp(r.code,post_original[i].bytes,32);
 if(!available||!match){if(d){d->reason=available?BIND_CODE:d->walk[0].reason;binding_read(&d->walk[1],c,ram,base,size,r.pc,ARM_ACCESS_FETCH,false);binding_read(&d->walk[2],c,ram,base,size,r.pc,ARM_ACCESS_READ,true);binding_read(&d->walk[3],c,ram,base,size,r.pc,ARM_ACCESS_FETCH,true);}return false;}
 r.expected_pc=r.pc+4;r.expected_mode=r.cpsr&63;
 if(amc_peek_ram(c,ram,base,size,c->r[13],r.stack,64))r.available|=1;
 uint32_t object=c->r[0];if(r.pc==0x312366a0)object=c->r[4];if(r.pc==0x31236734&&r.available&1)object=word(r.stack+8);if(r.pc==0x312367f8)object=c->r[6];
 if(object&&amc_peek_ram(c,ram,base,size,object,r.object,64))r.available|=2;
 if(r.pc==0x31236734&&(r.available&1)){uint32_t output=word(r.stack);if(output&&amc_peek_ram(c,ram,base,size,output,r.output,16))r.available|=4;}
 if(r.pc==0x312367f8){if(!(r.available&1))return false;uint32_t ret=word(r.stack+16);r.expected_pc=ret&~1u;r.expected_mode=16|(ret&1?32:0);if(!ret||(!(ret&1)&&(ret&3)))return false;}
 o->staged=r;o->pending=true;return true;
}
bool post_finish(post_observer*o,const arm_cpu_t*c){if(!o||!c||!o->pending||o->count>=POST_CAP||c->cycles!=o->staged.cycles+1)return false;post_record r=o->staged;r.after_pc=c->r[15];r.after_cpsr=c->cpsr;r.after_lr=c->r[14];r.after_cycles=c->cycles;uint32_t flags=(r.cpsr&~32u)|(r.expected_mode&32u);if(r.pc==0x312366a0)flags=(flags&0x0fffffffu)|0x20000000u|(r.regs[0]&0x80000000u)|(r.regs[0]?0:0x40000000u);r.retired=r.after_pc==r.expected_pc&&r.after_cpsr==flags;o->records[o->count++]=r;o->pending=false;return true;}

bool post_begin(post_observer*o,const arm_cpu_t*c,const uint8_t*ram,uint32_t base,uint32_t size){return post_begin_diagnosed(o,c,ram,base,size,NULL);}
