#include "peek.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,reads;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"chunked:%u %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t forbidden(void*c,uint32_t a){(void)c;(void)a;reads++;return 0;}
int main(void){
 arm_cpu_t*cpu=calloc(1,sizeof *cpu),*before=malloc(sizeof *cpu);uint8_t*ram=calloc(1,0x200000),*ram_before=malloc(0x200000),*got=malloc(0xc001),*old=malloc(0xc001);CHECK(cpu&&before&&ram&&ram_before&&got&&old);
 arm_bus_t bus={0};bus.read32=forbidden;cpu->bus=&bus;cpu->cpsr=ARM_MODE_SVC;cpu->cp15.sctlr=1;cpu->cp15.ttbr0=0x10000000;cpu->cp15.dacr=1;
 uint32_t first=0x10000402,second=0x10100402;memcpy(ram+0x400*4,&first,4);memcpy(ram+0x401*4,&second,4);
 for(unsigned i=0;i<0xc000;i++)ram[0xfc000+i]=(uint8_t)(i*13+7);
 *before=*cpu;memcpy(ram_before,ram,0x200000);
 unsigned lengths[]={1,4095,4096,4097,16384,32768,49152};
 for(unsigned i=0;i<sizeof lengths/sizeof *lengths;i++){
  size_t n=lengths[i];memset(got,0xa5,0xc001);CHECK(amc_peek_ram_chunked(cpu,ram,0x10000000,0x200000,0x400fc000,got,n));CHECK(!memcmp(got,ram+0xfc000,n)&&got[n]==0xa5);CHECK(!memcmp(cpu,before,sizeof *cpu)&&!memcmp(ram,ram_before,0x200000)&&!reads);
 }
 memset(got,0xa5,0xc001);memcpy(old,got,0xc001);CHECK(!amc_peek_ram(cpu,ram,0x10000000,0x200000,0x400fc000,got,16384));CHECK(!memcmp(got,old,0xc001));
 /* A late second-section fault cannot expose already copied chunks. */
 memset(ram+0x401*4,0,4);memcpy(ram_before,ram,0x200000);CHECK(!amc_peek_ram_chunked(cpu,ram,0x10000000,0x200000,0x400fc000,got,49152));CHECK(!memcmp(got,old,0xc001)&&!memcmp(cpu,before,sizeof *cpu)&&!memcmp(ram,ram_before,0x200000));
 memcpy(ram+0x401*4,&second,4);cpu->cpsr=ARM_MODE_USR;*before=*cpu;memcpy(ram_before,ram,0x200000);CHECK(!amc_peek_ram_chunked(cpu,ram,0x10000000,0x200000,0x400fc000,got,16384));CHECK(!memcmp(got,old,0xc001)&&!memcmp(cpu,before,sizeof *cpu)&&!memcmp(ram,ram_before,0x200000)&&!reads);
 /* SVC first page allowed, final1KiB subpage permission denied. */
 cpu->cpsr=ARM_MODE_SVC;uint32_t coarse=0x10004001,small=0x10100002|(1u<<4);memcpy(ram+0x401*4,&coarse,4);memcpy(ram+0x4000,&small,4);*before=*cpu;memcpy(ram_before,ram,0x200000);CHECK(!amc_peek_ram_chunked(cpu,ram,0x10000000,0x200000,0x400ff000,got,8192));CHECK(!memcmp(got,old,0xc001)&&!memcmp(cpu,before,sizeof *cpu)&&!memcmp(ram,ram_before,0x200000));
 CHECK(!amc_peek_ram_chunked(cpu,ram,0x10000000,0x200000,UINT32_MAX-4095,got,8192));CHECK(!amc_peek_ram_chunked(cpu,ram,0x10000000,0x200000,0x400fc000,got,0));CHECK(!amc_peek_ram_chunked(cpu,ram,0x10000000,0x200000,0x400fc000,got,49153));CHECK(!memcmp(got,old,0xc001)&&!reads);
 free(old);free(got);free(ram_before);free(ram);free(before);free(cpu);printf("PASS %u atomic chunk/boundary/late-permission/CPU-RAM-immutability checks\n",checks);return 0;
}
