#include "amc_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static unsigned checks,allocations,frees;
#define CHECK(c) do {checks++;if(!(c)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#c);exit(2);}}while(0)
static uint8_t original[AMC_SRAM_SIZE],coverage[AMC_SRAM_SIZE],memory[AMC_QUEUE_BYTES];
static uint8_t stream[400000],pcm[4500000];static size_t stream_n,pcm_n;
static amc_sram_t sram;
static amc_idle_t idle;
static amc_idle_t prior_idle;
static amc_engine engine,before;
static uint8_t prior_sram[AMC_SRAM_SIZE];
static amc_program_policy policy={original,coverage,AMC_SRAM_SIZE};
static amc_physical_ram physical={memory,0x14a8b000,AMC_QUEUE_BYTES};
static const char *artifact_dir;
static FILE *published_pcm;
static FILE *artifact(const char *name) {
    char path[512];int n=snprintf(path,sizeof path,"%s/%s",artifact_dir,name);
    CHECK(n>0&&(size_t)n<sizeof path);FILE *f=fopen(path,"wb");CHECK(f);return f;
}
static void dump(const char *name,const void *bytes,size_t n) {
    if(!artifact_dir)return;
    FILE *f=artifact(name);CHECK(fwrite(bytes,1,n,f)==n);CHECK(!fclose(f));
}
static void *allocate(size_t n){void *p=malloc(n);if(p)allocations++;return p;}
static void release(void *p){if(p)frees++;free(p);}
static void *fail_allocate(size_t n){(void)n;return NULL;}
static void *fail_open(void){return NULL;}
static bool fail_configure(void *h,unsigned r,unsigned c){(void)h;(void)r;(void)c;return false;}
static bool oversized_decode(void *h,const uint8_t *b,size_t n,amc_decoded *out) {
    bool okay=amc_faad_ops.decode(h,b,n,out);
    if(okay&&out->samples)out->samples=AMC_OUTPUT_BYTES/2+1;
    return okay; /* Corrupt metadata only: no synthetic PCM is generated. */
}
static size_t load(const char *path,uint8_t *p,size_t capacity) {
    FILE *f=fopen(path,"rb");CHECK(f);size_t n=fread(p,1,capacity,f);CHECK(feof(f)||fgetc(f)==EOF);fclose(f);return n;
}
static uint32_t read_reg(uint32_t off){uint32_t v=0xdeadbeef;CHECK(amc_engine_read(&engine,off,4,&v)==AMC_ENGINE_OK);return v;}
static uint16_t half(uint32_t off){return sram.bytes[off]|(uint16_t)sram.bytes[off+1]<<8;}
static void write_reg(uint32_t off,uint32_t v){CHECK(amc_engine_write(&engine,&physical,off,4,v)==AMC_ENGINE_OK);}
static void save_before(void){before=engine;prior_idle=idle;memcpy(prior_sram,sram.bytes,AMC_SRAM_SIZE);}
static void unchanged(void){CHECK(!memcmp(&before,&engine,sizeof engine));CHECK(!memcmp(&prior_idle,&idle,sizeof idle));CHECK(!memcmp(prior_sram,sram.bytes,AMC_SRAM_SIZE));}
static void reject_reg(uint32_t off,unsigned width,uint32_t v) {
    save_before();CHECK(amc_engine_write(&engine,&physical,off,width,v)==AMC_ENGINE_REJECT);unchanged();
}
static void reject_sram(uint32_t off,unsigned width,uint32_t v) {
    save_before();CHECK(amc_engine_sram_write(&engine,off,width,v)==AMC_ENGINE_REJECT);unchanged();
}
static void idle_prefix(void) {
    amc_idle_init(&idle);amc_idle_dependencies(&idle,0,0);
    CHECK(amc_idle_reset(&idle,0xa000)==AMC_ACCEPT);CHECK(amc_idle_reset(&idle,0xa00)==AMC_ACCEPT);
    const uint32_t off[]={0xa00,0x184,0x180,0xb10,0xb04,0x340};
    const uint32_t val[]={0,0,1,UINT32_MAX,UINT32_MAX,0};
    for(unsigned i=0;i<6;i++)CHECK(amc_idle_write(&idle,off[i],4,val[i])==AMC_ACCEPT);
    CHECK(amc_idle_write(&idle,0x340,4,1)==AMC_UPLOAD_END);CHECK(amc_idle_observe_upload(&idle));
    const uint32_t post[]={0x101c,0x100c,0x1000,0x1004,0x1008,0xa00,0x300,0x310,0x304,0x314,0x308,0x318,0x30c,0x31c,0x320,0x324,0x300,0x304,0xd0c};
    const uint32_t pv[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x07000001,0x3f081603,0x40};
    for(unsigned i=0;i<19;i++)CHECK(amc_idle_write(&idle,post[i],4,pv[i])==AMC_ACCEPT);
    CHECK(amc_idle_write(&idle,0,4,1)==AMC_RUN_REQUEST);
}
static amc_engine_result start(const amc_codec_ops *ops,void *(*a)(size_t)) {
    return amc_engine_start(&engine,&idle,&sram,coverage,&policy,ops,a,release);
}
static void fresh(void) {
    amc_engine_destroy(&engine);amc_engine_init(&engine);idle_prefix();memcpy(sram.bytes,original,AMC_SRAM_SIZE);
    CHECK(start(&amc_faad_ops,allocate)==AMC_ENGINE_OK);
}
static void setup(void){write_reg(0x204,1);write_reg(0x184,7);write_reg(0xb0c,0x2000);write_reg(0x1018,1);}
static void stage_dma(uint32_t address,uint32_t words,bool first) {
    write_reg(0x101c,1);if(first)write_reg(0x100c,0);write_reg(0x1000,1);
    write_reg(0x1004,address);write_reg(0x1008,words);if(first)write_reg(0xa00,1);write_reg(0x100c,1);
}
static void configure(void) {
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION,2,0x1b)==AMC_ENGINE_OK);
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+2,2,2)==AMC_ENGINE_OK);
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+4,2,0)==AMC_ENGINE_OK);
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+10,2,3)==AMC_ENGINE_OK);
}
static void masks(void) {
    for(unsigned i=0;i<AMC_SRAM_SIZE;i++) {
        bool init=i>=AMC_HEADER+8&&i<AMC_HEADER+20;
        bool output=(i>=AMC_OUTPUT0&&i<AMC_OUTPUT0+AMC_OUTPUT_BYTES)||
                    (i>=AMC_OUTPUT1&&i<AMC_OUTPUT1+AMC_OUTPUT_BYTES)||
                    (i>=AMC_HEADER+8&&i<AMC_HEADER+16);
        bool host=(i>=AMC_HEADER+8&&i<AMC_HEADER+12)||
                  (i>=AMC_SESSION&&i<AMC_SESSION+6)||(i>=AMC_SESSION+10&&i<AMC_SESSION+12);
        CHECK(!engine.init_writes[i]||init);CHECK(!engine.pcm_writes[i]||output);CHECK(!engine.host_writes[i]||host);
        if(!engine.init_writes[i]&&!engine.pcm_writes[i]&&!engine.host_writes[i])CHECK(sram.bytes[i]==original[i]);
    }
}
static void compare_publication(uint64_t index) {
    unsigned which=(unsigned)(index&1);uint32_t base=which?AMC_OUTPUT1:AMC_OUTPUT0;
    CHECK(half(AMC_HEADER+8+2*which)==0);CHECK(half(AMC_HEADER+12+2*which)==2048);
    CHECK(4096*(index+1)<=pcm_n);CHECK(!memcmp(sram.bytes+base,pcm+4096*index,4096));
    if(published_pcm)CHECK(fwrite(sram.bytes+base,1,4096,published_pcm)==4096);
}
static void consume_publication(uint64_t index) {
    CHECK(amc_engine_sram_write(&engine,AMC_HEADER+8+2*(index&1),2,1)==AMC_ENGINE_OK);
    write_reg(4,1);write_reg(0xb04,0x400);
}
int main(int argc,char **argv) {
    struct rlimit address={256u*1024u*1024u,256u*1024u*1024u},cpu={120,120};
    CHECK(!setrlimit(RLIMIT_AS,&address));CHECK(!setrlimit(RLIMIT_CPU,&cpu));alarm(180);
    CHECK(argc==5);CHECK(load(argv[1],original,sizeof original)==sizeof original);
    artifact_dir=getenv("AMC_TEST_ARTIFACTS");if(artifact_dir)published_pcm=artifact("published-pcm.bin");
    CHECK(load(argv[2],coverage,sizeof coverage)==sizeof coverage);
    stream_n=load(argv[3],stream,sizeof stream);pcm_n=load(argv[4],pcm,sizeof pcm);
    CHECK(stream_n==364719&&pcm_n==4374528);
    for(unsigned type=0;type<8;type++)if(type!=1) {
        uint8_t malformed[7];memcpy(malformed,stream,sizeof malformed);
        malformed[0]=(uint8_t)((malformed[0]&31)|(type<<5));
        amc_decoded untouched={123,456,NULL},got=untouched;
        CHECK(!amc_faad_ops.probe(malformed,sizeof malformed,48000,&got));
        CHECK(!memcmp(&untouched,&got,sizeof got));
    }
    CHECK(amc_engine_layout(AMC_HEADER,AMC_OUTPUT0,AMC_OUTPUT1,AMC_OUTPUT_BYTES));
    CHECK(!amc_engine_layout(AMC_HEADER,AMC_HEADER,AMC_OUTPUT1,AMC_OUTPUT_BYTES));
    CHECK(!amc_engine_layout(AMC_HEADER,AMC_OUTPUT0,AMC_OUTPUT0,AMC_OUTPUT_BYTES));
    CHECK(!amc_engine_layout(AMC_HEADER,AMC_OUTPUT0+2,AMC_OUTPUT1+2,AMC_OUTPUT_BYTES));
    CHECK(!amc_engine_layout(AMC_HEADER,0xfffff220,0x11220,AMC_OUTPUT_BYTES));
    CHECK(!amc_engine_layout(AMC_HEADER,0x11220,0x13220,AMC_OUTPUT_BYTES));
    CHECK(!amc_engine_layout(AMC_HEADER,AMC_OUTPUT0,AMC_OUTPUT1,8191));

    amc_engine_init(&engine);idle_prefix();memcpy(sram.bytes,original,sizeof original);
    save_before();idle.enabled=false;CHECK(start(&amc_faad_ops,allocate)==AMC_ENGINE_REJECT);idle.enabled=true;unchanged();
    sram.bytes[0x1c000]^=1;save_before();CHECK(start(&amc_faad_ops,allocate)==AMC_ENGINE_REJECT);unchanged();sram.bytes[0x1c000]^=1;
    coverage[0]^=1;uint8_t actual[AMC_SRAM_SIZE];memcpy(actual,coverage,sizeof actual);coverage[0]^=1;
    save_before();CHECK(amc_engine_start(&engine,&idle,&sram,actual,&policy,&amc_faad_ops,allocate,release)==AMC_ENGINE_REJECT);unchanged();
    save_before();CHECK(start(&amc_faad_ops,fail_allocate)==AMC_ENGINE_REJECT);unchanged();
    amc_codec_ops fail=amc_faad_ops;fail.open=fail_open;
    CHECK(start(&fail,allocate)==AMC_ENGINE_REJECT);unchanged();CHECK(allocations==frees);
    CHECK(start(&amc_faad_ops,allocate)==AMC_ENGINE_OK);CHECK(read_reg(0xb18)==0x300);CHECK(!amc_engine_irq(&engine));
    CHECK(read_reg(0x10)==1);CHECK(half(AMC_HEADER+16)==0x10&&half(AMC_HEADER+18)==0x1010);masks();
    for(unsigned i=0;i<3;i++) {
        save_before();CHECK(amc_engine_dependencies(&engine,i<2?(8u<<i):0,i==2?0x10:0)==AMC_ENGINE_REJECT);unchanged();
    }
    save_before();CHECK(amc_engine_dependencies(&engine,0x80000000,0x80000000)==AMC_ENGINE_OK);unchanged();
    /* Original resume writes both owner halfwords1, even if already1. */
    for(unsigned i=0;i<2;i++) {
        CHECK(amc_engine_sram_write(&engine,AMC_HEADER+8+2*i,2,1)==AMC_ENGINE_OK);
        CHECK(half(AMC_HEADER+8+2*i)==1&&engine.events==0x200&&engine.service&&!engine.next_output);
        save_before();CHECK(amc_engine_sram_write(&engine,AMC_HEADER+8+2*i,2,1)==AMC_ENGINE_OK);unchanged();
    }
    save_before();CHECK(start(&amc_faad_ops,allocate)==AMC_ENGINE_REJECT);unchanged();
    reject_reg(0x204,2,1);reject_reg(0x204,4,2);reject_reg(0,4,1);setup();
    const uint32_t unknown[]={8,0x100,0x180,0x184,0x188,0x340,0x400,0x404,0x410,0x47c,0x1014,0xb00,0xb08,0xb20,0x2000};
    for(unsigned i=0;i<sizeof unknown/sizeof unknown[0];i++)reject_reg(unknown[i],4,1);
    reject_reg(0xb04,4,8);reject_reg(0xb0c,4,UINT32_MAX);reject_reg(0x1018,4,2);
    for(unsigned width=1;width<=8;width++)if(width!=4)reject_reg(4,width,1);
    reject_sram(AMC_HEADER+16,2,0x11);reject_sram(AMC_SESSION+10,2,3);reject_sram(AMC_SESSION,4,0x1b);
    memcpy(memory,stream,AMC_QUEUE_BYTES);stage_dma(physical.base,4096,true);
    const uint32_t staging[]={0x100c,0x1000,0x1004,0x1008,0xa00};
    for(unsigned i=0;i<sizeof staging/sizeof staging[0];i++)reject_reg(staging[i],4,0);
    write_reg(0x101c,1);write_reg(0x101c,1);
    reject_reg(0x100c,4,0);reject_reg(0x100c,4,1);reject_reg(0x1000,4,1);
    reject_reg(0x1004,4,physical.base+4);reject_reg(0x1008,4,4);reject_reg(0xa00,4,1);
    CHECK(read_reg(0x1010)==physical.base);CHECK(!engine.input_pending&&!engine.copied_words);
    for(unsigned i=1;i<=4;i++) {
        CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);
        CHECK(read_reg(0x1010)==physical.base+4096*i);CHECK(engine.copied_words==1024*i);
        CHECK(engine.input_pending==(i==4));CHECK(!engine.published_frames);
    }
    CHECK(!memcmp(engine.queue,stream,16384));memory[0]^=0xff;CHECK(engine.queue[0]==stream[0]);memory[0]^=0xff;
    CHECK(read_reg(0x1010)!=physical.base+engine.copied_words); /* Reject word-address mutant. */
    idle.enabled=false;save_before();CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);unchanged();CHECK(!amc_engine_irq(&engine));idle.enabled=true;
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION,2,0x1b)==AMC_ENGINE_OK);
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+2,2,2)==AMC_ENGINE_OK);
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+4,2,0)==AMC_ENGINE_OK);
    reject_sram(AMC_SESSION+10,2,4);CHECK(!engine.configured);
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+10,2,3)==AMC_ENGINE_OK);
    write_reg(0x1018,0);write_reg(0xb0c,0x400);
    CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_OK);CHECK(engine.decoded_frames==1&&engine.decoded_bytes==7);
    CHECK(engine.published_frames==0&&!(engine.events&0x400));
    CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_PUBLISHED);compare_publication(0);
    CHECK(amc_engine_irq(&engine));write_reg(4,1);CHECK(engine.published_frames==1);
    save_before();CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);unchanged();
    write_reg(0xb10,0x400);CHECK(!amc_engine_irq(&engine)&&read_reg(0xb1c)==0);CHECK(engine.events==0x600);
    write_reg(0xb04,0x200);CHECK(engine.events==0x400);CHECK(read_reg(0xb18)==0x500);
    write_reg(0xb0c,0x400);CHECK(amc_engine_irq(&engine));write_reg(0xb04,0x400);CHECK(!amc_engine_irq(&engine));
    CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_PUBLISHED);compare_publication(1);
    write_reg(4,1);write_reg(0xb04,0x400);save_before();CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);unchanged();
    CHECK(amc_engine_sram_write(&engine,AMC_HEADER+8,2,1)==AMC_ENGINE_OK);
    CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_PUBLISHED);compare_publication(2);
    CHECK(amc_engine_sram_write(&engine,AMC_HEADER+10,2,1)==AMC_ENGINE_OK);consume_publication(2);
    for(unsigned pub=3;pub<48;pub++) {
        CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_PUBLISHED);compare_publication(pub);consume_publication(pub);
    }
    CHECK(engine.decoded_frames==49&&engine.decoded_bytes==16065&&engine.queue_used==319);
    CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);
    unsigned attempts=engine.probe_attempts;save_before();
    for(unsigned i=0;i<50;i++)CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);
    unchanged();CHECK(engine.probe_attempts==attempts);
    memcpy(memory,stream+16384,52);write_reg(0x101c,1); /* ISR ack, then helper ack again. */
    stage_dma(physical.base,13,false);CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_PUBLISHED);
    compare_publication(48);CHECK(engine.decoded_frames==50&&engine.decoded_bytes==16436&&engine.queue_used==0);
    CHECK(read_reg(0x1010)==physical.base+52&&engine.copied_words==13&&engine.input_pending);
    write_reg(0x1018,1);CHECK(amc_engine_irq(&engine));write_reg(0xb04,0x400);CHECK(amc_engine_irq(&engine));
    write_reg(0x101c,1);CHECK(!amc_engine_irq(&engine));masks();
    dump("after-frame50-sram.bin",sram.bytes,AMC_SRAM_SIZE);
    dump("init-written.bin",engine.init_writes,AMC_SRAM_SIZE);
    dump("pcm-written.bin",engine.pcm_writes,AMC_SRAM_SIZE);
    dump("host-written.bin",engine.host_writes,AMC_SRAM_SIZE);
    if(published_pcm){CHECK(!fclose(published_pcm));published_pcm=NULL;}
    memcpy(prior_sram,sram.bytes,AMC_SRAM_SIZE);write_reg(0xc,1);
    CHECK(read_reg(0x10)==0&&read_reg(0xb18)==0x100);CHECK(!engine.decoder&&!engine.queue&&!amc_engine_irq(&engine));
    CHECK(!memcmp(prior_sram,sram.bytes,AMC_SRAM_SIZE));CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_REJECT);
    reject_reg(4,4,1);CHECK(allocations==frees);
    CHECK(amc_engine_dependencies(&engine,8,0)==AMC_ENGINE_OK);CHECK(!idle.enabled&&!idle.de_complete);

    /* Resource and queue backpressure independently of decoder throughput. */
    fresh();setup();memcpy(memory,stream,AMC_QUEUE_BYTES);stage_dma(physical.base,16384,true);
    for(unsigned i=0;i<16;i++)CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);
    CHECK(engine.queue_used==AMC_QUEUE_BYTES&&engine.input_pending);
    stage_dma(physical.base,1,false);save_before();CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);unchanged();
    CHECK(engine.dma_active&&!engine.input_pending&&read_reg(0x1010)==physical.base);
    write_reg(0xc,1);CHECK(allocations==frees);

    fresh();setup();configure();save_before();CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);unchanged();
    CHECK(engine.decoded_frames==0&&engine.published_frames==0&&!(engine.events&0x400));
    write_reg(0x101c,1);write_reg(0x1000,1);
    write_reg(0x101c,1);CHECK(engine.input_phase==3); /* Acknowledgement does not erase staging. */
    reject_reg(0x1004,4,physical.base+1);reject_reg(0x1004,4,UINT32_MAX);write_reg(0x1004,physical.base+AMC_QUEUE_BYTES-4);
    reject_reg(0x1008,4,2);reject_reg(0x1008,4,0);reject_reg(0x1008,4,UINT32_MAX);
    amc_engine_destroy(&engine);
    fresh();setup();sram.bytes[AMC_HEADER+12]=1;save_before();CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_REJECT);unchanged();
    amc_engine_destroy(&engine);
    idle_prefix();memcpy(sram.bytes,original,sizeof original);amc_engine_init(&engine);
    fail=amc_faad_ops;fail.configure=fail_configure;CHECK(start(&fail,allocate)==AMC_ENGINE_OK);setup();
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION,2,0x1b)==AMC_ENGINE_OK);
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+2,2,2)==AMC_ENGINE_OK);
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+4,2,0)==AMC_ENGINE_OK);
    write_reg(0xb0c,0x200);CHECK(amc_engine_irq(&engine));
    CHECK(amc_engine_sram_write(&engine,AMC_SESSION+10,2,3)==AMC_ENGINE_FATAL);
    CHECK(engine.fault&&!engine.configured&&engine.events==0x200&&amc_engine_irq(&engine));
    CHECK(read_reg(0xb1c)==0x200&&read_reg(0xb18)==0x300);
    CHECK(half(AMC_SESSION+10)==0);amc_engine_destroy(&engine);CHECK(allocations==frees);
    idle_prefix();memcpy(sram.bytes,original,sizeof original);amc_engine_init(&engine);
    fail=amc_faad_ops;fail.decode=oversized_decode;CHECK(start(&fail,allocate)==AMC_ENGINE_OK);setup();configure();
    memcpy(memory,stream,AMC_QUEUE_BYTES);stage_dma(physical.base,4096,true);
    CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_OK); /* Real startup zero-output frame. */
    CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_FATAL);
    CHECK(engine.fault&&!engine.published_frames&&engine.events==0x200);
    CHECK(half(AMC_HEADER+8)==1&&half(AMC_HEADER+12)==0);
    for(unsigned i=0;i<AMC_SRAM_SIZE;i++)CHECK(!engine.pcm_writes[i]);
    amc_engine_destroy(&engine);CHECK(allocations==frees);
    fresh();setup();configure();memcpy(memory,stream,16);memory[0]=(memory[0]&31)|0xa0;
    stage_dma(physical.base,4,true);CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_FATAL);
    CHECK(engine.fault&&!engine.decoded_frames&&!engine.published_frames&&engine.events==0x200);
    CHECK(engine.input_pending&&amc_engine_irq(&engine)); /* Real completed copy survives diagnostic stop. */
    CHECK(half(AMC_HEADER+8)==1&&half(AMC_HEADER+12)==0);
    amc_engine_destroy(&engine);CHECK(allocations==frees);
    printf("PASS %u checks; real49+partial319 then52 bytes gives frame50; exact first49 PCM publications; no guest run\n",checks);
    return 0;
}
