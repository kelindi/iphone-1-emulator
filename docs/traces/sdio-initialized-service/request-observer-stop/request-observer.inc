#include "request.h"
#include "peek.h"
static request_gate_t request;
static bool request_bad,positive_mode;
static uint64_t request_count,prefix_transactions,prefix_instructions;
static uint32_t previous_pc,previous_cpsr,last_count;
static bool last_ok;
static uint32_t driver_address,pulldowns,frame_object,frame_virtual,frame_physical,frame_capacity,frame_offset,frame_logical;
static uint32_t setup_cccr,setup_card_mask,setup_controller_mask,setup_card_block,host_block_object;
static bool host_block_setter,host_block_committed;
static uint64_t host_block_cycles;
static uint8_t trigger_virtual[320];
typedef struct {bool entered,finished;uint32_t driver,args[3],sequence,object,buffer;uint64_t cycles;} builder_t;
static builder_t builders[2];
static bool peek32(checkpoint_phone_t*p,uint32_t va,uint32_t*out){uint8_t b[4];if(!request_peek(p,va,b,4))return false;*out=le(b);return true;}
static bool peek32at(checkpoint_phone_t*p,uint32_t base,uint32_t offset,uint32_t*out){return base<=UINT32_MAX-offset&&peek32(p,base+offset,out);}
static void request_prefix(checkpoint_phone_t*p,uint64_t instructions,bool valid){
 if(request.prefix||!valid){request_bad=true;return;}
 request.prefix=true;request.prefix_cycles=p->machine->cpu.cycles;prefix_transactions=transactions;prefix_instructions=instructions;
 fprintf(milestones,"exact077 prefix pc=c0380208 cycles=%"PRIu64" transactions=%"PRIu64" instructions=%"PRIu64"\n",request.prefix_cycles,prefix_transactions,instructions);
}
static void request_bus(checkpoint_phone_t*p,bool write,uint32_t address,uint32_t value,unsigned width,bool accepted){
 if(!positive_mode)return;sdio_t*s=&p->device;arm_cpu_t*c=&p->machine->cpu;
 if(!write&&address==SDIO_BASE+SDIO_RESP0&&(s->command&63u)==52u&&s->argument==0x10000c00u){
  if(request.reg6_error||!request.prefix||!accepted||width!=4||value!=0x1100u||c->r[15]!=0xc0431594u||!gate_arm_svc(c->cpsr)){request_bad=true;return;}
  request.reg6_error=true;request.reg6_value=value;request.reg6_cycles=c->cycles;
 }
 if(!write||address!=SDIO_BASE+SDIO_CMD||!(value&SDIO_EXECUTE)||(value&63u)!=53u)return;
 request_count++;request.trigger=true;request.trigger_cycles=c->cycles;request.trigger_pc=c->r[15];request.trigger_cpsr=c->cpsr;
 request.argument=s->argument;request.descriptor=value&~SDIO_EXECUTE;request.source=s->buffer_address;request.block_length=s->block_length;request.block_count=s->block_count;request.card_block_size=s->card.block_size[1];
 request.events=s->events;request.irq_pending=s->irq_pending;request.completions=s->completions;request.data_completions=s->data_completions;request.data_starts=s->data_starts;
 if(!accepted||width!=4||request_count!=1||!request.frame||!request_fields(request.argument,request.descriptor,request.source,request.block_length,request.block_count,request.card_block_size)||request.source!=frame_physical){request_bad=true;return;}
 /* A source witness is not a DMA transfer. This callback only reads the full
  * existing physical RAM mapping; it never invokes a card receive operation. */
 const uint8_t*source=p->underlying.host_ram?p->underlying.host_ram(p->underlying.ctx,request.source,320):NULL;
 if(!source||!request_peek(p,frame_virtual,trigger_virtual,320)){request_bad=true;return;}
 memcpy(request.physical_bytes,source,320);
 if(memcmp(trigger_virtual,request.virtual_bytes,320)||memcmp(source,trigger_virtual,320))request_bad=true;
 fprintf(milestones,"runtime request CALLBACK only pc=%08x cycles=%"PRIu64" arg=%08x descriptor=%08x source=%08x length=%u count=%u; retirement still required\n",c->r[15],c->cycles,request.argument,request.descriptor,request.source,request.block_length,request.block_count);
}
static void request_witness(checkpoint_phone_t*p){
 if(!positive_mode||!request.prefix)return;arm_cpu_t*c=&p->machine->cpu;if(!gate_arm_svc(c->cpsr))return;uint32_t pc=c->r[15];
 if(pc==0xc0380220u){if(!request.reg6_error||request.reg6_return){request_bad=true;return;}request.reg6_return=true;request.ignored_return=c->r[0];request.return_cycles=c->cycles;fprintf(milestones,"original register6 wrapper return at c0380220 r0=%08x cycles=%"PRIu64" (recorded, not assumed false)\n",c->r[0],c->cycles);}
 if(pc==0xc0380224u){if(!request.reg6_return||c->r[0]!=1){request_bad=true;return;}request.reg6_overwrite=true;request.overwrite_cycles=c->cycles;}
 if(pc==0xc038aab8u){if(!request.reg6_overwrite||c->r[0]!=1){request_bad=true;return;}request.verify_return=true;request.verify_cycles=c->cycles;}
 if(pc==0xc037a1c4u&&c->r[14]==0xc038abecu){if(host_block_setter||c->r[1]!=320){request_bad=true;return;}host_block_setter=true;host_block_object=c->r[0];}
 if(pc==0xc037a1c8u&&host_block_setter&&c->r[0]==host_block_object&&c->r[14]==0xc038abecu){uint8_t b[2];if(host_block_committed||host_block_object>UINT32_MAX-0x1bu||!request_peek(p,host_block_object+0x1a,b,2)||request_le16(b)!=320){request_bad=true;return;}host_block_committed=true;host_block_cycles=c->cycles;fprintf(milestones,"original host block-size setter committed pc=c037a1c8 object=%08x blockSize=320 cycles=%"PRIu64"\n",host_block_object,c->cycles);}
 if(pc==0xc038ac20u){
  uint32_t interface=0,command=0;if(!request.verify_return||request.setup||!host_block_committed||!peek32at(p,c->r[4],0x2e0,&interface)||!peek32at(p,interface,0x60,&command)||command!=host_block_object||!peek32(p,0xc03b1694u,&pulldowns)){request_bad=true;return;}
  driver_address=c->r[4];request.setup=true;request.setup_cycles=c->cycles;setup_cccr=p->device.card.interrupt_enable;setup_card_mask=p->device.endpoint.interrupt_mask;setup_controller_mask=p->device.irq_mask;setup_card_block=p->device.card.block_size[1];
  if(setup_card_mask!=3)request_bad=true;
  fprintf(milestones,"original runtime setup pc=c038ac20 driver=%08x pulldowns=%u blockSize=%u CCCRmask=%x cardmask=%x controllerMask=%x cycles=%"PRIu64"\n",driver_address,pulldowns,p->device.card.block_size[1],setup_cccr,setup_card_mask,setup_controller_mask,c->cycles);
 }
 if(pc==0xc0384dd0u||pc==0xc0386128u){
  unsigned index=pc==0xc0384dd0u?0:1;builder_t*b=&builders[index];uint8_t seq[2];
  if(!request.setup||b->entered||c->r[0]!=driver_address||driver_address>UINT32_MAX-0x316u||!request_peek(p,driver_address+0x314,seq,2)){request_bad=true;return;}
  *b=(builder_t){.entered=true,.driver=c->r[0],.args={c->r[1],c->r[2],c->r[3]},.sequence=request_le16(seq),.cycles=c->cycles};
  if(index==0&&(!pulldowns||c->r[1]!=1||c->r[2]!=65535||c->r[3]!=0))request_bad=true;
  if(index==1&&c->r[1]!=6)request_bad=true;
  fprintf(milestones,"original builder pc=%08x opcode=%04x args=%08x,%08x,%08x sequence=%u cycles=%"PRIu64"\n",pc,index?0x43:0x93,b->args[0],b->args[1],b->args[2],b->sequence,c->cycles);
 }
 if(pc==0xc0384ec0u||pc==0xc038620cu){unsigned index=pc==0xc0384ec0u?0:1;builder_t*b=&builders[index];if(!b->entered||b->finished||c->r[4]<4){request_bad=true;return;}b->finished=true;b->object=c->r[index?6:5];b->buffer=c->r[4]-4;}
 if(pc==0xc0380804u){
  if(request.frame||!request.setup){request_bad=true;return;}builder_t*b=NULL;unsigned index=0;
  for(unsigned i=0;i<2;i++)if(builders[i].finished&&builders[i].object==c->r[6]){if(b){request_bad=true;return;}b=&builders[i];index=i;}
  if(!b){request_bad=true;return;}uint8_t object[40];if(!request_peek(p,c->r[6],object,sizeof object)){request_bad=true;return;}
  frame_object=c->r[6];frame_virtual=c->r[5];frame_capacity=le(object+12);frame_offset=le(object+28);frame_physical=le(object+16);frame_logical=le(object+36);
  request.opcode=index?0x43:0x93;request.sequence=b->sequence;request.builder=true;request.builder_cycles=b->cycles;
  if(frame_virtual!=b->buffer||frame_virtual!=le(object+8)||frame_offset!=0||frame_capacity<320||le(object+32)!=320||frame_logical!=18||object[24]!=1||!request_peek(p,frame_virtual,request.virtual_bytes,320)||!request_packet(request.virtual_bytes,320,request.opcode,request.sequence)){request_bad=true;return;}
  request.frame=true;request.frame_cycles=c->cycles;
  fprintf(milestones,"original framed packet pc=c0380804 object=%08x virtual=%08x physical=%08x capacity=%u offset=%u logical=%u physicalLength=320 sequence=%u opcode=%04x cycles=%"PRIu64"\n",frame_object,frame_virtual,frame_physical,frame_capacity,frame_offset,frame_logical,request.sequence,request.opcode,c->cycles);
 }
}
static bool request_final(checkpoint_phone_t*p,const runtime_t*r){sdio_t*s=&p->device;arm_cpu_t*c=&p->machine->cpu;
 bool slots=r->slots[0].phase==SLOT_EMPTY&&r->slots[1].phase==SLOT_EMPTY&&!r->slots[0].used&&!r->slots[1].used&&runtime_ready(r);
 return !request_bad&&request_count==1&&request_retired(&request,previous_pc,previous_cpsr,last_count,last_ok,c->r[15],c->cpsr,c->cycles,s->busy,p->machine->timer.ticks,s->deadline,s->completions,s->data_completions,s->data_starts,s->data.armed,s->data.active,slots,s->events,s->irq_pending)&&s->pending_command==request.descriptor&&s->pending_argument==request.argument;
}
