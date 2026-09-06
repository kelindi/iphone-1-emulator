#include "prefetch.h"
#include <string.h>
bool fetch_cache_hit(const arm_cpu_t*c){return c&&c->fetch_host&&c->fetch_blk==(c->r[15]&~0x3ffu)&&c->fetch_gen==c->tlb_gen&&c->fetch_priv==((c->cpsr&31)!=ARM_MODE_USR);}
static bool context(const binding_diagnostic*a,const binding_diagnostic*b){return a->cpsr==b->cpsr&&!memcmp(a->regs,b->regs,64)&&a->sctlr==b->sctlr&&a->ttbr0==b->ttbr0&&a->ttbr1==b->ttbr1&&a->ttbcr==b->ttbcr&&a->dacr==b->dacr&&a->context_id==b->context_id&&!memcmp(a->expected,b->expected,32);}
unsigned fetch_prepare(fetch_observer*o,const arm_cpu_t*c,const binding_diagnostic*d){
 if(!o||!c||!d||o->pending||o->mapped||o->count>=8||!d->attempted||d->pc!=0x31236650||(d->cpsr&63)!=16||!d->target||!d->expected_available||d->pc!=c->r[15]||d->cpsr!=c->cpsr||d->cycles!=c->cycles||memcmp(d->regs,c->r,64))return FETCH_REFUSE;
 if(d->sctlr!=c->cp15.sctlr||d->ttbr0!=c->cp15.ttbr0||d->ttbr1!=c->cp15.ttbr1||d->ttbcr!=c->cp15.ttbcr||d->dacr!=c->cp15.dacr||d->context_id!=c->cp15.context_id)return FETCH_REFUSE;
 for(unsigned i=0;i<4;i++)if(d->walk[i].access!=(i&1?ARM_ACCESS_FETCH:ARM_ACCESS_READ)||d->walk[i].fresh!=(i>=2))return FETCH_REFUSE;
 if(o->count&&(!context(&o->initial,d)||c->cycles-o->initial.cycles>1000000))return FETCH_REFUSE;
 if(d->reason==BIND_OK){
  if(!o->faults)return FETCH_REFUSE;
  for(unsigned i=0;i<4;i++)if(!d->walk[i].available||d->walk[i].reason||memcmp(d->walk[i].code,d->expected,32))return FETCH_REFUSE;
  o->mapped=true;return FETCH_MAPPED;
 }
 if(d->reason!=BIND_FAULT||fetch_cache_hit(c))return FETCH_REFUSE;
 for(unsigned i=0;i<4;i++)if(d->walk[i].reason!=BIND_FAULT||d->walk[i].available||d->walk[i].spans!=1||d->walk[i].span[0].fault!=7||d->walk[i].span[0].va!=d->pc||d->walk[i].span[0].bytes!=32||d->walk[i].span[0].available||d->walk[i].span[0].pa||d->walk[i].words!=2||d->walk[i].table[1].value||!d->walk[i].table[1].available)return FETCH_REFUSE;
 if(!o->count)o->initial=*d;
 fetch_receipt*r=&o->records[o->count];*r=(fetch_receipt){.start=c->cycles,.pc=c->r[15],.cpsr=c->cpsr,.ifsr=c->cp15.ifsr,.ifar=c->cp15.ifar,.irq=c->irq_line,.fiq=c->fiq_line,.sctlr=c->cp15.sctlr,.fetch_present=c->fetch_host!=NULL,.fetch_block=c->fetch_blk,.fetch_gen=c->fetch_gen,.tlb_gen=c->tlb_gen,.fetch_priv=c->fetch_priv};o->pending=true;return FETCH_ALLOW;
}
bool fetch_finish(fetch_observer*o,const arm_cpu_t*c,const guest_step*s,bool in_guest){
 if(!o||!c||!s||!o->pending||o->count>=8||in_guest||s->active||!s->returned)return false;fetch_receipt*r=&o->records[o->count];
 if(s->cycles!=r->start||s->pc!=r->pc||s->cpsr!=r->cpsr||c->cycles!=r->start+1)return false;
 unsigned kind=1,mode=ARM_MODE_ABT,bank=ARM_BANK_ABT,vector=ARM_VEC_PREFETCH;
 if(r->fiq&&!(r->cpsr&ARM_CPSR_F)){kind=3;mode=ARM_MODE_FIQ;bank=ARM_BANK_FIQ;vector=ARM_VEC_FIQ;}
 else if(r->irq&&!(r->cpsr&ARM_CPSR_I)){kind=2;mode=ARM_MODE_IRQ;bank=ARM_BANK_IRQ;vector=ARM_VEC_IRQ;}
 uint32_t cpsr=(r->cpsr&~(63u|ARM_CPSR_E))|mode|ARM_CPSR_I|ARM_CPSR_A;if(kind==3)cpsr|=ARM_CPSR_F;if(r->sctlr&ARM_SCTLR_EE)cpsr|=ARM_CPSR_E;
 if(c->r[15]!=((r->sctlr&ARM_SCTLR_V)?0xffff0000u:0)+vector||c->cpsr!=cpsr||c->r[14]!=r->pc+4||c->spsr[bank]!=r->cpsr)return false;
 if(kind==1){if(c->cp15.ifsr!=7||c->cp15.ifar!=r->pc)return false;}else if(c->cp15.ifsr!=r->ifsr||c->cp15.ifar!=r->ifar)return false;
 r->end=c->cycles;r->after_pc=c->r[15];r->after_cpsr=c->cpsr;r->after_ifsr=c->cp15.ifsr;r->after_ifar=c->cp15.ifar;r->after_lr=c->r[14];r->after_spsr=c->spsr[bank];r->kind=kind;r->returned=1;o->pending=false;o->count++;if(kind==1)o->faults++;return true;
}
