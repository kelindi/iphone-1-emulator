#include "amc_idle.h"
#include "amc_sram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x); exit(1); } } while(0)
static uint32_t status(amc_idle_t *s) { uint32_t v=0xdeadbeef; CHECK(amc_idle_read(s,0xb18,4,&v)==AMC_ACCEPT); CHECK(!(v&0x200)); return v; }
static void reject_write(amc_idle_t *s,uint32_t off,unsigned width,uint32_t v) {
    amc_idle_t saved=*s; CHECK(amc_idle_write(s,off,width,v)==AMC_REJECT); CHECK(!memcmp(&saved,s,sizeof saved));
}
int main(void) {
    amc_idle_t s,saved; amc_sram_t ram,copy;
    amc_sram_init(&ram); CHECK(amc_sram_write(&ram,0x28000,4,0xaabbccdd));copy=ram;
    amc_idle_init(&s); CHECK(status(&s)==0);
    CHECK(amc_idle_write(&s,0x2000,4,3)==AMC_ACCEPT); CHECK(status(&s)==0);
    for(unsigned clock=0;clock<4;clock++) for(unsigned power=0;power<2;power++) {
        amc_idle_init(&s);amc_idle_dependencies(&s,clock<<3,power<<4);
        saved=s;CHECK(amc_idle_reset(&s,0xa00)==AMC_REJECT); CHECK(!memcmp(&s,&saved,sizeof s));
        if(clock||power){CHECK(amc_idle_reset(&s,0xa000)==AMC_REJECT);CHECK(!memcmp(&s,&saved,sizeof s));}
        CHECK(status(&s)==0);
    }
    amc_idle_init(&s);amc_idle_dependencies(&s,0,0);
    for(unsigned cmd=0;cmd<65536;cmd+=17){if(cmd==0xa000||cmd==0xa00)continue;saved=s;CHECK(amc_idle_reset(&s,cmd)==AMC_REJECT);CHECK(!memcmp(&s,&saved,sizeof s));}
    const uint32_t partial[]={0x8000,0x2000,0x800,0x200,0xaa00};
    for(unsigned i=0;i<sizeof partial/sizeof partial[0];i++){saved=s;CHECK(amc_idle_reset(&s,partial[i])==AMC_REJECT);CHECK(!memcmp(&s,&saved,sizeof s));}
    CHECK(amc_idle_write(&s,0x2000,4,3)==AMC_ACCEPT);
    CHECK(amc_idle_reset(&s,0xa000)==AMC_ACCEPT);CHECK(status(&s)==0);
    CHECK(amc_idle_reset(&s,0xa00)==AMC_ACCEPT);CHECK(status(&s)==0x100);
    for(unsigned i=0;i<32;i++)CHECK(status(&s)==0x100);
    for(unsigned off=0;off<0x3000;off+=4){
        if(off==0x2000||off==0xa00)continue;
        reject_write(&s,off,4,0);
        uint32_t v=0xaabbccdd;
        if(off==0x10||off==0xb18||off==0x184)continue;
        CHECK(amc_idle_read(&s,off,4,&v)==AMC_REJECT);CHECK(v==0xaabbccdd);
    }
    for(unsigned w=0;w<9;w++){if(w==4)continue;reject_write(&s,0xa00,w,0);uint32_t v=7;CHECK(amc_idle_read(&s,0xb18,w,&v)==AMC_REJECT);CHECK(v==7);}
    static const uint32_t offs[]={0xa00,0x184,0x180,0xb10,0xb04,0x340};
    static const uint32_t vals[]={0,0,1,UINT32_MAX,UINT32_MAX,0};
    for(unsigned i=0;i<6;i++){reject_write(&s,offs[i],4,vals[i]^2);CHECK(amc_idle_write(&s,offs[i],4,vals[i])==AMC_ACCEPT);CHECK(status(&s)==0x100);}
    saved=s;CHECK(amc_idle_write(&s,0x340,4,1)==AMC_UPLOAD_END);CHECK(!memcmp(&saved,&s,sizeof s));
    reject_write(&s,0,4,1);reject_write(&s,4,4,1);reject_write(&s,0xc,4,1);reject_write(&s,0x100c,4,1);
    CHECK(amc_idle_reset(&s,0xa00)==AMC_ACCEPT);CHECK(!s.upload&&s.prefix==0&&s.divider==3);CHECK(status(&s)==0x100);
    CHECK(amc_idle_reset(&s,0xa000)==AMC_ACCEPT);CHECK(status(&s)==0);
    CHECK(amc_idle_reset(&s,0xa00)==AMC_ACCEPT);amc_idle_dependencies(&s,8,0);CHECK(status(&s)==0);CHECK(!s.core_reset);
    amc_idle_dependencies(&s,0,0);CHECK(status(&s)==0);
    CHECK(amc_idle_reset(&s,0xa000)==AMC_ACCEPT);CHECK(amc_idle_reset(&s,0xa00)==AMC_ACCEPT);
    amc_idle_dependencies(&s,0,0x10);CHECK(status(&s)==0);CHECK(!s.core_reset&&s.divider==3);
    CHECK(!memcmp(&ram,&copy,sizeof ram));
    printf("isolated AMC idle reset: %u checks passed\n",checks);
    return 0;
}
