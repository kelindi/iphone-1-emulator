#include "test_endpoint.c"
static uint8_t memory[128];static unsigned reads;static bool mapped;
static const uint8_t *dma_read(void *ctx,uint32_t address,size_t length){
 CHECK(ctx==memory);reads++;
 return mapped&&address>=0x1000u&&address-0x1000u<=sizeof memory&&length<=sizeof memory-(address-0x1000u)?memory+(address-0x1000u):NULL;
}
static void data_setup(void){
 config.data_transfer=true;config.data_ticks=500;boot_setup();ready_endpoint();
 s.card.block_size[1]=32;write_reg(SDIO_BUFFER,0x1000);write_reg(SDIO_BLOCK_LENGTH,32);write_reg(SDIO_BLOCK_COUNT,2);
 for(unsigned i=0;i<sizeof memory;i++)memory[i]=(uint8_t)(i*7+3);
 reads=0;mapped=true;
 sdio_bind_dma_reader(&s,dma_read,memory);
}
static void arm_data(void){CHECK(issue(0x501f5,0x9a000002)==SDIO_R5_COMMAND_STATE);CHECK(s.data.armed&&!s.data.active&&s.endpoint.used==0&&reads==0);}
static void begin_data(void){arm_data();write_reg(SDIO_DCTRL,0x10);CHECK(s.data.active&&!s.data.armed&&s.endpoint.used==0&&reads==1);}
static void empty(void){CHECK(s.endpoint.used==0);for(unsigned i=0;i<sizeof s.endpoint.receive;i++)CHECK(s.endpoint.receive[i]==0);}
int data_contract_main(void){
 if(endpoint_contract_main())return 1;
 unsigned previous=checks;
 data_setup();CHECK(!sdio_write(&s,SDIO_DCTRL,0x10,4,now));empty();
 arm_data();for(unsigned i=0;i<10000;i++){sdio_step(&s,now);CHECK(!s.data.active&&s.endpoint.used==0);}
 CHECK(!sdio_write(&s,SDIO_BUFFER,0x1004,4,now));CHECK(s.buffer_address==0x1000);
 write_reg(SDIO_DCTRL,0x10);CHECK(!sdio_write(&s,SDIO_DCTRL,0x10,4,now));
 uint64_t deadline=s.data.deadline;now=deadline-1;sdio_step(&s,now);empty();CHECK(s.data_completions==0&&!sdio_irq_level(&s));
 /* Staged DMA bytes are stable even if CPU subsequently rewrites source. */
 uint8_t expected[64];memcpy(expected,memory,64);memset(memory,0,64);
 write_reg(SDIO_IRQMASK,1);CHECK(!sdio_irq_level(&s));now=deadline;sdio_step(&s,now);
 CHECK(s.data_completions==1&&s.endpoint.used==64&&!s.data.active);CHECK(!memcmp(expected,s.endpoint.receive,64));
 CHECK((s.events&0x20)&&s.irq_pending==1&&sdio_irq_level(&s));
 for(unsigned i=0;i<10000;i++){sdio_step(&s,now);CHECK(s.data_completions==1&&s.endpoint.used==64);}
 CHECK(s.events&0x20);write_reg(SDIO_IRQMASK,0);CHECK(s.irq_pending==1&&!sdio_irq_level(&s));write_reg(SDIO_IRQMASK,1);CHECK(sdio_irq_level(&s));
 write_reg(SDIO_IRQ,1);CHECK(!sdio_irq_level(&s)&&(s.events&0x20));write_reg(SDIO_ACK,0x20);CHECK(!(s.events&0x20));
 CHECK(f1read(0x20)==(SDIO_R5_COMMAND_STATE|8u));CHECK(f1read(0x40)==SDIO_R5_COMMAND_STATE);CHECK(f1read(0x41)==SDIO_R5_COMMAND_STATE);
 /* Bad forms return card errors without arming, reading or writing RAM. */
 uint32_t invalid[]={0x1a000002,0x8a000002,0xaa000002,0x92000002,0x9e000002,0x9a000000,0x9a000001,0x9a0001ff,0x98000002};
 for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++){data_setup();uint32_t r=issue(0x501f5,invalid[i]);CHECK(r&(SDIO_R5_ERROR|SDIO_R5_FUNCTION));CHECK(!s.data.armed&&reads==0);empty();}
 for(unsigned i=0;i<7;i++){data_setup();switch(i){case 0:s.block_length=0;break;case 1:s.block_length=UINT32_MAX;break;case 2:s.block_count=UINT32_MAX;break;case 3:s.card.block_size[1]=64;break;case 4:s.buffer_address=0xfffffffcu;break;case 5:s.buffer_address=0x1001;break;case 6:s.endpoint.used=1;break;}uint16_t used=s.endpoint.used;CHECK(issue(0x501f5,0x9a000002)&SDIO_R5_ERROR);CHECK(!s.data.armed&&reads==0&&s.endpoint.used==used);}
 data_setup();s.buffer_address=0x107c;arm_data();CHECK(!sdio_write(&s,SDIO_DCTRL,0x10,4,now));empty();CHECK(!s.data.active&&reads==1);
 data_setup();arm_data();mapped=false;CHECK(!sdio_write(&s,SDIO_DCTRL,0x10,4,now));empty();
 data_setup();arm_data();s.dma_reader=NULL;CHECK(!sdio_write(&s,SDIO_DCTRL,0x10,4,now));empty();
 data_setup();arm_data();now=UINT64_MAX-100;CHECK(!sdio_write(&s,SDIO_DCTRL,0x10,4,now));CHECK(reads==0);empty();
 /* Reset and cancellation cannot produce a later completion. */
 for(unsigned phase=0;phase<2;phase++)for(unsigned mode=0;mode<7;mode++){
  data_setup();if(phase)begin_data();else arm_data();
  switch(mode){case 0:sdio_host_reset(&s);break;case 1:sdio_card_reset(&s);break;case 2:write_reg(SDIO_DCTRL,3);write_reg(SDIO_DCTRL,0);break;case 3:write_reg(SDIO_CTRL,0);break;case 4:CHECK(ioenable(0)==SDIO_R5_COMMAND_STATE);break;case 5:CHECK(f1write(3,0)==SDIO_R5_COMMAND_STATE);break;case 6:sdio_step(&s,now-1);break;}
  CHECK(!s.data.active&&!s.data.armed);now+=10000;sdio_step(&s,now);empty();CHECK(s.data_completions==0&&!(s.events&0x20)&&!(s.irq_pending&1));
 }
 data_setup();begin_data();s.endpoint.used=1;now=s.data.deadline;sdio_step(&s,now);CHECK(s.data_errors==1&&!s.data_completions&&s.endpoint.used==1&&!s.irq_pending);
 data_setup();arm_data();sdio_host_reset(&s);sdio_step(&s,now-1);now+=10000;sdio_step(&s,now);CHECK(!s.data.active&&!s.data.armed&&!s.endpoint.ready);empty();
 /* A completed controller event survives host callback/card-only changes.
  * Do not use issue(): its intentional ACK-all would erase the test witness. */
 for(unsigned mode=0;mode<6;mode++){
  data_setup();begin_data();now=s.data.deadline;sdio_step(&s,now);
  CHECK(s.data_completions==1&&s.endpoint.used==64&&(s.events&0x20)&&s.irq_pending==1);
  switch(mode){
   case 0:sdio_bind_dma_reader(&s,dma_read,memory);break;
   case 1:sdio_bind_dma_reader(&s,NULL,NULL);break;
   case 2:case 3:{uint32_t arg=mode==2?0x98000600u:0x88000400u;
    write_reg(SDIO_ARG,arg);write_reg(SDIO_CMD,0x80050074u);
    now+=config.command_ticks;sdio_step(&s,now);CHECK(read_reg(SDIO_RESP0)==SDIO_R5_COMMAND_STATE);break;}
   case 4:sdio_card_reset(&s);break;
   case 5:now--;sdio_step(&s,now);break;
  }
  CHECK((s.events&0x20)&&s.irq_pending==1&&sdio_irq_level(&s));
  now+=10000;sdio_step(&s,now);CHECK(s.data_completions==1&&(s.events&0x20)&&s.irq_pending==1);
  write_reg(SDIO_IRQMASK,0);CHECK(!sdio_irq_level(&s)&&s.irq_pending==1&&(s.events&0x20));
  write_reg(SDIO_IRQMASK,1);CHECK(sdio_irq_level(&s));
  write_reg(SDIO_IRQ,1);CHECK(!sdio_irq_level(&s)&&(s.events&0x20));
  write_reg(SDIO_ACK,0x20);CHECK(!(s.events&0x20));
 }
 /* Explicit controller disable/reset and FIFO reset discard their data latches. */
 for(unsigned mode=0;mode<3;mode++){
  data_setup();begin_data();now=s.data.deadline;sdio_step(&s,now);
  if(mode==0)sdio_host_reset(&s);else if(mode==1)write_reg(SDIO_CTRL,0);else write_reg(SDIO_DCTRL,3);
  CHECK(!(s.events&0x20)&&!(s.irq_pending&1)&&!sdio_irq_level(&s));
  now+=10000;sdio_step(&s,now);CHECK(s.data_completions==1);
 }
 sdio_config_t bad=config;bad.data_ticks=0;CHECK(!sdio_init(&s,&bad));bad=config;bad.boot_endpoint=false;CHECK(!sdio_init(&s,&bad));
 printf("SDIO data contracts: %u new, %u total checks, %u failures\n",checks-previous,checks,failures);
 return failures?1:0;
}
