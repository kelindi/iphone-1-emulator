#include "sdio.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
 fprintf(stderr,"FAIL line%u: %s\n",__LINE__,#x); } } while(0)
static sdio_t s;
static uint64_t now;
static sdio_config_t config = {
 .present=true,.io_functions=1,.assigned_rca=1,.voltage_mask=0x60000u,
 .readable_cccr=(1u<<0)|(1u<<8),.cccr={0},.command_ticks=100
};
static uint32_t read_reg(unsigned off) { return sdio_read(&s,off,4,now); }
static void write_reg(unsigned off,uint32_t v) { CHECK(sdio_write(&s,off,v,4,now)); }
static uint32_t issue(uint32_t descriptor,uint32_t arg) {
 write_reg(SDIO_ACK,0xffffffffu);write_reg(SDIO_ARG,arg);write_reg(SDIO_CMD,descriptor);
 CHECK(read_reg(SDIO_STATUS)&SDIO_READY);
 write_reg(SDIO_CMD,read_reg(SDIO_CMD)|SDIO_EXECUTE);
 now+=config.command_ticks;sdio_step(&s,now);return read_reg(SDIO_RESP0);
}
static void setup(void) {
 CHECK(sdio_init(&s,&config));now=0;
 /* Exact observed Apple cold-init MMIO prefix. */
 write_reg(SDIO_CLKDIV,4);write_reg(SDIO_DCTRL,3);write_reg(SDIO_DCTRL,0);
 write_reg(SDIO_CTRL,1);write_reg(SDIO_ACK,0xffffffffu);
 write_reg(SDIO_IRQ,0xffffffffu);write_reg(SDIO_IRQMASK,3);write_reg(SDIO_CSR,2);
}
static void select_card(void) {
 CHECK(issue(0x40045u,0)==0x10060000u);CHECK(s.card.state==CARD_IDLE);
 CHECK(issue(0x40045u,0x60000u)==0x90060000u);CHECK(s.card.state==CARD_READY);
 CHECK(issue(0x60043u,0)==0x10000u);CHECK(s.card.state==CARD_STANDBY);
 CHECK(issue(0x10047u,0x10000u)==0x1e00u);CHECK(s.card.state==CARD_SELECTED);
}
int main(void) {
 CHECK(sdio_init(&s,&config));CHECK(read_reg(SDIO_STATUS)==0);
 CHECK(!sdio_write(&s,SDIO_CMD,SDIO_EXECUTE|0x40045u,4,now));
 CHECK(s.triggers==0&&s.card.state==CARD_IDLE);
 setup();
 write_reg(SDIO_ARG,0);write_reg(SDIO_CMD,0x40045u);
 CHECK(s.triggers==0&&read_reg(SDIO_RESP0)==0);
 for(unsigned i=0;i<100001;i++) CHECK(read_reg(SDIO_STATUS)==SDIO_READY);
 CHECK(s.triggers==0&&s.completions==0);
 write_reg(SDIO_CMD,SDIO_EXECUTE|0x40045u);
 CHECK(s.triggers==1&&s.busy&&s.card.state==CARD_IDLE);
 for(unsigned i=0;i<1000;i++) CHECK(read_reg(SDIO_STATUS)==0);
 CHECK(read_reg(SDIO_STATE)==0x10u&&read_reg(SDIO_RESP0)==0);
 CHECK(!sdio_write(&s,SDIO_CMD,SDIO_EXECUTE|0x40045u,4,now));
 CHECK(!sdio_write(&s,SDIO_ARG,0x60000u,4,now));
 now=99;CHECK(read_reg(SDIO_STATUS)==0&&s.busy);
 now=100;CHECK(read_reg(SDIO_STATUS)==(SDIO_READY|SDIO_SENT|SDIO_DONE));
 CHECK(read_reg(SDIO_RESP0)==0x10060000u&&!s.busy&&s.completions==1);
 CHECK(s.card.state==CARD_IDLE&&s.triggers==1);
 now=10000;sdio_step(&s,now);CHECK(s.completions==1);
 /* A new operation does not acknowledge an earlier latched event. */
 write_reg(SDIO_CMD,SDIO_EXECUTE|0x40045u);
 CHECK(read_reg(SDIO_STATUS)==(SDIO_SENT|SDIO_DONE));
 CHECK(read_reg(SDIO_RESP0)==0x10060000u);
 write_reg(SDIO_ACK,SDIO_DONE);CHECK(read_reg(SDIO_STATUS)==SDIO_SENT);
 now+=100;sdio_step(&s,now);CHECK(s.completions==2);
 write_reg(SDIO_ACK,SDIO_DONE);CHECK(read_reg(SDIO_STATUS)==(SDIO_READY|SDIO_SENT));
 write_reg(SDIO_ACK,0xffffffffu);CHECK(read_reg(SDIO_STATUS)==SDIO_READY);
 CHECK(read_reg(SDIO_RESP0)==0x10060000u);CHECK(!sdio_irq_level(&s));
 write_reg(SDIO_IRQMASK,0xffffffffu);CHECK(!sdio_irq_level(&s));
 CHECK(read_reg(SDIO_IRQ)==0);
 /* Width, alignment and unknown-response writes cannot trigger anything. */
 uint64_t triggers=s.triggers;
 CHECK(!sdio_write(&s,SDIO_CMD,SDIO_EXECUTE|0x40045u,1,now));
 CHECK(!sdio_write(&s,SDIO_CMD+1,SDIO_EXECUTE|0x40045u,4,now));
 CHECK(!sdio_write(&s,SDIO_RESP0,0xffffffffu,4,now));CHECK(s.triggers==triggers);
 CHECK(sdio_read(&s,SDIO_STATUS,1,now)==0);
 setup();CHECK(issue(0x60043u,0)==0);CHECK(read_reg(SDIO_STATUS)&SDIO_RESPONSE_ERROR);
 CHECK(s.card.state==CARD_IDLE);
 CHECK(issue(0x40045u,0x80u)==0);CHECK(read_reg(SDIO_STATUS)&SDIO_RESPONSE_ERROR);
 CHECK(s.card.state==CARD_IDLE);
 setup();select_card();
 CHECK(issue(0x50074u,0)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x50074u,8u<<9)==SDIO_R5_COMMAND_STATE);
 CHECK(s.cccr_reads==2&&s.cccr_read_mask==0x101u);
 CHECK(issue(0x50074u,9u<<9)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ADDRESS));
 CHECK(issue(0x50074u,3u<<9)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ADDRESS));
 CHECK(issue(0x50074u,1u<<28)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(issue(0x50074u,2u<<28)==(SDIO_R5_COMMAND_STATE|SDIO_R5_FUNCTION));
 CHECK(issue(0x50074u,0x80000002u)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(issue(0x50074u,0x04000000u)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(s.cccr_reads==2&&s.config.cccr[0]==0);
 CHECK(issue(0x500f5u,0)==0);CHECK(read_reg(SDIO_STATUS)&SDIO_RESPONSE_ERROR);
 CHECK(!(read_reg(SDIO_STATUS)&SDIO_DONE));CHECK(s.card.state==CARD_SELECTED);
 CHECK(issue(0x10047u,0x20000u)==0);CHECK(s.card.state==CARD_STANDBY);
 CHECK(read_reg(SDIO_STATUS)&SDIO_RESPONSE_ERROR);
 CHECK(issue(0x50074u,0)==0);CHECK(read_reg(SDIO_STATUS)&SDIO_RESPONSE_ERROR);
 CHECK(issue(0x10047u,0x10000u)==0x1e00u);CHECK(s.card.state==CARD_SELECTED);
 /* Host reset aborts a command while preserving independent card state. */
 write_reg(SDIO_CMD,SDIO_EXECUTE|0x50074u);sdio_host_reset(&s);
 now+=1000;CHECK(read_reg(SDIO_STATUS)==0&&read_reg(SDIO_RESP0)==0);
 CHECK(!s.busy&&s.card.state==CARD_SELECTED);
 write_reg(SDIO_CTRL,1);CHECK(issue(0x50074u,0)==SDIO_R5_COMMAND_STATE);
 /* Card power reset cancels pending work and selection together. */
 write_reg(SDIO_CMD,SDIO_EXECUTE|0x50074u);sdio_card_reset(&s);
 now+=1000;CHECK(read_reg(SDIO_STATUS)==SDIO_READY&&read_reg(SDIO_RESP0)==0);
 CHECK(s.card.state==CARD_IDLE&&s.card.rca==0);
 setup();write_reg(SDIO_ARG,0x60000u);write_reg(SDIO_CMD,SDIO_EXECUTE|0x40045u);
 write_reg(SDIO_CTRL,0);now+=1000;CHECK(!s.busy&&s.card.state==CARD_IDLE);
 CHECK(read_reg(SDIO_STATUS)==0);
 /* Time rollback is a discontinuity, not wrapped future completion. */
 setup();now=1000;write_reg(SDIO_CMD,SDIO_EXECUTE|0x40045u);
 now=999;sdio_step(&s,now);CHECK(!s.busy&&s.completions==0);
 now=10000;sdio_step(&s,now);CHECK(s.completions==0);
 setup();now=UINT64_MAX-50;CHECK(!sdio_write(&s,SDIO_CMD,SDIO_EXECUTE|0x40045u,4,now));
 CHECK(!s.busy&&s.triggers==0);
 setup();s.config.present=false;CHECK(issue(0x40045u,0)==0);
 CHECK(read_reg(SDIO_STATUS)&SDIO_RESPONSE_ERROR);CHECK(!s.completions);
 /* Additional explicit enumeration map; frozen first-gate contracts above. */
 config.enumeration=true;setup();select_card();
 write_reg(SDIO_CTRL,5);CHECK(read_reg(SDIO_CTRL)==5u);
 CHECK(!sdio_write(&s,SDIO_CTRL,9,4,now));CHECK(read_reg(SDIO_CTRL)==5u);
 CHECK(issue(0x50074,9u<<9)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x50074,10u<<9)==(SDIO_R5_COMMAND_STATE|0x12u));
 CHECK(issue(0x50074,11u<<9)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x50074,0x100u<<9)==(SDIO_R5_COMMAND_STATE|7u));
 for(unsigned f=2;f<8;f++) CHECK(issue(0x50074,(f*0x100u)<<9)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x50074,0x88000000u|(7u<<9)|0x82u)==(SDIO_R5_COMMAND_STATE|0x82u));
 CHECK(issue(0x50074,7u<<9)==(SDIO_R5_COMMAND_STATE|0x82u));
 CHECK(issue(0x50074,0x88000000u|(7u<<9)|0x83u)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(s.card.bus_interface==0x82u);
 CHECK(issue(0x50074,0x88000000u|(4u<<9)|3u)==(SDIO_R5_COMMAND_STATE|3u));
 CHECK(issue(0x50074,0x88000000u|(4u<<9)|7u)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(s.card.interrupt_enable==3u&&!sdio_irq_level(&s));
 CHECK(issue(0x50074,0x88000000u|(0x10u<<9)|4u)==(SDIO_R5_COMMAND_STATE|4u));
 CHECK(issue(0x50074,0x88000000u|(0x110u<<9)|0u)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x50074,0x88000000u|(0x111u<<9)|2u)==(SDIO_R5_COMMAND_STATE|2u));
 CHECK(s.card.block_size[0]==4u&&s.card.block_size[1]==512u);
 CHECK(issue(0x50074,0x1200u<<9)==(SDIO_R5_COMMAND_STATE|0x15u));
 CHECK(issue(0x50074,0x88000000u|(0x1200u<<9)|0x42u)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ERROR));
 CHECK(issue(0x50074,0x1200u<<9)==(SDIO_R5_COMMAND_STATE|0x15u));
 /* Parse the declared common chain independently through CMD52. */
 unsigned cis=0x1200u,tuples=0;bool manufacturer=false,extension=false;
 for(unsigned guard=0;guard<8;guard++){
  unsigned code=issue(0x50074,cis++<<9)&0xffu;if(code==0xffu)break;
  unsigned len=issue(0x50074,cis++<<9)&0xffu;CHECK(len>0&&len<32);
  unsigned bytes[32]={0};for(unsigned j=0;j<len&&j<32;j++)bytes[j]=issue(0x50074,cis++<<9)&0xffu;
  if(code==0x20u){CHECK(len==4);CHECK((bytes[0]|(bytes[1]<<8))==0x02dfu);CHECK((bytes[2]|(bytes[3]<<8))==0x9103u);manufacturer=true;}
  if(code==0x22u){CHECK(len==4&&bytes[0]==0);CHECK((bytes[1]|(bytes[2]<<8))==512u&&bytes[3]==0x32u);extension=true;}
  tuples++;
 }
 CHECK(tuples==4&&manufacturer&&extension&&cis==0x1219u);
 CHECK(issue(0x50074,0x1219u<<9)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ADDRESS));
 CHECK(issue(0x50074,0x1300u<<9)==(SDIO_R5_COMMAND_STATE|SDIO_R5_ADDRESS));
 CHECK(issue(0x50074,2u<<9)==SDIO_R5_COMMAND_STATE);
 CHECK(issue(0x50074,3u<<9)==SDIO_R5_COMMAND_STATE);
 sdio_host_reset(&s);CHECK(s.card.bus_interface==0x82u&&s.card.block_size[0]==4u);
 sdio_card_reset(&s);CHECK(s.card.interrupt_enable==0&&s.card.bus_interface==0&&s.card.block_size[1]==0);
 printf("SDIO isolated contracts: %u checks, %u failures\n",checks,failures);
 return failures?1:0;
}
