#include "test_main.c"
#include <stdlib.h>
static void full_ready(void) {
    main_setup(true);config.complete_main=true;
    setup();select_card();ready_endpoint();
    s.card.block_size[1]=32;write_reg(SDIO_BUFFER,0x1000);
    write_reg(SDIO_BLOCK_LENGTH,32);write_reg(SDIO_BLOCK_COUNT,2);
    sdio_bind_dma_reader(&s,main_dma,main_source);
    memcpy(s.helper.memory,supported_helper,sizeof supported_helper);s.helper.used=sizeof supported_helper;
    main_terminator();semantic_finish();
    CHECK(s.main_model.phase==MAIN_HEADER&&s.main_model.requested==16);
}
static void guards(void) {
    for(unsigned i=0;i<16;i++) {
        CHECK(s.main_model.low_before[i]==0xa5&&s.main_model.low_after[i]==0xa5);
        CHECK(s.main_model.high_before[i]==0xa5&&s.main_model.high_after[i]==0xa5);
    }
}
static void full_body(uint32_t destination,unsigned length,unsigned seed) {
    header(1,destination,length,seed);semantic_finish();
    CHECK(s.main_model.requested==length&&s.main_model.phase==MAIN_BODY);
    for(unsigned i=0;i<512;i++)main_source[i]=(uint8_t)(i*13+seed);
    uint8_t expected[512];memcpy(expected,main_source,512);
    main_transfer((length+31u)&~31u);semantic_finish();
    uint8_t *bank=destination<0xc0001000u?s.main_model.low+destination:s.main_model.high+destination-0xc0001000u;
    CHECK(!memcmp(bank,expected,length-4));
    CHECK(s.main_model.phase==MAIN_HEADER&&s.main_model.requested==16);
    guards();
}
static void load_exact(const char *path,void *bytes,size_t n) {
    FILE*f=fopen(path,"rb");CHECK(f!=NULL);if(!f)exit(2);
    CHECK(fread(bytes,1,n,f)==n);CHECK(fgetc(f)==EOF);fclose(f);
}
int main(void) {
    if(first_main_contract_main())return 1;
    unsigned previous=checks;
    for(unsigned n=4;n<=512;n++) {
        full_ready();uint32_t destination=(n&1)?0xc0001000u:0u;
        full_body(destination,n,n);
        CHECK(s.main_model.bodies==1&&s.main_model.copied==n-4);
        uint8_t *coverage=destination?s.main_model.high_coverage:s.main_model.low_coverage;
        for(unsigned i=0;i<512;i++)CHECK(coverage[i]==(i<n-4?1:0));
        /* Odd requests are codec-level contracts; Apple's original caller
         * treats their bit0 as an error, so this is not host acceptance. */
    }
    /* Exact upper bounds and independent banks, including arbitrary overwrites. */
    full_ready();full_body(0x17000-508,512,1);full_body(0xc0007000-508,512,2);
    full_body(0,88,3);full_body(0,88,4);full_body(0xc0001000,504,5);
    CHECK(s.main_model.bodies==5&&s.main_model.copied==508+508+84+84+500);
    uint32_t invalid[]={0x17000-507,0x17000,0x4002000,0xc0000000,0xc0000800,0xc0000a00,0xc0000fff,0xc0007000-507,0xc0007000,UINT32_MAX};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
        full_ready();header(1,invalid[i],512,0);semantic_finish();
        CHECK(s.main_model.phase==MAIN_STOPPED&&s.main_model.diagnostic==1&&!s.main_model.copied);
        CHECK(s.main_model.pending_length==32&&(s.events&0x20));guards();
    }
    /* Early command4 records only a handoff request, never a full-image claim. */
    full_ready();header(4,0x1234,0,0x9999);semantic_finish();
    CHECK(s.main_model.phase==MAIN_HANDOFF&&!s.main_model.requested&&!s.main_model.errors);
    CHECK(s.main_model.header[1]==0x1234&&s.main_model.header[3]==0x9999);
    CHECK(s.main_model.pending_length==32&&!s.main_model.bodies&&!s.main_model.copied);
    CHECK(!sdio_endpoint_can_receive(&s,config.io_port,32));
    CHECK(f1read(0x40)==SDIO_R5_COMMAND_STATE);guards();

    /* Full-mode completed body/header cancellation semantics, including short
     * rounded bodies whose last physical eight bytes are never copied. */
    for(unsigned mode=0;mode<6;mode++) {
        full_ready();header(1,0,88,0);semantic_finish();
        memset(main_source,0xe9,96);main_transfer(96);
        CHECK(s.main_model.pending_length==96&&s.main_model.phase==MAIN_BODY_PENDING);
        if(mode==0)sdio_bind_dma_reader(&s,main_dma,main_source);
        if(mode==1)sdio_host_reset(&s);
        if(mode==2)write_reg(SDIO_DCTRL,3);
        if(mode==3)CHECK(issue_preserving_latches(0x88000400)==SDIO_R5_COMMAND_STATE);
        if(mode==4)CHECK(issue_preserving_latches(0x98000600)==SDIO_R5_COMMAND_STATE);
        if(mode==5){now--;sdio_step(&s,now);}
        CHECK(s.main_model.pending_length==96&&s.main_model.pending[95]==0xe9);
        if(mode!=1&&mode!=2)CHECK(s.irq_pending==1&&(s.events&0x20));
        if(!s.endpoint.enabled){CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2));now+=config.function_init_ticks;sdio_step(&s,now);}
        if(!s.endpoint.host_power)CHECK(f1write(3,2)==(SDIO_R5_COMMAND_STATE|2));
        semantic_finish();CHECK(s.main_model.bodies==1&&s.main_model.copied==84);
        for(unsigned i=0;i<96;i++)CHECK(s.main_model.low[i]==(i<84?0xe9:0));
        guards();
    }
    full_ready();full_body(0,512,1);sdio_card_reset(&s);
    CHECK(s.main_model.phase==MAIN_OFF&&!s.main_model.bodies&&!s.helper.used);
    for(unsigned i=0;i<sizeof s.main_model.low;i++)CHECK(!s.main_model.low[i]&&!s.main_model.low_coverage[i]);
    guards();

    /* Deadline mixing remains observable with the general rounded decoder;
     * it cannot become an accepted528-byte transfer in a512-byte endpoint. */
    full_ready();header(1,0,512,0);s.main_model.deadline=now+150;
    unsigned lo=f1read(0x10)&255,hi=f1read(0x11)&255;
    CHECK(((hi<<8)|lo)==528);write_reg(SDIO_BLOCK_COUNT,17);
    CHECK(issue(0x501f5,0x9a000011)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
    CHECK(!s.data.armed&&!s.main_model.copied);

    /* Independently generated original-container oracle drives the public
     * transfer API, not the model's memory. Full actual guest proof is later. */
    uint8_t *container=malloc(122608),*expected_low=malloc(0x17000),*expected_high=malloc(0x6000);
    uint8_t *mask_low=malloc(0x17000),*mask_high=malloc(0x6000);
    CHECK(container&&expected_low&&expected_high&&mask_low&&mask_high);
    if(!container||!expected_low||!expected_high||!mask_low||!mask_high)return 3;
    load_exact(".context/sdio-first/main-full-plan/original-main.bin",container,122608);
    load_exact(".context/sdio-first/main-full-plan/expected-low.bin",expected_low,0x17000);
    load_exact(".context/sdio-first/main-full-plan/expected-high.bin",expected_high,0x6000);
    load_exact(".context/sdio-first/main-full-plan/expected-low-mask.bin",mask_low,0x17000);
    load_exact(".context/sdio-first/main-full-plan/expected-high-mask.bin",mask_high,0x6000);
    full_ready();unsigned pos=0,frames=0;
    while(pos<122608) {
        uint32_t command=0,length=0;for(unsigned j=0;j<4;j++){command|=(uint32_t)container[pos+j]<<(8*j);length|=(uint32_t)container[pos+8+j]<<(8*j);}
        memcpy(main_source,container+pos,16);main_transfer(32);semantic_finish();frames++;pos+=16;
        if(command==4)break;
        CHECK(s.main_model.requested==length&&length<=512&&pos+length<=122608);
        memcpy(main_source,container+pos,length);main_transfer((length+31)&~31);semantic_finish();frames++;pos+=length;
    }
    CHECK(pos==122608&&frames==467&&s.main_model.phase==MAIN_HANDOFF);
    CHECK(s.main_model.headers==234&&s.main_model.bodies==233&&s.main_model.copied==117932);
    CHECK(s.main_model.header[0]==4&&s.main_model.header[1]==0&&s.main_model.header[2]==0&&s.main_model.header[3]==0x188cdb1f);
    CHECK(!memcmp(s.main_model.low,expected_low,0x17000)&&!memcmp(s.main_model.high,expected_high,0x6000));
    CHECK(!memcmp(s.main_model.low_coverage,mask_low,0x17000)&&!memcmp(s.main_model.high_coverage,mask_high,0x6000));
    CHECK(!s.main_model.errors&&!s.data_errors);guards();
    free(container);free(expected_low);free(expected_high);free(mask_low);free(mask_high);
    printf("SDIO full-main contracts: %u new, %u total checks, %u failures\n",checks-previous,checks,failures);
    return failures?1:0;
}
