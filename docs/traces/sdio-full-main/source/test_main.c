#include "test_helper.c"
#include "supported_helper.inc"
static uint8_t main_source[512];
static bool main_mapped=true;
static const uint8_t *main_dma(void *ctx,uint32_t address,size_t length) {
    CHECK(ctx==main_source);
    return main_mapped&&address==0x1000&&length<=sizeof main_source ? main_source:NULL;
}
static void main_setup(bool valid_identity) {
    config=(sdio_config_t){.present=true,.enumeration=true,.boot_endpoint=true,
      .data_transfer=true,.helper_receiver=true,.helper_semantics=true,
      .semantic_ticks=1000,.helper_capacity=4096,.endpoint_capacity=512,
      .data_ticks=500,.function_init_ticks=300,.io_port=0x10000,
      .io_functions=1,.assigned_rca=1,.voltage_mask=0x60000,
      .readable_cccr=0x101,.command_ticks=100};
    setup();select_card();ready_endpoint();
    s.card.block_size[1]=32;write_reg(SDIO_BUFFER,0x1000);
    write_reg(SDIO_BLOCK_LENGTH,32);write_reg(SDIO_BLOCK_COUNT,2);
    sdio_bind_dma_reader(&s,main_dma,main_source);main_mapped=true;
    /* Identity fixture seeds only received program bytes in this setup.
     * A separate below test uses all37 genuine physical frames. */
    memcpy(s.helper.memory,supported_helper,sizeof supported_helper);
    s.helper.used=sizeof supported_helper;
    if(!valid_identity)s.helper.memory[100]^=1;
}
static void main_transfer(unsigned length) {
    write_reg(SDIO_BLOCK_COUNT,length/32);
    CHECK(issue(0x501f5,0x9a000000u|length/32)==SDIO_R5_COMMAND_STATE);
    CHECK(s.data.armed&&!s.data.active);write_reg(SDIO_DCTRL,0x10);
    uint64_t before=s.data_completions,due=s.data.deadline;
    now=due-1;sdio_step(&s,now);CHECK(s.data_completions==before);
    now=due;sdio_step(&s,now);
    CHECK(s.data_completions==before+1&&!s.data.active);
    CHECK((s.events&0x20)&&s.irq_pending==1);
}
static void main_terminator(void) {
    memset(main_source,0,32);main_transfer(32);
}
static void semantic_finish(void) {
    if(s.main_model.waiting_deadline)sdio_step(&s,now);
    CHECK(s.main_model.deadline>now);
    uint16_t request=s.main_model.requested;
    now=s.main_model.deadline-1;sdio_step(&s,now);CHECK(s.main_model.requested==request);
    now++;sdio_step(&s,now);
}
static void main_ready(void) {
    main_setup(true);main_terminator();
    CHECK(s.main_model.phase==MAIN_BOOT_PENDING&&s.main_model.requested==0);
    semantic_finish();CHECK(s.main_model.phase==MAIN_HEADER&&s.main_model.requested==16);
}
static void put_word(unsigned offset,uint32_t value) {
    for(unsigned i=0;i<4;i++)main_source[offset+i]=(uint8_t)(value>>(8*i));
}
static void header(unsigned cmd,uint32_t destination,uint32_t length,uint32_t opaque) {
    memset(main_source,0xcc,32);put_word(0,cmd);put_word(4,destination);
    put_word(8,length);put_word(12,opaque);main_transfer(32);
}
static void body_pending(unsigned seed) {
    main_ready();header(1,0xc0001000,512,seed);semantic_finish();
    CHECK(s.main_model.phase==MAIN_BODY&&s.main_model.requested==512);
    for(unsigned i=0;i<512;i++)main_source[i]=(uint8_t)((i*37+seed)^(i>>2));
    main_transfer(512);CHECK(s.main_model.phase==MAIN_BODY_PENDING);
    CHECK(!memcmp(s.main_model.pending,main_source,512)&&s.main_model.pending_length==512);
    for(unsigned i=0;i<512;i++)CHECK(s.main_model.memory[i]==0);
}
static uint32_t issue_preserving_latches(uint32_t arg) {
    write_reg(SDIO_ARG,arg);write_reg(SDIO_CMD,0x50074);
    write_reg(SDIO_CMD,SDIO_EXECUTE|0x50074);
    now+=config.command_ticks;sdio_step(&s,now);return read_reg(SDIO_RESP0);
}
int first_main_contract_main(void) {
    if(helper_contract_main())return 1;
    unsigned previous=checks;
    main_setup(true);s.helper.used=0;memset(s.helper.memory,0,sizeof s.helper.memory);
    unsigned position=0;
    while(position<sizeof supported_helper) {
        unsigned length=sizeof supported_helper-position;if(length>60)length=60;
        put_word(0,length);memcpy(main_source+4,supported_helper+position,length);
        main_transfer(64);position+=length;
        CHECK(s.main_model.phase==MAIN_OFF&&s.main_model.requested==0);
    }
    main_terminator();CHECK(s.helper.used==2140&&s.helper.frames==37);
    CHECK(!memcmp(s.helper.memory,supported_helper,2140));
    CHECK(s.main_model.phase==MAIN_BOOT_PENDING);semantic_finish();
    CHECK(s.main_model.requested==16&&s.main_model.publications==1);
    main_setup(false);main_terminator();now+=10000;sdio_step(&s,now);
    CHECK(s.main_model.phase==MAIN_OFF&&s.main_model.requested==0&&s.helper.terminated);
    CHECK(s.main_model.errors==0); /* unsupported code is stored, not falsely executed */

    for(unsigned seed=0;seed<256;seed++) {
        body_pending(seed);uint8_t expected[512];memcpy(expected,main_source,512);
        /* Mutating source after physical completion cannot change card bytes. */
        memset(main_source,0,512);semantic_finish();
        CHECK(s.main_model.bodies==1&&s.main_model.copied==508&&s.main_model.requested==16);
        CHECK(!memcmp(s.main_model.memory,expected,508));
        for(unsigned i=508;i<512;i++)CHECK(s.main_model.memory[i]==0);
        CHECK(!s.main_model.errors&&!s.main_model.pending_length&&s.main_model.headers==1);
        for(unsigned i=0;i<100;i++){sdio_step(&s,now);CHECK(s.main_model.bodies==1&&s.main_model.copied==508);}
        CHECK(f1read(0x40)==SDIO_R5_COMMAND_STATE&&f1read(0x41)==SDIO_R5_COMMAND_STATE);
    }
    /* Invalid headers do not invent SDIO CRC/errors or partially write RAM. */
    uint32_t lengths[]={0,1,2,3,4,31,508,513,0x800,UINT32_MAX};
    for(unsigned i=0;i<sizeof lengths/sizeof lengths[0];i++) {
        main_ready();header(1,0xc0001000,lengths[i],0);semantic_finish();
        CHECK(s.main_model.phase==MAIN_STOPPED&&s.main_model.diagnostic==1);
        CHECK(s.main_model.copied==0&&!s.data_errors&&(s.events&0x20)&&s.irq_pending==1);
        for(unsigned j=0;j<512;j++)CHECK(s.main_model.memory[j]==0);
    }
    uint32_t dests[]={0,0xc0000fff,0xc0001005,0xc0001200,UINT32_MAX};
    for(unsigned i=0;i<sizeof dests/sizeof dests[0];i++) {
        main_ready();header(1,dests[i],512,0);semantic_finish();
        CHECK(s.main_model.phase==MAIN_STOPPED&&s.main_model.copied==0&&s.main_model.diagnostic==1);
    }
    main_ready();header(1,0xc0001004,512,0);semantic_finish();
    CHECK(s.main_model.phase==MAIN_BODY); /* exact upper boundary, not partial mapping */
    main_ready();header(99,0,0,0);semantic_finish();
    CHECK(s.main_model.phase==MAIN_HEADER&&s.main_model.requested==17&&!s.main_model.errors&&!s.main_model.copied);
    main_ready();header(4,0,0,0);semantic_finish();
    CHECK(s.main_model.phase==MAIN_STOPPED&&s.main_model.diagnostic==2&&!s.main_model.requested&&!s.main_model.copied);

    /* Completed bytes survive every host-only cancellation boundary. */
    for(unsigned mode=0;mode<6;mode++) {
        body_pending(mode);uint8_t retained[512];memcpy(retained,s.main_model.pending,512);
        if(mode==0)sdio_bind_dma_reader(&s,main_dma,main_source);
        if(mode==1)sdio_host_reset(&s);
        if(mode==2)write_reg(SDIO_DCTRL,3);
        if(mode==3)CHECK(issue_preserving_latches(0x88000400)==SDIO_R5_COMMAND_STATE);
        if(mode==4)CHECK(issue_preserving_latches(0x98000600)==SDIO_R5_COMMAND_STATE);
        if(mode==5){now--;sdio_step(&s,now);}
        CHECK(s.main_model.pending_length==512&&!memcmp(retained,s.main_model.pending,512));
        CHECK(s.main_model.copied==0);
        if(mode!=1&&mode!=2)CHECK(s.irq_pending==1&&(s.events&0x20));
        if(!s.endpoint.enabled){CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2));now+=config.function_init_ticks;sdio_step(&s,now);}
        if(!s.endpoint.host_power)CHECK(f1write(3,2)==(SDIO_R5_COMMAND_STATE|2));
        semantic_finish();CHECK(s.main_model.copied==508&&!memcmp(retained,s.main_model.memory,508));
    }
    body_pending(7);sdio_card_reset(&s);now+=10000;sdio_step(&s,now);
    CHECK(s.main_model.phase==MAIN_OFF&&!s.main_model.pending_length&&!s.main_model.copied&&!s.helper.used);
    for(unsigned i=0;i<512;i++)CHECK(!s.main_model.memory[i]&&!s.main_model.pending[i]);

    /* Pending header bytes are card-owned too; controller/FIFO resets and
     * rebind must not replace them with zeroed/reused host staging. */
    for(unsigned mode=0;mode<3;mode++) {
        main_ready();header(1,0xc0001000,512,0x12345678);
        uint8_t retained[32];memcpy(retained,s.main_model.pending,32);
        if(mode==0)sdio_host_reset(&s);
        if(mode==1)write_reg(SDIO_DCTRL,3);
        if(mode==2)sdio_bind_dma_reader(&s,main_dma,main_source);
        memset(main_source,0,32);
        CHECK(s.main_model.pending_length==32&&!memcmp(retained,s.main_model.pending,32));
        semantic_finish();CHECK(s.main_model.requested==512&&s.main_model.header[3]==0x12345678);
    }
    /* Uncommitted body DMA cancels; it must not turn into retained card data. */
    for(unsigned mode=0;mode<6;mode++) {
        main_ready();header(1,0xc0001000,512,0);semantic_finish();
        write_reg(SDIO_BLOCK_COUNT,16);
        CHECK(issue(0x501f5,0x9a000010)==SDIO_R5_COMMAND_STATE);
        write_reg(SDIO_DCTRL,0x10);uint64_t due=s.data.deadline;
        if(mode==0)sdio_bind_dma_reader(&s,main_dma,main_source);
        if(mode==1)sdio_host_reset(&s);
        if(mode==2)write_reg(SDIO_DCTRL,3);
        if(mode==3)CHECK(ioenable(0)==SDIO_R5_COMMAND_STATE);
        if(mode==4)CHECK(f1write(3,0)==SDIO_R5_COMMAND_STATE);
        if(mode==5){now--;sdio_step(&s,now);}
        now=due+10000;sdio_step(&s,now);
        CHECK(!s.data.active&&!s.data.armed&&!s.main_model.pending_length&&!s.main_model.copied);
        CHECK(s.main_model.phase==MAIN_BODY&&s.main_model.requested==512);
    }
    main_ready();header(1,0xc0001000,512,0);semantic_finish();
    write_reg(SDIO_BLOCK_COUNT,1);
    CHECK(issue(0x501f5,0x9a000001)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
    CHECK(!s.data.armed&&!s.main_model.pending_length); /* stale previous request */
    write_reg(SDIO_BLOCK_COUNT,16);main_mapped=false;
    CHECK(issue(0x501f5,0x9a000010)==SDIO_R5_COMMAND_STATE);
    CHECK(!sdio_write(&s,SDIO_DCTRL,0x10,4,now));
    CHECK(!s.data.active&&!s.main_model.pending_length&&!s.main_model.copied);
    main_ready();s.endpoint.used=1;s.endpoint.receive[0]=0x99;
    write_reg(SDIO_BLOCK_COUNT,1);
    CHECK(issue(0x501f5,0x9a000001)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
    CHECK(s.endpoint.used==1&&s.endpoint.receive[0]==0x99&&!s.main_model.pending_length);
    main_ready();memset(main_source,0,32);put_word(0,1);
    put_word(4,0xc0001000);put_word(8,512);
    CHECK(sdio_endpoint_receive(&s,config.io_port,main_source,32));
    CHECK(s.main_model.pending_length==32&&s.endpoint.used==0);
    sdio_host_reset(&s);memset(main_source,0,32);semantic_finish();
    CHECK(s.main_model.requested==512&&s.main_model.headers==1);

    /* No atomic low/high read fiction: reads can straddle publication. */
    main_ready();header(1,0xc0001000,512,0);
    s.main_model.deadline=now+150;
    unsigned low=f1read(0x10)&255,high=f1read(0x11)&255;
    CHECK(low==16&&high==2&&((high<<8)|low)==528&&s.main_model.requested==512);
    write_reg(SDIO_BLOCK_COUNT,17);
    CHECK(issue(0x501f5,0x9a000011)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
    CHECK(!s.data.armed&&!s.main_model.copied);
    body_pending(8);s.main_model.deadline=now+150;
    low=f1read(0x10)&255;high=f1read(0x11)&255;
    CHECK(low==0&&high==0&&s.main_model.requested==16&&s.main_model.copied==508);
    write_reg(SDIO_BLOCK_COUNT,0);
    CHECK(issue(0x501f5,0x9a000000)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
    CHECK(!s.data.armed&&s.main_model.bodies==1);
    /* Both bytes before/after the deadline yield an unchanged full request. */
    for(unsigned delta=1;delta<401;delta++) {
        main_ready();header(1,0xc0001000,512,0);s.main_model.deadline=now+delta;
        low=f1read(0x10)&255;high=f1read(0x11)&255;
        unsigned pair=(high<<8)|low;
        CHECK(pair==(delta<=100?512:delta<=200?528:16));
        CHECK(s.main_model.copied==0);
    }
    main_ready();header(1,0xc0001000,512,0);s.main_model.waiting_deadline=true;
    now=UINT64_MAX-10;sdio_step(&s,now);
    CHECK(s.main_model.phase==MAIN_STOPPED&&s.main_model.diagnostic==3&&s.main_model.pending_length==32);
    printf("SDIO main-first contracts: %u new, %u total checks, %u failures\n",checks-previous,checks,failures);
    return failures?1:0;
}
