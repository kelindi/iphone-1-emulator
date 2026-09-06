#include "peek.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,reads;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"peek:%u %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t forbidden(void*c,uint32_t a){(void)c;(void)a;reads++;return 0;}
int main(void){
 arm_cpu_t*cpu=calloc(1,sizeof *cpu),*before=malloc(sizeof *cpu);uint8_t*ram=calloc(1,0x200000);CHECK(cpu&&before&&ram);
 arm_bus_t original={0};original.read32=forbidden;cpu->bus=&original;cpu->cpsr=ARM_MODE_SVC;
 cpu->cp15.sctlr=1;cpu->cp15.ttbr0=0x10000000;cpu->cp15.dacr=3;
 /* VA0x40000000 and40100000 map separate RAM sections. */
 uint32_t section0=0x10000000|0xc02,section1=0x10100000|0xc02;
 memcpy(ram+0x400*4,&section0,4);memcpy(ram+0x401*4,&section1,4);
 for(unsigned i=0;i<32;i++)ram[0xffff0+i]=(uint8_t)(i+1);
 *before=*cpu;uint8_t got[32]={0};CHECK(amc_peek_ram(cpu,ram,0x10000000,0x200000,0x400ffff0,got,sizeof got));CHECK(!memcmp(got,ram+0xffff0,sizeof got));CHECK(!memcmp(cpu,before,sizeof *cpu));CHECK(!reads);
 uint32_t pa=0;CHECK(amc_peek_translate(cpu,ram,0x10000000,0x200000,0x400ffff0,&pa)&&pa==0x100ffff0);CHECK(!memcmp(cpu,before,sizeof *cpu));
 memset(got,0xa5,sizeof got);uint8_t unchanged[32];memcpy(unchanged,got,32);
 CHECK(!amc_peek_ram(cpu,ram,0x10000000,0x200000,0x50000000,got,32));CHECK(!memcmp(got,unchanged,32));CHECK(!memcmp(cpu,before,sizeof *cpu));
 /* Valid first page followed by unmapped page cannot leak partial output. */
 memset(ram+0x401*4,0,4);CHECK(!amc_peek_ram(cpu,ram,0x10000000,0x200000,0x400ffff0,got,32));CHECK(!memcmp(got,unchanged,32));CHECK(!memcmp(cpu,before,sizeof *cpu));
 /* A legacy small page may change permission at a1KiB subpage boundary. */
 uint32_t coarse=0x10004001,small=0x10008000|2|(3u<<4);
 memcpy(ram+0x400*4,&coarse,4);memcpy(ram+0x4000,&small,4);cpu->cp15.dacr=1;*before=*cpu;
 CHECK(!amc_peek_ram(cpu,ram,0x10000000,0x200000,0x400003f0,got,32));CHECK(!memcmp(got,unchanged,32));CHECK(!memcmp(cpu,before,sizeof *cpu));
 cpu->cp15.ttbr0=0x38500000;*before=*cpu;CHECK(!amc_peek_ram(cpu,ram,0x10000000,0x200000,0x40000000,got,32));CHECK(!reads);CHECK(!memcmp(cpu,before,sizeof *cpu));
 CHECK(!amc_peek_ram(cpu,ram,0x10000000,0x200000,UINT32_MAX-7,got,32));
 free(cpu);free(before);free(ram);printf("side-effect-free destination peek: %u checks passed\n",checks);return 0;
}
