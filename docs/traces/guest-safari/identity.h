#include "peek.h"
#include <string.h>
/* No actual guest access changes page residency or CPU/device state. */
static bool own_identity(const arm_cpu_t *cpu,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t va,const uint8_t*expected,size_t n){
 if(!cpu||!expected||!n||n>65536||(uint64_t)va+n>UINT32_MAX+1ull)return false;
 uint8_t got[4096];size_t done=0;
 while(done<n){size_t take=n-done;if(take>sizeof got)take=sizeof got;
  if(!amc_peek_ram(cpu,ram,base,size,va+(uint32_t)done,got,take)||memcmp(got,expected+done,take))return false;done+=take;}
 return true;
}
static bool own_entry_identity(const arm_cpu_t*cpu,const uint8_t*ram,uint32_t base,uint32_t size,uint32_t entry,const uint8_t*expected,size_t n){
 return cpu && (cpu->cpsr&0x3fu)==ARM_MODE_USR && cpu->r[15]==entry && n<=4096u-(entry&4095u) && own_identity(cpu,ram,base,size,entry,expected,n);
}
