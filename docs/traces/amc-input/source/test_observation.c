#define main prefix_probe_main
#include "probe.c"
#undef main
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"observation line%u %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t zero_clock(void*c,uint32_t a){(void)c;(void)a;return 0;}
static void setup(void){
 memset(&machine,0,sizeof machine);memset(&original,0,sizeof original);original.read32=zero_clock;
 amc_idle_init(&idle);amc_sram_init(&sram);amc_sram_init(&expected);
 memset(predicted,0,sizeof predicted);memset(written,0,sizeof written);memset(footprint,0,sizeof footprint);
 idle.enabled=idle.core_reset=idle.de_complete=idle.upload=true;idle.prefix=6;enabled=true;upload_verified=false;result=0;
 descriptors=1;expectedBytes=1;predicted[0]=1;written[0]=1;footprint[0]=1;
}
static void expect_stop(void){amc_idle_t saved=idle;
 if(!setjmp(finish)){writebus(&machine,0x38500340,4,1);CHECK(false);}
 CHECK(result==1);CHECK(!upload_verified&&!idle.upload_observed);CHECK(!memcmp(&saved,&idle,sizeof idle));
}
int main(void){
 setup();descriptors=0;expect_stop();
 setup();sram.bytes[0]=1;expect_stop();
 setup();written[0]=0;expect_stop(); /* Expected zero cannot masquerade as a guest write. */
 setup();writebus(&machine,0x38500340,4,1);CHECK(upload_verified&&idle.upload_observed);CHECK(!idle.input_pending);
 amc_sram_t before=sram;unsigned old=sramW;
 if(!setjmp(finish)){writebus(&machine,AMC_SRAM_BASE,4,0);CHECK(false);}
 CHECK(result==1);CHECK(sramW==old&&!memcmp(&before,&sram,sizeof before));
 printf("isolated upload observation/coverage: %u checks passed\n",checks);return 0;
}
