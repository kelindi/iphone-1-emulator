#include "transport.h"
#include <stdlib.h>
#include <string.h>
static unsigned checks,callbacks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"transport:%u %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t forbidden(void*c,uint32_t a){(void)c;(void)a;callbacks++;return 0;}
static void put(uint8_t*r,uint32_t a,uint32_t v){memcpy(r+a,&v,4);}
int main(void){
 s5l8900_t*m=calloc(1,sizeof*m),*before=malloc(sizeof*m);uint8_t*ram=calloc(1,0x400000),*saved=malloc(0x400000);transport_values*v=malloc(sizeof*v);CHECK(m&&before&&ram&&saved&&v);
 m->ram=ram;m->ram_base=0x10000000;m->ram_size=0x400000;m->bus.read32=forbidden;m->cpu.bus=&m->bus;m->cpu.cpsr=0x60000010;m->cpu.cp15.sctlr=1;m->cpu.cp15.ttbr0=0x10000000;m->cpu.cp15.dacr=1;
 put(ram,0x392*4,0x10100c02);put(ram,0x312*4,0x10200c02);
 FILE*f=fopen(".context/amc-first/stage-r/run-1/destination-caller.bin","rb");CHECK(f&&fread(ram+0x22b2b4,1,32,f)==32);fclose(f);
 for(unsigned i=0;i<64;i++)ram[0x154d38+i]=(uint8_t)(i+7);
 destination_observer d={.captured=true,.context=&m->cpu,.caller_available=true,.caller_address=0x3122b2b4,.cpsr=0x60000010};d.regs[14]=0x3122b2b8;d.regs[5]=0x39254d38;memcpy(d.caller,ram+0x22b2b4,32);
 uint8_t out[64],zero[64]={0};*before=*m;memcpy(saved,ram,0x400000);CHECK(transport_manager(&d,ram,m->ram_base,m->ram_size,out));CHECK(!memcmp(out,ram+0x154d38,64));CHECK(!memcmp(m,before,sizeof*m)&&!memcmp(ram,saved,0x400000)&&!callbacks);
 #define FAIL() do{memset(out,0,64);CHECK(!transport_manager(&d,ram,m->ram_base,m->ram_size,out));CHECK(!memcmp(out,zero,64));}while(0)
 d.regs[5]^=4;FAIL();d.regs[5]^=4;d.regs[14]^=4;FAIL();d.regs[14]^=4;d.caller[3]^=1;FAIL();d.caller[3]^=1;d.cpsr^=32;FAIL();d.cpsr^=32;m->cpu.cpsr=0x60000013;FAIL();m->cpu.cpsr=0x60000010;
 put(ram,0x392*4,0x10100402);FAIL();put(ram,0x392*4,0x10100c02);m->cpu.cp15.ttbr0+=0x4000;FAIL();m->cpu.cp15.ttbr0-=0x4000;ram[0x22b2b4]^=1;FAIL();ram[0x22b2b4]^=1;
 CHECK(transport_collect(m,v));CHECK(v->count==467);CHECK(!memcmp(m,before,sizeof*m)&&!memcmp(ram,saved,0x400000)&&!callbacks);
 m->dmac[0].config=1;m->dmac[0].ch[0].cfg=1;m->dmac[0].ch[0].lli=0x10000043;
 CHECK(transport_lli(m,0,0,out));CHECK(!memcmp(out,ram+0x40,16));m->dmac[0].ch[0].lli=0x103ffff5;CHECK(!transport_lli(m,0,0,out));m->dmac[0].ch[0].lli=0xfffffff3;CHECK(!transport_lli(m,0,0,out));m->dmac[0].ch[0].lli=0;CHECK(!transport_lli(m,0,0,out));
 m->dmac[0].ch[0].lli=0x10000040;m->dmac[0].ch[0].cfg|=PL080_CFG_HALT;CHECK(!transport_lli(m,0,0,out));m->dmac[0].ch[0].cfg=0;CHECK(!transport_lli(m,0,0,out));m->dmac[0].ch[0].cfg=1;m->dmac[0].config=0;CHECK(!transport_lli(m,0,0,out));CHECK(!transport_lli(m,2,0,out));CHECK(!transport_lli(m,0,8,out));
 *before=*m;CHECK(transport_export(m,&d,".context/amc-first/stage-u/service-artifacts","synthetic",true));CHECK(!memcmp(m,before,sizeof*m)&&!memcmp(ram,saved,0x400000)&&!callbacks);
 uint32_t clock[1024],oldclock[1024];for(unsigned i=0;i<1024;i++)clock[i]=i*17;memcpy(oldclock,clock,sizeof clock);
 m->stub_count=1;m->stubs[0].base=S5L8900_CLOCK_BASE;m->stubs[0].nregs=1024;m->stubs[0].size=4096;m->stubs[0].regs=clock;*before=*m;
 CHECK(transport_collect(m,v));CHECK(v->count==1497);CHECK(!memcmp(clock,oldclock,sizeof clock)&&!memcmp(m,before,sizeof*m)&&!callbacks);
 m->stub_count=S5L_STUB_MAX+1;CHECK(!transport_collect(m,v));m->stub_count=1;m->stubs[0].base=S5L8900_CLOCK_BASE;m->stubs[0].nregs=1025;CHECK(!transport_collect(m,v));
 printf("transport observation %u PASS; synthetic only\n",checks);free(v);free(saved);free(ram);free(before);free(m);
}
