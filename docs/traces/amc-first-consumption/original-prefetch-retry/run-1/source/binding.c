#include "binding.h"
#include <stdlib.h>
#include <string.h>
typedef struct {const uint8_t*ram;uint32_t base,size;binding_walk*walk;bool failed;} binding_bus;
static uint32_t table_read(void*context,uint32_t a){
 binding_bus*b=context;binding_walk*w=b->walk;uint32_t v=0;
 if(w->words>=8){b->failed=true;return 0;}
 binding_word*r=&w->table[w->words++];r->address=a;
 if((a&3)||a<b->base||(uint64_t)a+4>(uint64_t)b->base+b->size){b->failed=true;return 0;}
 memcpy(&v,b->ram+a-b->base,4);r->value=v;r->available=1;return v;
}
bool binding_read_with_allocator(binding_walk*w,const arm_cpu_t*c,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t va,arm_access_t access,bool fresh,void*(*allocate)(size_t),void(*release)(void*)){
 if(!w)return false;memset(w,0,sizeof *w);w->access=access;w->fresh=fresh;
 if(!c||!ram||!size||!allocate||!release||(access!=ARM_ACCESS_READ&&access!=ARM_ACCESS_FETCH)||(uint64_t)va+32>UINT32_MAX+1ull){w->reason=BIND_ARGUMENT;return false;}
 arm_cpu_t*copy=allocate(sizeof *copy);if(!copy){w->reason=BIND_ALLOC;return false;}*copy=*c;
 binding_bus context={ram,base,size,w,false};arm_bus_t bus={0};bus.ctx=&context;bus.read32=table_read;copy->bus=&bus;
 if(fresh)arm_mmu_tlb_flush(copy);uint8_t temporary[32];unsigned offset=0;
 while(offset<32){
  if(w->spans>=2){w->reason=BIND_ARGUMENT;break;}
  binding_span*s=&w->span[w->spans++];s->va=va+offset;s->bytes=1024-(s->va&1023);if(s->bytes>32-offset)s->bytes=32-offset;
  s->fault=arm_mmu_translate(copy,s->va,access,(copy->cpsr&31)!=ARM_MODE_USR,&s->pa);
  if(context.failed){w->reason=BIND_TABLE;break;}
  if(s->fault){s->pa=0;w->reason=BIND_FAULT;break;}
  if(s->pa<base||(uint64_t)s->pa+s->bytes>(uint64_t)base+size){w->reason=BIND_PHYSICAL;break;}
  s->available=1;memcpy(temporary+offset,ram+s->pa-base,s->bytes);offset+=s->bytes;
 }
 if(offset==32){memcpy(w->code,temporary,32);w->available=1;w->reason=BIND_OK;}
 release(copy);return w->available!=0;
}
bool binding_read(binding_walk*w,const arm_cpu_t*c,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t va,arm_access_t access,bool fresh){return binding_read_with_allocator(w,c,ram,base,size,va,access,fresh,malloc,free);}
