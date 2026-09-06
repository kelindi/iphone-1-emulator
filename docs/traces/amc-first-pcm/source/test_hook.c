#define main guest_probe_main
#include "probe.c"
#undef main
static unsigned checks, physical_writes;
static uint32_t fake_read(void*c,uint32_t a){(void)c;(void)a;return 0;}
static void fake_write(void*c,uint32_t a,uint32_t v){(void)c;(void)a;(void)v;physical_writes++;}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"hook:%u %s\n",__LINE__,#x);exit(1);}}while(0)
static int16_t samples[]={42,-42};
static void *fake_open(void){return malloc(1);}
static bool fake_config(void*p,unsigned rate,unsigned channels){return p&&rate==48000&&channels==2;}
static bool fake_probe(const uint8_t*p,size_t n,unsigned rate,amc_decoded*out){if(!p||!n||rate!=48000)return false;*out=(amc_decoded){1,2,samples};return true;}
static bool fake_decode(void*p,const uint8_t*b,size_t n,amc_decoded*out){return p&&fake_probe(b,n,48000,out);}
static const amc_codec_ops fake_ops={fake_open,free,fake_config,fake_probe,fake_decode};
static amc_sram_t saved_sram;
static amc_engine saved_engine;
static void reject_write(uint32_t address,unsigned width,uint32_t value){
 saved_sram=sram;saved_engine=engine;unsigned before=physical_writes;result=0;
 if(!setjmp(finish)){writebus(&machine,address,width,value);CHECK(false);}
 CHECK(result==1);CHECK(!memcmp(&saved_sram,&sram,sizeof sram));
 CHECK(!memcmp(&saved_engine,&engine,sizeof engine));CHECK(physical_writes==before);
}
int main(void){
 memset(&machine,0,sizeof machine);original.read32=fake_read;original.write32=fake_write;
 amc_idle_init(&idle);idle.enabled=true;idle.de_complete=true;idle.upload_observed=true;idle.post_prefix=19;
 amc_sram_init(&sram);amc_engine_init(&engine);enabled=true;
 CHECK(amc_engine_start(&engine,&idle,&sram,written,&policy,&fake_ops,malloc,free)==AMC_ENGINE_OK);engine_started=true;
 CHECK(engine.events==0x200&&engine.initialized);CHECK(idle.armed);
 reject_write(0x38500204,4,2);reject_write(0x3c500048,4,0x18);reject_write(0x39a00000,4,0);reject_write(AMC_SRAM_BASE+AMC_OUTPUT0,2,1);
 writebus(&machine,AMC_SRAM_BASE+AMC_HEADER+8,2,1);CHECK(engine.host_writes[AMC_HEADER+8]);
 writebus(&machine,0x38500204,4,1);writebus(&machine,0x38500184,4,7);writebus(&machine,0x38500b0c,4,0x2000);writebus(&machine,0x38501018,4,1);
 CHECK(engine.setup_phase==4);engine.dma_active=true;
 reject_write(0x3850100c,4,0);reject_write(0x38501000,4,1);
 engine.dma_active=false;engine.configured=true;engine.service=true;engine.events=0;engine.mask=0x400;
 engine.queue[0]=0x20;engine.queue_used=1;engine.queue_epoch++;machine.cpu.cycles=1234;
 layout_seen=true;input_seen=true;first_input_cycles=100;uint32_t raw=machine.vic[0].raw;result=0;service_boundary();
 CHECK(result==5);CHECK(machine.cpu.cycles==1234);CHECK(engine.published_frames==1);
 CHECK(engine.events==0x400);CHECK(amc_engine_irq(&engine));CHECK(machine.vic[0].raw==raw);
 CHECK(sram.bytes[AMC_OUTPUT0]==42&&sram.bytes[AMC_OUTPUT0+2]==214);CHECK(engine.pcm_writes[AMC_OUTPUT0]);
 amc_engine_destroy(&engine);printf("isolated guest-hook contracts: %u checks passed; synthetic PCM only in wrapper unit test\n",checks);return 0;
}
