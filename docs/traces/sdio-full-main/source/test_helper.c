#include "test_data.c"
static void helper_setup(unsigned capacity){
 config.helper_receiver=true;config.helper_capacity=(uint16_t)capacity;data_setup();
}
static void helper_frame(unsigned wire){
 write_reg(SDIO_BLOCK_COUNT,wire/32u);
 CHECK(issue(0x501f5,0x9a000000u|wire/32u)==SDIO_R5_COMMAND_STATE);
 CHECK(s.data.armed&&!s.data.active);write_reg(SDIO_DCTRL,0x10);
 uint16_t before=s.helper.used;uint64_t due=s.data.deadline;
 now=due-1;sdio_step(&s,now);CHECK(s.helper.used==before&&s.data.active);
 now=due;sdio_step(&s,now);CHECK(!s.data.active&&(s.events&0x20)&&s.irq_pending==1);
}
static void payload(unsigned n,uint8_t value){
 memset(memory,0xa5,64);memory[0]=(uint8_t)n;memory[1]=memory[2]=memory[3]=0;
 for(unsigned i=0;i<n&&i<60;i++)memory[4+i]=value+i;
}
int helper_contract_main(void){
 if(data_contract_main())return 1;
 unsigned previous=checks;
 /* Arbitrary content and exact capacity, independent of any golden firmware. */
 uint8_t arbitrary[2140];for(unsigned i=0;i<sizeof arbitrary;i++)arbitrary[i]=(uint8_t)((i*37u)^(i>>3));
 helper_setup(sizeof arbitrary);unsigned position=0;uint8_t last[64]={0};
 for(unsigned frame=0;frame<36;frame++){
  unsigned n=sizeof arbitrary-position;if(n>60)n=60;
  memory[0]=(uint8_t)n;memory[1]=memory[2]=memory[3]=0;memcpy(memory+4,arbitrary+position,n);
  if(frame==35)CHECK(!memcmp(memory+44,last+44,20));
  memcpy(last,memory,64);helper_frame(64);position+=n;
  CHECK(s.helper.used==position&&s.helper.frames==frame+1&&!s.helper.terminated&&!s.helper.framing_stalled);
  CHECK(!memcmp(s.helper.memory,arbitrary,position));CHECK(s.endpoint.used==0);
  for(unsigned j=position;j<sizeof s.helper.memory;j++)CHECK(s.helper.memory[j]==0);
  uint64_t copies=s.data_completions;for(unsigned j=0;j<100;j++){sdio_step(&s,now);CHECK(s.data_completions==copies&&s.helper.used==position);}
 }
 CHECK(position==2140&&!sdio_endpoint_can_receive(&s,config.io_port,64));CHECK(sdio_endpoint_can_receive(&s,config.io_port,32));
 memset(memory,0,32);helper_frame(32);CHECK(s.helper.terminated&&s.helper.frames==37&&s.helper.used==2140);
 CHECK(!memcmp(s.helper.memory,arbitrary,sizeof arbitrary));CHECK(s.data_starts==37&&s.data_completions==37&&!s.helper_protocol_errors);
 CHECK(!sdio_endpoint_can_receive(&s,config.io_port,64)&&!sdio_endpoint_can_receive(&s,config.io_port,32));
 CHECK(f1read(0x10)==SDIO_R5_COMMAND_STATE&&f1read(0x11)==SDIO_R5_COMMAND_STATE);
 CHECK(f1read(0x34)==SDIO_R5_COMMAND_STATE&&f1read(0x35)==SDIO_R5_COMMAND_STATE);
 CHECK(f1read(0x40)==SDIO_R5_COMMAND_STATE&&f1read(0x41)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x501f5,0x9a000001)&SDIO_R5_ERROR);CHECK(s.helper.frames==37&&s.data_completions==37);
 /* Valid early termination records only a short stream, no helper execution. */
 helper_setup(4096);memset(memory,0,32);helper_frame(32);
 CHECK(s.helper.terminated&&s.helper.used==0&&s.helper.frames==1);CHECK(f1read(0x10)==SDIO_R5_COMMAND_STATE);
 /* Malformed framing is separate from physical SDIO completion. */
 for(unsigned mode=0;mode<6;mode++){
  helper_setup(mode==5?10:4096);payload(60,7);unsigned wire=64;
  if(mode==0)memory[0]=61;
  if(mode==1)memset(memory,0xff,4);
  if(mode==2)memory[0]=0; /* unsupported64-byte terminator in this narrow Apple gate */
  if(mode==3){wire=32;memory[0]=1;}
  if(mode==4)memory[3]=0x80;
  uint8_t source[64];memcpy(source,memory,64);helper_frame(wire);
  CHECK(s.data_completions==1&&!s.data_errors&&!s.transport_errors);
  CHECK(s.helper_protocol_errors==1&&s.helper.framing_stalled&&!s.helper.terminated&&s.helper.used==0&&s.helper.frames==0);
  CHECK(s.endpoint.used==wire&&!memcmp(s.endpoint.receive,source,wire));
  for(unsigned i=0;i<sizeof s.helper.memory;i++)CHECK(s.helper.memory[i]==0);
  CHECK(!sdio_endpoint_can_receive(&s,config.io_port,32));
 }
 helper_setup(0);payload(1,3);CHECK(issue(0x501f5,0x9a000002)&SDIO_R5_ERROR);CHECK(!s.data.armed&&s.helper.used==0&&reads==0);
 /* Helper mode admits one frame only into an empty FIFO, even if configured larger. */
 for(unsigned cap=64;cap<=128;cap+=64){
  helper_setup(4096);s.endpoint.capacity=(uint16_t)cap;uint8_t held[32];memset(held,0x72,sizeof held);
  CHECK(sdio_endpoint_receive(&s,config.io_port,held,sizeof held));CHECK(s.endpoint.used==32);
  CHECK(!sdio_endpoint_can_receive(&s,config.io_port,32)&&!sdio_endpoint_can_receive(&s,config.io_port,64));
  payload(1,0x33);CHECK(issue(0x501f5,0x9a000002)&SDIO_R5_ERROR);
  CHECK(!s.data.armed&&s.helper.used==0&&s.endpoint.used==32&&!memcmp(s.endpoint.receive,held,32));
  CHECK(!sdio_endpoint_receive(&s,config.io_port,memory,32));
  now+=10000;sdio_step(&s,now);CHECK(s.endpoint.used==32&&!memcmp(s.endpoint.receive,held,32)&&s.data_completions==0);
 }
 /* Header length excludes nonzero transported padding from executable bytes. */
 helper_setup(4096);payload(1,0x55);helper_frame(64);CHECK(s.helper.used==1&&s.helper.memory[0]==0x55&&s.helper.memory[1]==0);
 /* Host/FIFO/function controls preserve committed card storage. */
 for(unsigned mode=0;mode<6;mode++){
  helper_setup(4096);payload(60,11);helper_frame(64);uint8_t stored[60];memcpy(stored,s.helper.memory,60);
  if(mode==0)sdio_bind_dma_reader(&s,NULL,NULL);
  if(mode==1)sdio_host_reset(&s);
  if(mode==2)write_reg(SDIO_DCTRL,3);
  if(mode==3)CHECK(ioenable(0)==SDIO_R5_COMMAND_STATE);
  if(mode==4)CHECK(f1write(3,0)==SDIO_R5_COMMAND_STATE);
  if(mode==5){now--;sdio_step(&s,now);}
  CHECK(s.helper.used==60&&s.helper.frames==1&&!memcmp(stored,s.helper.memory,60));
  now+=10000;sdio_step(&s,now);CHECK(s.helper.used==60&&s.data_completions==1);
 }
 /* True card reset clears receiver state, without implicit controller acknowledgement. */
 helper_setup(4096);payload(60,23);helper_frame(64);sdio_card_reset(&s);
 CHECK(s.helper.used==0&&s.helper.frames==0&&!s.helper.terminated&&!s.helper.framing_stalled);
 CHECK((s.events&0x20)&&s.irq_pending==1);for(unsigned i=0;i<sizeof s.helper.memory;i++)CHECK(s.helper.memory[i]==0);
 /* Cancellation during a second transfer must not append or replay it. */
 for(unsigned mode=0;mode<6;mode++){
  helper_setup(4096);payload(60,13);helper_frame(64);payload(60,91);
  write_reg(SDIO_BLOCK_COUNT,2);CHECK(issue(0x501f5,0x9a000002)==SDIO_R5_COMMAND_STATE);write_reg(SDIO_DCTRL,0x10);
  if(mode==0)sdio_bind_dma_reader(&s,dma_read,memory);
  if(mode==1)sdio_host_reset(&s);
  if(mode==2)write_reg(SDIO_DCTRL,3);
  if(mode==3)CHECK(ioenable(0)==SDIO_R5_COMMAND_STATE);
  if(mode==4)CHECK(f1write(3,0)==SDIO_R5_COMMAND_STATE);
  if(mode==5){now--;sdio_step(&s,now);}
  now+=10000;sdio_step(&s,now);CHECK(s.helper.used==60&&s.helper.frames==1&&s.data_completions==1);
  CHECK(!s.data.active&&!s.data.armed&&s.helper.memory[60]==0);
 }
 sdio_config_t bad=config;bad.helper_capacity=SDIO_HELPER_CAPACITY+1;CHECK(!sdio_init(&s,&bad));bad=config;bad.data_transfer=false;CHECK(!sdio_init(&s,&bad));
 printf("SDIO helper contracts: %u new, %u total checks, %u failures\n",checks-previous,checks,failures);
 return failures?1:0;
}
