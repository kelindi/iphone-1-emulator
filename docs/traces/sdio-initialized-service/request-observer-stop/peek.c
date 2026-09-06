#include "peek.h"
#include <stdlib.h>
#include <string.h>
/* All translation mutations are confined to a CPU copy. The temporary bus
 * reads page tables only through the existing physical-RAM pointer contract;
 * it cannot call MMIO callbacks, write RAM, or update the original TLB. */
typedef struct {checkpoint_phone_t*p;bool valid;} peek_ctx_t;
static uint32_t read_table(void*ctx,uint32_t pa){peek_ctx_t*x=ctx;uint8_t*b=NULL;
 if(x->p->underlying.host_ram)b=x->p->underlying.host_ram(x->p->underlying.ctx,pa,4);
 if(!b){x->valid=false;return 0;}return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
bool request_peek(checkpoint_phone_t*p,uint32_t va,void*out,size_t n){
 if(!p||!p->machine||!out||!n||n>4096||n-1>UINT32_MAX-va||!p->underlying.host_ram)return false;
 arm_cpu_t*c=malloc(sizeof*c);uint8_t*temporary=malloc(n);if(!c||!temporary){free(c);free(temporary);return false;}
 memcpy(c,&p->machine->cpu,sizeof*c);peek_ctx_t context={p,true};arm_bus_t bus={0};bus.ctx=&context;bus.read32=read_table;c->bus=&bus;
 bool ok=true;size_t offset=0;
 while(offset<n){uint32_t current=va+(uint32_t)offset,pa=0;size_t chunk=1024u-(current&1023u);if(chunk>n-offset)chunk=n-offset;
  if(arm_mmu_translate(c,current,ARM_ACCESS_READ,true,&pa)||!context.valid||chunk-1>UINT32_MAX-pa){ok=false;break;}
  uint8_t*source=p->underlying.host_ram(p->underlying.ctx,pa,(uint32_t)chunk);if(!source){ok=false;break;}
  memcpy(temporary+offset,source,chunk);offset+=chunk;
 }
 if(ok)memcpy(out,temporary,n);free(temporary);free(c);return ok;
}
