#define main old_contract_main
#include "test_sdio.c"
#undef main
static uint32_t f1read(unsigned address){return issue(0x50074u,0x10000000u|(address<<9));}
static uint32_t f1write(unsigned address,unsigned value){return issue(0x50074u,0x98000000u|(address<<9)|value);}
static uint32_t ioenable(unsigned value){return issue(0x50074u,0x88000000u|(2u<<9)|value);}
static void boot_setup(void){
 config.enumeration=true;config.boot_endpoint=true;config.endpoint_capacity=64;
 config.io_port=0x10000;config.function_init_ticks=300;setup();select_card();
}
static void ready_endpoint(void){
 CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2u));
 now+=config.function_init_ticks;sdio_step(&s,now);CHECK(s.endpoint.ready);
 CHECK(f1write(3,2)==(SDIO_R5_COMMAND_STATE|2u));
}
int endpoint_contract_main(void){
 if(old_contract_main())return 1;
 unsigned previous=checks;
 boot_setup();CHECK(!s.endpoint.initialized&&!s.endpoint.ready);
 CHECK(f1read(0)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2u));
 CHECK(s.endpoint.initialized&&s.endpoint.enabled&&s.endpoint.initializing&&!s.endpoint.ready);
 CHECK(s.endpoint.capacity==64&&s.endpoint.used==0);
 for(unsigned i=0;i<sizeof s.endpoint.receive;i++)CHECK(s.endpoint.receive[i]==0);
 uint64_t deadline=s.endpoint.ready_deadline;
 for(unsigned i=0;i<10000;i++){sdio_step(&s,now);CHECK(!s.endpoint.ready);}
 CHECK(issue(0x50074,3u<<9)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x50074,3u<<9)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x50074,3u<<9)==(SDIO_R5_COMMAND_STATE|2u));
 CHECK(now==deadline&&s.endpoint.ready&&!s.endpoint.initializing);
 CHECK(s.iorx_wait_reads==2&&s.iorx_ready_reads==1);
 CHECK(f1read(0)==SDIO_R5_COMMAND_STATE);
 CHECK(f1read(1)==SDIO_R5_COMMAND_STATE);
 CHECK(f1read(2)==(SDIO_R5_COMMAND_STATE|1u));
 CHECK(f1read(0x20)==(SDIO_R5_COMMAND_STATE|8u));
 CHECK(!sdio_endpoint_can_receive(&s,config.io_port,64));
 CHECK(f1write(3,2)==(SDIO_R5_COMMAND_STATE|2u));
 CHECK(f1read(0x20)==(SDIO_R5_COMMAND_STATE|9u));
 CHECK(sdio_endpoint_can_receive(&s,config.io_port,64));
 CHECK(!sdio_endpoint_can_receive(&s,config.io_port+1,64));
 CHECK(!sdio_endpoint_can_receive(&s,config.io_port,0));
 CHECK(!sdio_endpoint_can_receive(&s,config.io_port,65));
 CHECK(!sdio_endpoint_can_receive(&s,config.io_port,SIZE_MAX));
 uint8_t payload[64],before[SDIO_ENDPOINT_CAPACITY];for(unsigned i=0;i<64;i++)payload[i]=(uint8_t)(i^0xa5);
 memcpy(before,s.endpoint.receive,sizeof before);
 CHECK(!sdio_endpoint_receive(&s,config.io_port,NULL,64));CHECK(s.endpoint.used==0);
 CHECK(!sdio_endpoint_receive(&s,config.io_port,payload,65));CHECK(!memcmp(before,s.endpoint.receive,sizeof before));
 CHECK(sdio_endpoint_receive(&s,config.io_port,payload,64));CHECK(s.endpoint.used==64);
 CHECK(!memcmp(s.endpoint.receive,payload,64));
 CHECK(!sdio_endpoint_receive(&s,config.io_port,payload,1));CHECK(s.endpoint.used==64);
 CHECK(f1read(0x20)==(SDIO_R5_COMMAND_STATE|8u));
 CHECK(f1read(0x34)==SDIO_R5_COMMAND_STATE&&f1read(0x35)==SDIO_R5_COMMAND_STATE);
 CHECK(f1read(0x40)==SDIO_R5_COMMAND_STATE&&f1read(0x41)==SDIO_R5_COMMAND_STATE);
 CHECK(f1read(0x10)==SDIO_R5_COMMAND_STATE&&f1read(0x11)==SDIO_R5_COMMAND_STATE);
 CHECK(f1read(0x12)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ADDRESS));
 CHECK(f1write(0x20,0xff)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(f1write(3,4)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));CHECK(s.endpoint.host_power);
 CHECK(f1write(4,3)==(SDIO_R5_COMMAND_STATE|3u));CHECK(!sdio_irq_level(&s));
 CHECK(f1write(4,0x10)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));CHECK(s.endpoint.interrupt_mask==3);
 CHECK(f1write(3,0)==SDIO_R5_COMMAND_STATE);CHECK(s.endpoint.used==0&&!s.endpoint.host_power);
 for(unsigned i=0;i<sizeof s.endpoint.receive;i++)CHECK(s.endpoint.receive[i]==0);
 CHECK(ioenable(0)==SDIO_R5_COMMAND_STATE);CHECK(!s.endpoint.initialized&&!s.endpoint.ready);
 /* Disable cancels initialization and future time cannot resurrect it. */
 CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2));CHECK(ioenable(0)==SDIO_R5_COMMAND_STATE);
 now+=10000;sdio_step(&s,now);CHECK(!s.endpoint.ready&&!s.endpoint.enabled);
 /* Card reset clears the function; controller reset preserves it. */
 CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2));sdio_card_reset(&s);now+=10000;sdio_step(&s,now);
 CHECK(!s.endpoint.ready&&!s.endpoint.enabled&&s.card.state==CARD_IDLE);
 boot_setup();CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2));uint64_t complete=s.completions;
 sdio_host_reset(&s);CHECK(s.endpoint.initializing&&s.card.state==CARD_SELECTED);
 now+=300;sdio_step(&s,now);CHECK(s.endpoint.ready&&s.completions==complete&&s.control==0);
 write_reg(SDIO_CTRL,1);CHECK(issue(0x50074,3u<<9)==(SDIO_R5_COMMAND_STATE|2));
 /* A time rollback invalidates unfinished endpoint initialization. */
 boot_setup();CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2));sdio_step(&s,now-1);
 now+=10000;sdio_step(&s,now);CHECK(!s.endpoint.ready&&!s.endpoint.enabled);
 /* Controller reset cannot hide a rollback from the independent card. */
 boot_setup();CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|2));sdio_host_reset(&s);
 CHECK(s.have_time&&s.endpoint.initializing);sdio_step(&s,now-1);
 CHECK(!s.endpoint.enabled&&!s.endpoint.initializing&&!s.endpoint.ready);
 now+=10000;sdio_step(&s,now);CHECK(!s.endpoint.ready&&s.card.state==CARD_SELECTED);
 boot_setup();ready_endpoint();sdio_host_reset(&s);CHECK(s.have_time&&s.endpoint.ready);
 sdio_step(&s,now-1);CHECK(!s.endpoint.ready&&!s.endpoint.enabled);
 now+=10000;sdio_step(&s,now);CHECK(!s.endpoint.ready&&s.card.state==CARD_SELECTED);
 /* No storage / impossible deadline cannot enable the function. */
 boot_setup();s.config.endpoint_capacity=0;CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(!s.endpoint.enabled&&!s.endpoint.initialized);
 boot_setup();now=UINT64_MAX-200;CHECK(ioenable(2)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(!s.endpoint.enabled&&!s.endpoint.initializing);
 boot_setup();CHECK(ioenable(4)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));CHECK(!s.endpoint.enabled);
 sdio_config_t bad=config;bad.endpoint_capacity=SDIO_ENDPOINT_CAPACITY+1;CHECK(!sdio_init(&s,&bad));
 bad=config;bad.endpoint_capacity=1;CHECK(!sdio_init(&s,&bad));
 bad=config;bad.io_port=0x20000;CHECK(!sdio_init(&s,&bad));
 bad=config;bad.function_init_ticks=0;CHECK(!sdio_init(&s,&bad));
 /* Bounds remain checked even if derived endpoint state is damaged. */
 boot_setup();ready_endpoint();s.endpoint.used=65;CHECK(!sdio_endpoint_can_receive(&s,config.io_port,1));
 s.endpoint.used=0;s.endpoint.capacity=SDIO_ENDPOINT_CAPACITY+1;CHECK(!sdio_endpoint_can_receive(&s,config.io_port,1));
 /* Data registers latch, but CMD53 still cannot fabricate any completion. */
 boot_setup();ready_endpoint();write_reg(SDIO_BUFFER,0x08004000);write_reg(SDIO_BLOCK_LENGTH,32);write_reg(SDIO_BLOCK_COUNT,2);
 CHECK(read_reg(SDIO_BUFFER)==0x08004000&&read_reg(SDIO_BLOCK_LENGTH)==32&&read_reg(SDIO_BLOCK_COUNT)==2);
 complete=s.completions;CHECK(issue(0x500f5,0x9a000002)==0);CHECK(s.completions==complete);
 CHECK((read_reg(SDIO_STATUS)&SDIO_RESPONSE_ERROR)&&!(read_reg(SDIO_STATUS)&SDIO_DONE));
 CHECK(s.endpoint.used==0&&s.endpoint.ready);
 printf("SDIO endpoint contracts: %u new, %u total checks, %u failures\n",checks-previous,checks,failures);
 return failures?1:0;
}
