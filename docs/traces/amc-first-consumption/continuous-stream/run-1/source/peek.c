#include "peek.h"
#include <stdlib.h>
#include <string.h>
typedef struct {const uint8_t*ram;uint32_t base,size;bool failed;} peek_bus;
static uint32_t table_word(void*ctx,uint32_t pa){
 peek_bus*s=ctx;uint32_t value=0;
 if((pa&3)||pa<s->base||(uint64_t)pa+4>(uint64_t)s->base+s->size){s->failed=true;return 0;}
 memcpy(&value,s->ram+pa-s->base,4);return value;
}
static bool translate(arm_cpu_t*copy,peek_bus*bus,uint32_t va,uint32_t*pa){
 /* All counters, TLB fills and faults touch only this copied CPU. Page-table
  * reads use ordinary RAM bytes; no live bus callback or MMIO is reachable. */
 uint32_t fault=arm_mmu_translate(copy,va,ARM_ACCESS_READ,(copy->cpsr&31u)!=ARM_MODE_USR,pa);
 return !fault&&!bus->failed;
}
bool amc_peek_translate(const arm_cpu_t*cpu,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t va,uint32_t*pa){
 if(!cpu||!ram||!pa)return false;arm_cpu_t*copy=malloc(sizeof *copy);if(!copy)return false;*copy=*cpu;
 peek_bus ctx={ram,base,size,false};arm_bus_t bus={0};bus.ctx=&ctx;bus.read32=table_word;copy->bus=&bus;
 bool ok=translate(copy,&ctx,va,pa);free(copy);return ok;
}
bool amc_peek_ram(const arm_cpu_t*cpu,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t va,void*out,size_t n){
 if(!cpu||!ram||!out||!n||n>4096||(uint64_t)va+n>UINT32_MAX+1ull)return false;
 arm_cpu_t*copy=malloc(sizeof *copy);if(!copy)return false;*copy=*cpu;
 peek_bus ctx={ram,base,size,false};arm_bus_t bus={0};bus.ctx=&ctx;bus.read32=table_word;copy->bus=&bus;
 uint8_t scratch[4096];size_t offset=0;bool ok=true;
 while(offset<n){uint32_t here=va+(uint32_t)offset,pa;
  /* Legacy small-page permissions vary per1KiB, matching the core TLB. */
  size_t chunk=1024u-(here&1023u);if(chunk>n-offset)chunk=n-offset;
  if(!translate(copy,&ctx,here,&pa)||pa<base||(uint64_t)pa+chunk>(uint64_t)base+size){ok=false;break;}
  memcpy(scratch+offset,ram+pa-base,chunk);offset+=chunk;
 }
 if(ok)memcpy(out,scratch,n);free(copy);return ok;
}
