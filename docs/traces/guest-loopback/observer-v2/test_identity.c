#include "identity.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,live_reads;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"identity:%d %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t forbidden(void*ctx,uint32_t address){(void)ctx;(void)address;live_reads++;return 0;}
static uint32_t ram_read(void*ctx,uint32_t address){uint32_t v=0;if(address>=0x10000000&&address<=0x1001fffc)memcpy(&v,(uint8_t*)ctx+address-0x10000000,4);return v;}
static void word(uint8_t*ram,unsigned offset,uint32_t value){memcpy(ram+offset,&value,4);}
int main(void){
 uint8_t*ram=calloc(1,0x20000),*before_ram=malloc(0x20000),expected[4848];arm_cpu_t*cpu=calloc(1,sizeof *cpu),*before=malloc(sizeof *cpu);CHECK(ram&&before_ram&&cpu&&before);
 arm_bus_t bus={0};bus.read32=forbidden;cpu->bus=&bus;cpu->cpsr=ARM_MODE_USR;cpu->r[15]=0x1a60;cpu->cp15.sctlr=ARM_SCTLR_M|ARM_SCTLR_XP;cpu->cp15.ttbr0=0x10004000;cpu->cp15.dacr=1;
 word(ram,0x4000,0x10005001);word(ram,0x5004,0x10010000|(3u<<4)|2u);
 for(unsigned i=0;i<sizeof expected;i++)expected[i]=(uint8_t)(i*17+3);
 memcpy(ram+0x10a60,expected,4096-0xa60);memcpy(ram+0x11000,expected+4096-0xa60,sizeof expected-(4096-0xa60));
 memcpy(before_ram,ram,0x20000);memcpy(before,cpu,sizeof *cpu);
 CHECK(own_entry_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,64));
 CHECK(!own_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,sizeof expected));
 CHECK(!memcmp(before,cpu,sizeof *cpu)&&!memcmp(before_ram,ram,0x20000)&&!live_reads);
 /* Genuine demand-map equivalent: only the synthetic fixture changes PTE. */
 word(ram,0x5008,0x10011000|(3u<<4)|2u);memcpy(before_ram,ram,0x20000);
 CHECK(own_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,sizeof expected));
 CHECK(!memcmp(before,cpu,sizeof *cpu)&&!memcmp(before_ram,ram,0x20000)&&!live_reads);
 expected[4000]^=1;CHECK(!own_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,sizeof expected));expected[4000]^=1;
 cpu->cpsr|=ARM_CPSR_T;CHECK(!own_entry_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,64));cpu->cpsr=ARM_MODE_SVC;CHECK(!own_entry_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,64));cpu->cpsr=ARM_MODE_USR;
 CHECK(!own_entry_identity(cpu,ram,0x10000000,0x20000,0x1a64,expected,64));CHECK(!own_entry_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,4096));
 /* A prefilled successful live TLB cannot override current RAM page tables. */
 arm_bus_t membus={0};membus.ctx=ram;membus.read32=ram_read;cpu->bus=&membus;uint32_t pa=0;CHECK(!arm_mmu_translate(cpu,0x2000,ARM_ACCESS_READ,false,&pa)&&pa==0x10011000);cpu->bus=&bus;
 word(ram,0x5008,0x10011000|(1u<<4)|2u);memcpy(before,cpu,sizeof *cpu);memcpy(before_ram,ram,0x20000);CHECK(!own_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,sizeof expected));
 CHECK(!own_identity(cpu,ram,0x10000000,0x20000,0x2000,expected+(0x2000-0x1a60),64));
 CHECK(!memcmp(before,cpu,sizeof *cpu)&&!memcmp(before_ram,ram,0x20000)&&!live_reads);
 cpu->cp15.ttbr0=0x38500000;memcpy(before,cpu,sizeof *cpu);CHECK(!own_entry_identity(cpu,ram,0x10000000,0x20000,0x1a60,expected,64));CHECK(!memcmp(before,cpu,sizeof *cpu)&&!live_reads);
 CHECK(!own_identity(cpu,ram,0x10000000,0x20000,0xfffffff0,expected,32));
 free(ram);free(before_ram);free(cpu);free(before);printf("PASS %u demand-page/identity/permission/CPU+RAM+bus immutability checks; synthetic only, no guest retry\n",checks);return 0;
}
