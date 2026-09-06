#include "observer.h"
#include "peek.h"
#include <string.h>
static bool site_ok(const arm_cpu_t*c,const uint8_t*r,uint32_t b,uint32_t n,const qualified_site*s){
 uint8_t code[32];if(!c||!r||!s||c->r[15]!=s->pc||(c->cpsr&63u)!=s->mode||s->pc>UINT32_MAX-31u||sizeof code>4096u-(s->pc&4095u))return false;
 if((s->mode&ARM_CPSR_T)?(s->pc&1u):(s->pc&3u))return false;
 return amc_peek_ram(c,r,b,n,s->pc,code,sizeof code)&&!memcmp(code,s->code,sizeof code);
}
static bool word(const arm_cpu_t*c,const uint8_t*r,uint32_t b,uint32_t n,uint32_t p,uint32_t*v){return !(p&3u)&&amc_peek_ram(c,r,b,n,p,v,4);}
static entry_observation row(const arm_cpu_t*c){entry_observation x={0};x.pc=c->r[15];x.cpsr=c->cpsr;x.lr=c->r[14];x.cycles=c->cycles;memcpy(x.args,c->r,16);return x;}
bool observe_kernel_target(const arm_cpu_t*c,const uint8_t*r,uint32_t b,uint32_t n,const qualified_site*s,const qualified_process*p,entry_observation*out){
 if(!p||!out||!s||(s->mode&31u)!=ARM_MODE_SVC||!site_ok(c,r,b,n,s)||c->r[0]!=p->proc||p->proc<0xc0000000u||p->proc>0xfffff000u||(p->proc&3u)||!p->pid||!p->task)return false;
 uint32_t pid,task;char name[16];
 if(!word(c,r,b,n,p->proc+0x24u,&pid)||!word(c,r,b,n,p->proc+0x138u,&task)||!amc_peek_ram(c,r,b,n,p->proc+0xebu,name,sizeof name)||pid!=p->pid||task!=p->task||memcmp(name,p->name,16))return false;
 entry_observation x=row(c);x.proc=p->proc;x.pid=pid;x.task=task;memcpy(out,&x,sizeof x);return true;
}
bool observe_own_site(const arm_cpu_t*c,const uint8_t*r,uint32_t b,uint32_t n,const qualified_site*s,const qualified_space*p,entry_observation*out){
 if(!p||!out||!s||(s->mode&63u)!=ARM_MODE_USR||!site_ok(c,r,b,n,s))return false;
 if(c->cp15.sctlr!=p->sctlr||c->cp15.ttbr0!=p->ttbr0||c->cp15.ttbr1!=p->ttbr1||c->cp15.ttbcr!=p->ttbcr||c->cp15.dacr!=p->dacr||c->cp15.context_id!=p->context_id)return false;
 entry_observation x=row(c);memcpy(out,&x,sizeof x);return true;
}
