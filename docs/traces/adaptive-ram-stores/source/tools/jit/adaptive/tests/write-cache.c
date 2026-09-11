#include "soc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static s5l8900_t m;
static uint8_t expected_ram[1u<<20];
static unsigned checks,notifications,last_len;
static const uint8_t *last_host;
static uint32_t last_value;
#define CHECK(x) do { checks++; assert(x); } while(0)
static void written(void *ctx,const uint8_t *host,uint32_t len){
 CHECK(ctx==&m);CHECK(len==1||len==2||len==4);
 notifications++;last_host=host;last_len=len;last_value=0;memcpy(&last_value,host,len);
}
static void refused(uint32_t va,unsigned width,bool priv){
 arm_cpu_t before=m.cpu;memcpy(expected_ram,m.ram,sizeof expected_ram);unsigned count=notifications;
 CHECK(!arm_write_cache_try(&m.cpu,va,width,priv,0x76543210));
 CHECK(!memcmp(&before,&m.cpu,sizeof before));CHECK(!memcmp(expected_ram,m.ram,sizeof expected_ram));CHECK(count==notifications);
}
static void hit(uint32_t va,unsigned width,bool priv){
 uint32_t value=0x87654321,mask=width==4?UINT32_MAX:width==2?0xffff:0xff;
 arm_cpu_t before=m.cpu;before.dwrite_hits++;memcpy(expected_ram,m.ram,sizeof expected_ram);memcpy(expected_ram+va,&value,width);unsigned count=notifications;
 CHECK(arm_write_cache_try(&m.cpu,va,width,priv,value)==m.ram+va);
 CHECK(!memcmp(&before,&m.cpu,sizeof before));CHECK(!memcmp(expected_ram,m.ram,sizeof expected_ram));CHECK(notifications==count+1);
 CHECK(last_host==m.ram+va&&last_len==width&&last_value==(value&mask));
}
static void warm(uint32_t va,bool user){
 uint32_t raw=user?0xe4a10000:0xe5810000;
 s5l8900_load(&m,0,&raw,4);m.cpu.r[0]=0x12345678;m.cpu.r[1]=va;m.cpu.r[15]=0;CHECK(arm_step(&m.cpu)==ARM_OK);
}
int main(void){
 CHECK(s5l8900_init(&m,0,sizeof expected_ram));CHECK(s5l8900_set_direct_ram_writes(&m,true));m.cpu.cpsr=ARM_MODE_SYS|ARM_CPSR_I|ARM_CPSR_F;
 m.bus.ram_written=written;refused(0x9000,4,true);warm(0x9000,false);
 hit(0x9000,4,true);hit(0x9001,1,true);hit(0x9002,2,true);
 refused(0x9000,4,false);refused(0x9400,4,true);refused(0x93fe,4,true);refused(0x9000,0,true);refused(0x9000,3,true);
 m.cpu.cp15.sctlr&=~(ARM_SCTLR_A|ARM_SCTLR_U);refused(0x9001,4,true);refused(0x9001,2,true);
 m.cpu.cp15.sctlr|=ARM_SCTLR_U;hit(0x9001,4,true);hit(0x9001,2,true);refused(0x93ff,2,true);
 m.cpu.cp15.sctlr|=ARM_SCTLR_A;refused(0x9001,4,true);m.cpu.cp15.sctlr&=~ARM_SCTLR_A;
 warm(0x9000,true);hit(0x9000,4,false);
 CHECK(s5l8900_set_direct_ram_writes(&m,false));refused(0x9000,4,true);
 CHECK(s5l8900_set_direct_ram_writes(&m,true));refused(0x9000,4,true);warm(0x9000,false);hit(0x9000,4,true);
 arm_mmu_tlb_flush(&m.cpu);refused(0x9000,4,true);
 uint32_t descriptor=(3u<<10)|2u;s5l8900_load(&m,0x4000,&descriptor,4);
 m.cpu.cp15.ttbr0=0x4000;m.cpu.cp15.dacr=1;m.cpu.cp15.sctlr|=ARM_SCTLR_M|ARM_SCTLR_XP;arm_mmu_tlb_flush(&m.cpu);
 warm(0x9000,false);hit(0x9000,4,true);refused(0x9000,4,false);refused(0x200000,4,true);refused(S5L8900_TIMER_BASE+TIMER4_COUNTBUF,4,true);
 const arm_bus_t *bus=m.cpu.bus;m.cpu.bus=NULL;refused(0x9000,4,true);m.cpu.bus=bus;
 s5l8900_free(&m);printf("write-cache: %u checks PASS; precise committed writes/notifications and no-effect refusals\n",checks);
}
