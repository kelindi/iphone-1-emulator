/* Disposable native functional probe. Unsupported requests stop before their
 * device mutation. An accepted quiesce is an explicitly labeled earlier stop.
 * First publication is frozen before continuation; final owner release stops
 * after retirement and before any further guest instruction or engine service.
 * Neither boundary is saved/resumable: isolated device/decoder state is absent
 * from the production snapshot format. */
#include "emu_api.c"
#include "amc_sram.h"
#include "amc_idle.h"
#include "amc_engine.h"
#include "consume.h"
#include "peek.h"
#include "capture.h"
#include "admission.h"
#include "ordering.h"
#include "later.h"
#include <setjmp.h>
static arm_bus_t original;
static amc_sram_t sram, expected;
static amc_idle_t idle;
static amc_engine engine;
static uint8_t policy_sram[AMC_SRAM_SIZE], policy_written[AMC_SRAM_SIZE];
static const amc_program_policy policy={policy_sram,policy_written,AMC_SRAM_SIZE};
static amc_physical_ram physical;
static bool engine_started, touch_released, in_guest, layout_seen;
static uint32_t layout_pointers[4];
static uint64_t first_input_cycles, second_input_cycles, third_input_cycles, start_cycles, service_calls;
static unsigned publication_buffer;
static consume_observer consumed;
typedef struct {uint32_t address,width,value,pc,cpsr,owner;uint64_t cycles;} read_record;
_Static_assert(sizeof(read_record)==32,"fixed read-ledger ABI");
static read_record read_ledger[CONSUME_RECORD_CAP];
static bool first_saved,release_pending,release_path,ipc_pending,ipc_complete,copy_pending,copy_complete;
static bool output_ack,output_enabled,service_requested;
static uint64_t first_publication_cycles,release_cycles;
static uint32_t release_pc,release_cpsr,release_index,release_driver,copy_dst,copy_src,copy_count,copy_cpsr;
static const char *reference;
static order_log ordering;
static unsigned later_requests;
static bool user_boundary_saved;
static uint8_t later_inputs[2][16384];
static dma_observer dma_observed;
static void later_boundary(unsigned);
static void first_boundary(void);
static void continuation_read(uint32_t,unsigned,uint32_t);
static bool continuation_hook(void*);
static void continuation_done(void);
/* IRQ18 is the original AMC device-tree line. This only drives the existing
 * VIC level; masking/routing and CPU interrupt entry remain the core's work. */
static void irq_level(bool refresh) {
 s5l_vic_set_line(&machine.vic[0],18,amc_engine_irq(&engine));
 machine.level_dirty=true;
 if(refresh)s5l8900_tick(&machine,0);
}
static jmp_buf finish;
static unsigned amcR,amcW,sramR,sramW,clockW,descriptors,expectedBytes;
static unsigned initialReads;
static uint8_t written[AMC_SRAM_SIZE], predicted[AMC_SRAM_SIZE], footprint[AMC_SRAM_SIZE];
static unsigned result;
static bool enabled, upload_verified;
static uint64_t upload_cycles, command_cycles;
static const char *output;
static void dumpfile(const char*,const void*,size_t);
static uint32_t input_address, input_words, input_bytes, verified_driver;
static bool input_seen;
static void stop(const char *why,uint32_t a,uint32_t v,unsigned w){
 printf("STOP %s cycles%llu ticks%llu pc%08x lr%08x address%08x width%u value%08x\n",why,(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,machine.cpu.r[15],machine.cpu.r[14],a,w,v);
 printf("STOP context %s\n",in_guest?"inside-unretired-instruction":"between-instructions");
 result=1;longjmp(finish,1);
}
static bool overlap(uint32_t a,unsigned w,uint32_t base,uint32_t size){return (uint64_t)a+w>base && a<(uint64_t)base+size;}
static bool isAMC(uint32_t a,unsigned w){return overlap(a,w,0x38500000,0x3000);}
static uint32_t rawread(void*c,uint32_t a,unsigned w){return w==4?original.read32(c,a):w==2?original.read16(c,a):original.read8(c,a);}
static void rawwrite(void*c,uint32_t a,unsigned w,uint32_t v){if(w==4)original.write32(c,a,v);else if(w==2)original.write16(c,a,v);else original.write8(c,a,v);}
static void dependencies(void*c){
 uint32_t clock=original.read32(c,0x3c500048);
 if(engine_started){if(amc_engine_dependencies(&engine,clock,machine.power.state)!=AMC_ENGINE_OK)stop("unsupported active dependency",0x3c500048,clock,4);}
 else amc_idle_dependencies(&idle,clock,machine.power.state);
}
static uint32_t readbus(void*c,uint32_t a,unsigned w){uint32_t v;
 if(isAMC(a,w)){
  amcR++;if(enabled){dependencies(c);if(engine_started){if(amc_engine_read(&engine,a-0x38500000,w,&v)!=AMC_ENGINE_OK)stop("unsupported engine read",a,0,w);}else if(amc_idle_read(&idle,a-0x38500000,w,&v)!=AMC_ACCEPT)stop("unsupported AMC read",a,0,w);}else v=rawread(c,a,w);
  if(amcR<200)printf("AMC R %08x width%u value%08x pc%08x tick%llu\n",a,w,v,machine.cpu.r[15],(unsigned long long)machine.timer.ticks);
  return v;
 }
 if(overlap(a,w,AMC_SRAM_BASE,AMC_SRAM_SIZE)){
  sramR++;if(!amc_sram_read(&sram,a-AMC_SRAM_BASE,w,&v))stop("invalid SRAM read",a,0,w);
  for(unsigned i=0;i<w;i++)if(!written[a-AMC_SRAM_BASE+i])initialReads++;
  if(sramR<10)printf("SRAM R %08x width%u value%08x\n",a,w,v);
  if(first_saved)continuation_read(a,w,v);return v;
 }
 return rawread(c,a,w);
}
static void writebus(void*c,uint32_t a,unsigned w,uint32_t v){
 if(isAMC(a,w)){
  amcW++;printf("AMC W %08x width%u value%08x pc%08x tick%llu\n",a,w,v,machine.cpu.r[15],(unsigned long long)machine.timer.ticks);
  if(!enabled){rawwrite(c,a,w,v);return;}
  dependencies(c);
  if(engine_started){
   bool starting=a==0x3850100c&&w==4&&v==1;
   bool later=starting&&third_input_cycles;
   if(later&&later_requests>=2)stop("sixth input request cap",a,v,w);
   if(later){
    unsigned number=4+later_requests;
    if(!admit_later(&engine,&physical,number,machine.cpu.cycles,first_publication_cycles,machine.cpu.r[15],machine.cpu.r[14],machine.cpu.cpsr))stop("later input admission differs",a,v,w);
    if(number==4&&machine.timer.ticks!=1756773987ull)stop("fourth timer prefix differs",a,v,w);
    later_boundary(number);
    size_t offset=0,bytes=0;if(!capture_span(&physical,engine.staged_address,engine.staged_words,&offset,&bytes)||bytes!=16384)stop("later input capture bounds",a,v,w);
    memcpy(later_inputs[number-4],physical.bytes+offset,bytes);
    dumpfile(number==4?"fourth-input.bin":"fifth-input.bin",later_inputs[number-4],bytes);
    printf("CAPTURE later request%u address%08x words%u cycles%llu ticks%llu pc%08x lr%08x cpsr%08x copiedBytes%llu queueBytes%zu decodedBytes%llu pending%u active%u events%08x mask%08x phase%u\n",number,engine.staged_address,engine.staged_words,(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,machine.cpu.r[15],machine.cpu.r[14],machine.cpu.cpsr,(unsigned long long)engine.copied_bytes,engine.queue_used,(unsigned long long)engine.decoded_bytes,engine.input_pending,engine.dma_active,engine.events,engine.mask,engine.input_phase);
   }
   bool third=starting&&second_input_cycles&&!third_input_cycles;
   if(starting&&first_saved&&!third&&!later){
    if(!admit_second(&engine,machine.cpu.cycles,first_publication_cycles,machine.cpu.r[15],machine.cpu.r[14],machine.cpu.cpsr))stop("second input admission witness differs",a,v,w);
    /* Observation only. No MMIO read, engine admission, or guest mutation. */
    size_t offset=0,bytes=0;
    if(!capture_span(&physical,engine.staged_address,engine.staged_words,&offset,&bytes)||bytes!=16384)
     stop("second input observation bounds",a,v,w);
    if(machine.cpu.cycles!=9873247421ull||first_publication_cycles!=9873234585ull||machine.cpu.r[15]!=0xc0278130u||machine.cpu.r[14]!=0xc0274874u)
     stop("second input observation prefix differs",a,v,w);
    dumpfile("second-input.bin",physical.bytes+offset,bytes);
    printf("CAPTURE second address%08x words%u bytes%zu cycles%llu ticks%llu pc%08x lr%08x cpsr%08x firstPublication%llu\n",engine.staged_address,engine.staged_words,bytes,(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,machine.cpu.r[15],machine.cpu.r[14],machine.cpu.cpsr,(unsigned long long)first_publication_cycles);
    printf("CAPTURE preadmission initialized%u configured%u dmaActive%u dmaStart%08x dmaWords%u copiedWords%u copiedBytes%llu queueBytes%zu inputPending%u inputPhase%u transport%u events%08x mask%08x decodedBytes%llu published%llu\n",engine.initialized,engine.configured,engine.dma_active,engine.dma_start,engine.dma_words,engine.copied_words,(unsigned long long)engine.copied_bytes,engine.queue_used,engine.input_pending,engine.input_phase,engine.transport_enabled,engine.events,engine.mask,(unsigned long long)engine.decoded_bytes,(unsigned long long)engine.published_frames);
   }
   if(third){
    if(!admit_third(&engine,machine.cpu.cycles,first_publication_cycles,machine.cpu.r[15],machine.cpu.r[14],machine.cpu.cpsr))stop("third input admission witness differs",a,v,w);
    size_t offset=0,bytes=0;if(!capture_span(&physical,engine.staged_address,engine.staged_words,&offset,&bytes)||bytes!=16384)stop("third input capture bounds",a,v,w);
    dumpfile("third-input.bin",physical.bytes+offset,bytes);
    printf("CAPTURE third address%08x words%u bytes%zu cycles%llu ticks%llu pc%08x lr%08x cpsr%08x\n",engine.staged_address,engine.staged_words,bytes,(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,machine.cpu.r[15],machine.cpu.r[14],machine.cpu.cpsr);
    printf("CAPTURE third-preadmission copiedBytes%llu copiedWords%u queueBytes%zu decodedBytes%llu decodedFrames%llu published%llu events%08x mask%08x inputPending%u dmaActive%u phase%u\n",(unsigned long long)engine.copied_bytes,engine.copied_words,engine.queue_used,(unsigned long long)engine.decoded_bytes,(unsigned long long)engine.decoded_frames,(unsigned long long)engine.published_frames,engine.events,engine.mask,engine.input_pending,engine.dma_active,engine.input_phase);
    dumpfile("third-request/sram.bin",sram.bytes,AMC_SRAM_SIZE);dumpfile("third-request/written.bin",written,AMC_SRAM_SIZE);
    dumpfile("third-request/init-written.bin",engine.init_writes,AMC_SRAM_SIZE);dumpfile("third-request/pcm-written.bin",engine.pcm_writes,AMC_SRAM_SIZE);dumpfile("third-request/host-written.bin",engine.host_writes,AMC_SRAM_SIZE);
    dumpfile("third-request/owned-queue.bin",engine.queue,engine.queue_used);dumpfile("third-request/first-pcm.bin",consumed.expected,CONSUME_BYTES);dumpfile("third-request/registers.bin",machine.cpu.r,sizeof machine.cpu.r);
    dumpfile("third-request/read-coverage.bin",consumed.coverage,CONSUME_BYTES);dumpfile("third-request/read-bytes.bin",consumed.seen,CONSUME_BYTES);dumpfile("third-request/read-ledger.bin",read_ledger,consumed.reads*sizeof(read_record));
   }
   if(starting&&!first_saved&&(!input_seen||first_input_cycles||engine.staged_address!=input_address||engine.staged_words!=input_words))stop("unexpected first DMA request",a,v,w);
   if(starting&&first_saved&&!third&&!later){
    /* Preserve the observed Stage I pre-admission bytes without a guest read. */
    dumpfile("second-request/sram.bin",sram.bytes,AMC_SRAM_SIZE);dumpfile("second-request/written.bin",written,AMC_SRAM_SIZE);
    dumpfile("second-request/init-written.bin",engine.init_writes,AMC_SRAM_SIZE);dumpfile("second-request/pcm-written.bin",engine.pcm_writes,AMC_SRAM_SIZE);dumpfile("second-request/host-written.bin",engine.host_writes,AMC_SRAM_SIZE);
    dumpfile("second-request/owned-queue.bin",engine.queue,engine.queue_used);dumpfile("second-request/first-pcm.bin",consumed.expected,CONSUME_BYTES);dumpfile("second-request/registers.bin",machine.cpu.r,sizeof machine.cpu.r);
    dumpfile("second-request/read-coverage.bin",consumed.coverage,CONSUME_BYTES);dumpfile("second-request/read-bytes.bin",consumed.seen,CONSUME_BYTES);dumpfile("second-request/read-ledger.bin",read_ledger,consumed.reads*sizeof(read_record));
   }
   /* Capture the actual physical span immediately before acceptance. Private
    * guest descriptor/packet objects are evidence only, never decoder inputs. */
   if(starting&&!first_saved)dumpfile("input-final.bin",physical.bytes+input_address-physical.base,4u*input_words);
   bool acknowledged=first_saved&&a==0x38500b04&&w==4&&(v&engine.events&0x400u);
   if(amc_engine_write(&engine,&physical,a-0x38500000,w,v)!=AMC_ENGINE_OK)stop("unsupported engine write",a,v,w);
   if(later){later_requests++;printf("ACCEPT later request%u address%08x words%u cycles%llu; actual live RAM DMA only\n",3+later_requests,engine.staged_address,engine.staged_words,(unsigned long long)machine.cpu.cycles);}
   if(third){third_input_cycles=machine.cpu.cycles;printf("ACCEPT third request physical%08x words%u cycles%llu; actual live RAM DMA only\n",engine.staged_address,engine.staged_words,(unsigned long long)third_input_cycles);}
   if(starting&&first_saved&&!third&&!later){second_input_cycles=machine.cpu.cycles;printf("ACCEPT second request physical%08x words%u cycles%llu; actual live RAM DMA only\n",engine.staged_address,engine.staged_words,(unsigned long long)second_input_cycles);}
   if(starting&&!first_saved){first_input_cycles=machine.cpu.cycles;printf("ACCEPT input request physical%08x bytes%u words%u cycles%llu ticks%llu pc%08x\n",input_address,input_bytes,input_words,(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,machine.cpu.r[15]);}
   if(acknowledged)output_ack=true;
   if(first_saved&&a==0x38500b0c&&(engine.mask&0x400u))output_enabled=true;
   if(first_saved&&a==0x38500004&&v==1)service_requested=true;
   irq_level(false);
   if(!engine.initialized)stop("supported quiesce before target",a,v,w);
   return;
  }
  amc_result_t r=amc_idle_write(&idle,a-0x38500000,w,v);
  if(r==AMC_REJECT)stop("unsupported AMC write",a,v,w);
  if(r==AMC_RUN_REQUEST){
   uint32_t status=0;if(!upload_verified||idle.input_pending||amc_idle_read(&idle,0xb18,4,&status)!=AMC_ACCEPT||status!=0x100||memcmp(&sram,&expected,sizeof sram))stop("run sentinel invariant",a,v,w);
   for(unsigned i=0;i<AMC_SRAM_SIZE;i++)if(written[i]!=footprint[i])stop("run sentinel write coverage",AMC_SRAM_BASE+i,0,1);
   printf("VERIFY original run request cycles%llu ticks%llu pc%08x address%08x width%u value%08x postWrites%u status%08x inputPending%u\n",(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,machine.cpu.r[15],a,w,v,idle.post_prefix,status,idle.input_pending);
   dumpfile("upload/sram.bin",sram.bytes,AMC_SRAM_SIZE);
   dumpfile("upload/expected.bin",expected.bytes,AMC_SRAM_SIZE);
   dumpfile("upload/predicted.bin",predicted,AMC_SRAM_SIZE);
   dumpfile("upload/written.bin",written,AMC_SRAM_SIZE);
   /* Program policy is pinned to prior external original-file evidence.
    * Current descriptor prediction remains a separately checked observation. */
   if(amc_engine_start(&engine,&idle,&sram,written,&policy,&amc_faad_ops,malloc,free)!=AMC_ENGINE_OK)stop("engine start admission",a,v,w);
   engine_started=true;command_cycles=machine.cpu.cycles;
   printf("ACCEPT engine initialization at original +0 store cycles%llu events%08x; store retirement pending\n",(unsigned long long)command_cycles,engine.events);
   irq_level(false);
   return;
  }
  if(r==AMC_INPUT_REQUEST)stop("unexpected uninitialized input request",a,v,w);
  if(r==AMC_UPLOAD_END){
   if(!descriptors||!expectedBytes||memcmp(&sram,&expected,sizeof sram))stop("upload bytes differ",a,v,w);
   for(unsigned i=0;i<AMC_SRAM_SIZE;i++)if(predicted[i]&&!written[i])stop("declared byte not guest-written",AMC_SRAM_BASE+i,0,1);
   printf("PASS original upload bytes%u descriptors%u SRAMwrites%u bit9zero synchronous-reset-approximation\n",expectedBytes,descriptors,sramW);
   if(!amc_idle_observe_upload(&idle))stop("invalid upload observation transition",a,v,w);
   upload_verified=true;upload_cycles=machine.cpu.cycles;
   puts("OBSERVE upload marker verified; returning callback without execution/status/event transition");
   return;
  }
  return;
 }
 if(overlap(a,w,AMC_SRAM_BASE,AMC_SRAM_SIZE)){
  if(engine_started){
   printf("ENGINE SRAM W %08x width%u value%08x pc%08x\n",a,w,v,machine.cpu.r[15]);
   unsigned owner=sram.bytes[AMC_HEADER+8]|(unsigned)sram.bytes[AMC_HEADER+9]<<8;
   if(amc_engine_sram_write(&engine,a-AMC_SRAM_BASE,w,v)!=AMC_ENGINE_OK)stop("unsupported engine SRAM write",a,v,w);
   unsigned after=sram.bytes[AMC_HEADER+8]|(unsigned)sram.bytes[AMC_HEADER+9]<<8;
   if(first_saved&&a==0x2200f208u)printf("OWNER WRITE accepted address%08x width%u value%08x before%u after%u cycles%llu pc%08x cpsr%08x\n",a,w,v,owner,after,(unsigned long long)machine.cpu.cycles,machine.cpu.r[15],machine.cpu.cpsr);
   if(first_saved&&consume_released(a,w,v,owner,after)){
    release_pending=true;release_cycles=machine.cpu.cycles;release_pc=machine.cpu.r[15];release_cpsr=machine.cpu.cpsr;
    printf("RELEASE storePending cycles%llu pc%08x cpsr%08x covered%u path%u index%u driver%08x\n",(unsigned long long)release_cycles,release_pc,release_cpsr,consumed.covered,release_path,release_index,release_driver);
   }
   return;
  }
  if(upload_verified)stop("SRAM write after verified upload",a,v,w);
  if(!idle.upload)stop("SRAM write before supported upload",a,v,w);
  if(w!=4 || (a&3) || a<AMC_SRAM_BASE || (uint64_t)a+w>(uint64_t)AMC_SRAM_BASE+AMC_SRAM_SIZE)stop("invalid SRAM write",a,v,w);
  for(unsigned i=0;i<w;i++)if(!footprint[a-AMC_SRAM_BASE+i])stop("undeclared SRAM write",a,v,w);
  if(!amc_sram_write(&sram,a-AMC_SRAM_BASE,w,v))stop("invalid SRAM write",a,v,w);
  sramW++;for(unsigned i=0;i<w;i++)written[a-AMC_SRAM_BASE+i]=1;
  if(sramW<10)printf("SRAM W %08x width%u value%08x pc%08x\n",a,w,v,machine.cpu.r[15]);return;
 }
 if(engine_started&&(overlap(a,w,0x3c500050,4)||overlap(a,w,0x3c500048,4)||overlap(a,w,0x39a00000,0x80)))stop("unmodeled active lifecycle",a,v,w);
 if(overlap(a,w,0x3c500050,4)){
  clockW++;printf("RESET W %08x width%u value%08x pc%08x\n",a,w,v,machine.cpu.r[15]);
  if(enabled){dependencies(c);if(a!=0x3c500050||w!=4||amc_idle_reset(&idle,v)!=AMC_ACCEPT)stop("unsupported reset",a,v,w);}
 }
 rawwrite(c,a,w,v);
 if(enabled&&(overlap(a,w,0x3c500048,4)||overlap(a,w,0x39a00000,0x80)))dependencies(c);
}
static uint32_t r32(void*c,uint32_t a){return readbus(c,a,4);}static uint16_t r16(void*c,uint32_t a){return readbus(c,a,2);}static uint8_t r8(void*c,uint32_t a){return readbus(c,a,1);}
static void w32(void*c,uint32_t a,uint32_t v){writebus(c,a,4,v);}static void w16(void*c,uint32_t a,uint16_t v){writebus(c,a,2,v);}static void w8(void*c,uint32_t a,uint8_t v){writebus(c,a,1,v);}
static uint32_t ptradd(uint32_t a,uint64_t delta){if((uint64_t)a+delta>UINT32_MAX)stop("guest pointer overflow",a,0,0);return a+(uint32_t)delta;}
static uint32_t translate(uint32_t va){uint32_t pa;if(arm_mmu_translate(&machine.cpu,va,ARM_ACCESS_READ,true,&pa))stop("unmapped descriptor",va,0,4);return pa;}
static uint32_t word(uint32_t va){
 if(va&3)stop("unaligned descriptor",va,0,4);uint32_t pa=translate(va),v;
 if(pa<machine.ram_base||(uint64_t)pa+4>(uint64_t)machine.ram_base+machine.ram_size)stop("non-RAM descriptor",va,pa,4);
 memcpy(&v,machine.ram+pa-machine.ram_base,4);return v;
}
static void prediction(uint32_t a,unsigned w,uint32_t v){
 if(a<AMC_SRAM_BASE||(uint64_t)a+w>(uint64_t)AMC_SRAM_BASE+AMC_SRAM_SIZE)stop("descriptor bank extent",a,v,w);
 for(unsigned i=0;i<w;i++){unsigned off=a-AMC_SRAM_BASE+i;if(predicted[off])stop("overlapping descriptor byte",a,v,w);expectedBytes++;predicted[off]=1;}
 unsigned aligned=(a-AMC_SRAM_BASE)&~3u;for(unsigned i=0;i<4;i++)footprint[aligned+i]=1;
 if(!amc_sram_write(&expected,a-AMC_SRAM_BASE,w,v))stop("prediction write bounds",a,v,w);
}
static void describe(uint32_t pc){
 uint32_t cell=machine.cpu.r[0],desc=machine.cpu.r[1],baseVA=word(cell),base=translate(baseVA);
 bool half=pc==0xc0277930;uint32_t bank=half?0x28000:pc==0xc0277908?0x1c000:0x14000;
 uint32_t bankSize=half?0x4000:pc==0xc0277908?0xc000:0x8000;
 if(base!=AMC_SRAM_BASE+bank)stop("unexpected descriptor destination",base,bank,4);
 uint32_t data=word(desc),map=word(ptradd(desc,4));uint32_t lanes=word(data),dataList=word(ptradd(data,4)),mapList=word(ptradd(map,4));
 printf("DESCRIPTOR %u pc%08x cell%08x descriptor%08x baseVA%08x basePA%08x data%08x map%08x lanes%u\n",descriptors,pc,cell,desc,baseVA,base,data,map,lanes);
 if(!lanes||lanes>32||word(map)!=lanes)stop("unsupported lane count",desc,lanes,4);
 for(unsigned lane=0;lane<lanes;lane++){
  uint32_t d=ptradd(dataList,8u*lane),m=ptradd(mapList,8u*lane),source=word(ptradd(d,4)),count=word(m),segments=word(ptradd(m,4)),length=word(d);
  printf("LANE %u dataHeader%u source%08x segmentCount%u segments%08x\n",lane,word(d),source,count,segments);
  if(count>1024)stop("descriptor segment count",m,count,4);uint64_t cursor=0;
  for(unsigned seg=0;seg<count;seg++){
   uint32_t rec=ptradd(segments,8u*seg),start=word(rec),n=word(ptradd(rec,4));
   printf("SEGMENT lane%u index%u start%u count%u sourceOffset%llu\n",lane,seg,start,n,(unsigned long long)cursor*4);
   if(n>AMC_SRAM_SIZE/2||cursor+n>length)stop("descriptor count bound",rec,n,4);
   for(uint32_t j=0;j<n;j++){
    uint64_t off=half?2*((uint64_t)start+j):4*(((uint64_t)start+j)*lanes+lane);unsigned width=half?2:4;
    if(off+width>bankSize)stop("descriptor exceeds bank",base,(uint32_t)off,width);
    uint32_t sourceVA=ptradd(source,4*cursor++),value=word(sourceVA);
    prediction(base+(uint32_t)off,width,value);
   }
  }
  if(cursor!=length)stop("descriptor declared length",d,length,4);
 }
 descriptors++;
}
static void dump_guest_words(const char *name,uint32_t va,unsigned size){
 uint32_t copy[256];if(size>sizeof copy||(size&3))stop("diagnostic object bound",va,size,0);
 for(unsigned i=0;i<size/4;i++)copy[i]=word(ptradd(va,4u*i));dumpfile(name,copy,size);
}
static void capture_descriptor_data(const char *name,const char *kind,uint32_t descriptor,unsigned size,uint32_t physical){
 uint32_t flags=word(ptradd(descriptor,0x10)),length=word(ptradd(descriptor,0x1c)),capacity=word(ptradd(descriptor,0x6c));
 uint32_t va=word(ptradd(descriptor,(flags&0xf0u)==0x20u?0x68:0x34));
 if(!size)size=length;
 if(!size||size>0x10000u||(size&3u)||size>length||length>capacity||capacity>0x10000u)stop("descriptor length/capacity",descriptor,size,4);
 uint8_t data[0x10000];
 for(unsigned off=0;off<size;off+=4){uint32_t address=ptradd(va,off),pa=translate(address),value=word(address);
  if(physical&&pa!=physical+off)stop("input virtual/physical mapping",address,pa,4);
  if(!(address&0xfffu)||!off)printf("INPUTMAP %s va%08x pa%08x offset%u\n",kind,address,pa,off);
  memcpy(data+off,&value,4);
 }
 printf("INPUTDATA %s descriptor%08x virtual%08x length%u capacity%u captured%u\n",kind,descriptor,va,length,capacity,size);
 dumpfile(name,data,size);
}
static void input_capture(void){
 uint32_t driver=machine.cpu.r[5],descriptor=machine.cpu.r[4];
 uint32_t address=machine.cpu.r[0],words=machine.cpu.r[1],bytes=machine.cpu.r[8];
 if(input_seen||!engine_started||!engine.initialized||engine.setup_phase!=4u||driver!=verified_driver||word(ptradd(driver,0xc0))!=2u||machine.cpu.r[14]!=0xc0275f58)stop("unexpected input caller",address,words,4);
 if(!bytes||bytes>0x10000u||words!=(bytes+3u)/4u||(address&3u)||address<machine.ram_base||
    (uint64_t)address+4ull*words>(uint64_t)machine.ram_base+machine.ram_size)stop("input span bound",address,words,4);
 uint32_t encrypted=(word(ptradd(driver,0xd8))>>8)&255u;
 if(encrypted)stop("unmodeled encrypted input",driver,encrypted,1);
 unsigned index=3,matches=0;for(unsigned i=0;i<3;i++)if(word(ptradd(driver,0x110+4u*i))==descriptor){index=i;matches++;}
 if(matches!=1)stop("ambiguous input descriptor",driver,descriptor,4);
 uint32_t packet=word(ptradd(driver,0x128+4u*index)),vt=word(descriptor),method=word(ptradd(vt,0x100));
 if(!packet)stop("missing packet descriptor",driver,index,4);
 if(vt!=0xc01929b4u||method!=0xc0147bffu||word(packet)!=vt)stop("unsupported input descriptor type",descriptor,vt,4);
 printf("INPUT driver%08x index%u descriptor%08x vtable%08x addressMethod%08x packetDescriptor%08x physical%08x bytes%u words%u encrypted%u\n",driver,index,descriptor,vt,method,packet,address,bytes,words,encrypted);
 dump_guest_words("input-driver-prefix.bin",driver,0x180);
 dump_guest_words("input-descriptor-prefix.bin",descriptor,0x80);
 dump_guest_words("packet-descriptor-prefix.bin",packet,0x80);
 dump_guest_words("input-vtable-prefix.bin",vt,0x104);
 capture_descriptor_data("input-virtual.bin","input",descriptor,4u*words,address);
 capture_descriptor_data("input-capacity.bin","input-capacity",descriptor,0,0);
 capture_descriptor_data("packet-data.bin","packet",packet,0,0);
 dumpfile("input-rounded.bin",machine.ram+address-machine.ram_base,4u*words);
 /* Observation only: engine uses public physical address/count MMIO. */
 input_address=address;input_words=words;input_bytes=bytes;input_seen=true;
}
static bool hook(void*c){(void)c;uint32_t pc=machine.cpu.r[15];printf("HOOK pc%08x lr%08x r0%08x r1%08x r2%08x r3%08x\n",pc,machine.cpu.r[14],machine.cpu.r[0],machine.cpu.r[1],machine.cpu.r[2],machine.cpu.r[3]);
 if(pc==0xc0274a34){uint32_t driver=machine.cpu.r[4],params=machine.cpu.r[6];printf("CALLER driver%08x params%08x",driver,params);for(unsigned i=0;i<7;i++)printf(" p%u:%08x",i,word(ptradd(params,4u*i)));puts("");if(word(params)!=2)stop("unexpected codec type",params,word(params),4);verified_driver=driver;}
 if(pc==0xc0274ad8){uint32_t obj=machine.cpu.r[0],vt=word(obj);printf("TASK object%08x vtable%08x method%08x argument%u\n",obj,vt,word(ptradd(vt,8)),machine.cpu.r[1]);if(vt!=0xc029c844||word(ptradd(vt,8))!=0xc02782fc||machine.cpu.r[1]!=0)stop("unexpected task manager",obj,vt,4);}
 if(pc==0xc0277908||pc==0xc0277930||pc==0xc027795c)describe(pc);
 if(pc==0xc0278084)input_capture();
 if(pc==0xc027721c)puts("OBSERVE original interrupt enable helper");
 if(pc==0xc0276010){
  if(machine.cpu.r[0]!=verified_driver||layout_seen)stop("unexpected layout caller",pc,0,0);
  layout_pointers[0]=machine.cpu.r[1];layout_pointers[1]=machine.cpu.r[2];layout_pointers[2]=machine.cpu.r[3];layout_pointers[3]=word(machine.cpu.r[13]);
 }
 if(pc==0xc0276014){
  uint32_t offset0=word(layout_pointers[0]),capacity0=word(layout_pointers[1]),offset1=word(layout_pointers[2]),capacity1=word(layout_pointers[3]);
  printf("HOST layout return%u firstOffset%08x firstCapacity%u secondOffset%08x secondCapacity%u\n",machine.cpu.r[0],offset0,capacity0,offset1,capacity1);
  if(machine.cpu.r[0]||offset0!=AMC_OUTPUT0||offset1!=AMC_OUTPUT1||capacity0!=AMC_OUTPUT_BYTES||capacity1!=AMC_OUTPUT_BYTES)stop("unexpected returned layout",pc,machine.cpu.r[0],4);
  layout_seen=true;
 }
 if(pc==0xc0276014||pc==0xc027602c)printf("HOST layout/session return pc%08x r0%08x verifiedDriver%08x\n",pc,machine.cpu.r[0],verified_driver);
 return false;
}
static void dumpfile(const char *name,const void *data,size_t n){
 char path[1024];if(snprintf(path,sizeof path,"%s/%s",output,name)>=(int)sizeof path){perror("artifact path");exit(7);}
 FILE*f=fopen(path,"wb");if(!f){perror(path);exit(7);}if(fwrite(data,1,n,f)!=n||fclose(f)){perror(path);exit(7);}
}
static bool load_exact(const char *name,void *data,size_t n){FILE*f=fopen(name,"rb");if(!f)return false;bool ok=fread(data,1,n,f)==n&&fgetc(f)==EOF;return fclose(f)==0&&ok;}
#include "continuation.inc"
static void later_boundary(unsigned number){
 const char*dir=number==4?"fourth-request":number==5?"fifth-request":"user-return";char path[128];
 #define SAVE_LATER(name,data,size) do{snprintf(path,sizeof path,"%s/%s",dir,name);dumpfile(path,data,size);}while(0)
 SAVE_LATER("sram.bin",sram.bytes,AMC_SRAM_SIZE);SAVE_LATER("expected.bin",expected.bytes,AMC_SRAM_SIZE);SAVE_LATER("predicted.bin",predicted,AMC_SRAM_SIZE);SAVE_LATER("written.bin",written,AMC_SRAM_SIZE);
 SAVE_LATER("init-written.bin",engine.init_writes,AMC_SRAM_SIZE);SAVE_LATER("pcm-written.bin",engine.pcm_writes,AMC_SRAM_SIZE);SAVE_LATER("host-written.bin",engine.host_writes,AMC_SRAM_SIZE);
 SAVE_LATER("registers.bin",machine.cpu.r,sizeof machine.cpu.r);SAVE_LATER("owned-queue.bin",engine.queue,engine.queue_used);SAVE_LATER("first-pcm.bin",consumed.expected,CONSUME_BYTES);
 SAVE_LATER("read-coverage.bin",consumed.coverage,CONSUME_BYTES);SAVE_LATER("read-bytes.bin",consumed.seen,CONSUME_BYTES);SAVE_LATER("read-ledger.bin",read_ledger,consumed.reads*sizeof(read_record));SAVE_LATER("ordering.bin",ordering.records,ordering.count*sizeof(order_record));
 #undef SAVE_LATER
}
static void service_boundary(void){
 if(!engine_started)return;
 if(release_pending){continuation_done();return;}
 unsigned owner=sram.bytes[AMC_HEADER+8+2*engine.next_output]|(unsigned)sram.bytes[AMC_HEADER+9+2*engine.next_output]<<8;
 if(first_saved&&engine.published_frames>=2&&engine.service&&!(engine.events&0x400)&&engine.queue_used&&owner)stop("third publication work cap",0,0,0);
 unsigned before=engine.next_output;
 dma_record observed={0};
 if(later_requests&&!copy_prepare(&dma_observed,&engine,&physical,3+later_requests,later_inputs[later_requests-1],machine.cpu.cycles,&observed))stop("actual DMA copy observation bounds/changed bytes",0,0,0);
 amc_engine_result r=amc_engine_step(&engine,&physical);service_calls++;
 if(later_requests&&!copy_finish(&dma_observed,&engine,&observed))stop("actual DMA copy accounting differs",0,0,0);
 if(observed.bytes)printf("DMA COPY request%u address%08x bytes%u offset%u cycles%llu copiedBefore%llu decodedBefore%llu decodedAfter%llu wordsBefore%u wordsAfter%u queueBefore%u queueAfter%u pending%u active%u\n",observed.request,observed.address,observed.bytes,observed.offset,(unsigned long long)observed.cycles,(unsigned long long)observed.copied_before,(unsigned long long)observed.decoded_before,(unsigned long long)observed.decoded_after,observed.words_before,observed.words_after,observed.queue_before,observed.queue_after,observed.pending,observed.active);
 if(r==AMC_ENGINE_FATAL||r==AMC_ENGINE_REJECT)stop("engine service failure at retired boundary",0,0,0);
 if(r==AMC_ENGINE_PUBLISHED){
  publication_buffer=before;
  if(!layout_seen||!input_seen||!first_input_cycles)stop("publication before observed host layout/input",0,0,0);
  if(engine.published_frames>2)stop("publication cap",0,0,0);
  if(first_saved){
   printf("PUBLICATION second cycles%llu frames%llu decodedBytes%llu samples%llu buffer%u\n",(unsigned long long)machine.cpu.cycles,(unsigned long long)engine.decoded_frames,(unsigned long long)engine.decoded_bytes,(unsigned long long)engine.sample_units,before);
   dumpfile("second-pcm.bin",sram.bytes+AMC_OUTPUT1,4096);irq_level(true);return;
  }
  printf("PASS first PCM publication after retired instruction cycles%llu ticks%llu decodedFrames%llu decodedBytes%llu sampleUnits%llu buffer%u events%08x mask%08x inputPending%u irqWouldAssert%u serviceCalls%llu; no next guest instruction or output IRQ refresh\n",
   (unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,(unsigned long long)engine.decoded_frames,(unsigned long long)engine.decoded_bytes,(unsigned long long)engine.sample_units,before,engine.events,engine.mask,engine.input_pending,amc_engine_irq(&engine),(unsigned long long)service_calls);
  first_boundary();irq_level(true);return;
 }
 irq_level(true);
}
int main(int argc,char**argv){
 if(argc!=7)return 2;reference=argv[6];enabled=atoi(argv[2])!=0;output=argv[3];setvbuf(stdout,NULL,_IOLBF,0);
 if(!load_exact(argv[4],policy_sram,sizeof policy_sram)||!load_exact(argv[5],policy_written,sizeof policy_written))return 8;
 amc_engine_init(&engine);amc_idle_init(&idle);amc_sram_init(&sram);amc_sram_init(&expected);
 machine_ready=s5l_board_init(s5l_board_find("m68"),&machine,256u<<20);if(!machine_ready||snapshot_load(&machine,argv[1]))return 3;
 if(amc_sram_ram_conflict(machine.ram_base,machine.ram_size))return 4;
 physical=(amc_physical_ram){machine.ram,machine.ram_base,machine.ram_size};
 machine.mtz1.uptime_ms=machine.timer.ticks/6000;machine.mtz1.uptime_tick_acc=machine.timer.ticks%6000;
 reset_host_inputs();emu_release_inputs();s5l8900_set_uart0_host(&machine,serial_tx,NULL);s5l8900_set_direct_ram_writes(&machine,false);
 original=machine.bus;machine.bus.read32=r32;machine.bus.read16=r16;machine.bus.read8=r8;machine.bus.write32=w32;machine.bus.write16=w16;machine.bus.write8=w8;
 uint32_t pcs[]={0xc0274a34,0xc0274ad8,0xc0277ea0,0xc02718f8,0xc027744c,0xc0277844,0xc02778b8,0xc0277908,0xc0277930,0xc027795c,0xc027721c,0xc0277f94,0xc0278084,0xc0276010,0xc0276014,0xc027602c};
 if(!s5l8900_set_pre_step_hook(&machine,hook,NULL,pcs,sizeof pcs/sizeof pcs[0]))return 9;
 printf("START cycles%llu ticks%llu enabled%u\n",(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,enabled);
 start_cycles=machine.cpu.cycles;
 if(!setjmp(finish)){
  emu_touch(1,140,140);
  /* One instruction per service boundary is a declared functional scheduling
   * policy, not measured DSP timing and not the old 50k batching schedule. */
  for(uint64_t calls=0;calls<155300000ull;calls++){
   uint64_t retired=machine.cpu.cycles-start_cycles;
   if(retired>=155300000ull)stop("total retirement cap",0,0,0);
   if(!touch_released&&retired>=5000000){emu_touch(0,140,140);touch_released=true;}
   if(command_cycles&&!first_input_cycles&&machine.cpu.cycles-command_cycles>=5000000)stop("pre-input retirement cap",0,0,0);
   if(first_input_cycles&&machine.cpu.cycles-first_input_cycles>=5000000)stop("post-input retirement cap",0,0,0);
   if(first_saved&&machine.cpu.cycles-first_publication_cycles>=5000000)stop("post-publication retirement cap",0,0,0);
   uint64_t before=machine.cpu.cycles;in_guest=true;emu_run(1);in_guest=false;
   if(machine.cpu.cycles!=before+1)stop("unexpected one-step retirement",0,0,0);
   if(ordering.pending){
    if(order_finish(&ordering,&machine.cpu)!=ORDER_OK)stop("ordering retirement boundary differs",0,ordering.count,0);
    const order_record*r=&ordering.records[ordering.count-1];
    printf("ORDER record%u retired%u pc%08x cpsr%08x cycles%llu afterPC%08x afterCPSR%08x afterCycles%llu driver%08x state%08x output0%08x output1%08x flags%04x frame%08x savedLR%08x",ordering.count,r->retired,r->pc,r->cpsr,(unsigned long long)r->cycles,r->after_pc,r->after_cpsr,(unsigned long long)r->after_cycles,r->driver,r->state,r->output[0],r->output[1],r->flags,r->frame,r->saved_lr);
    for(unsigned n=0;n<16;n++)printf(" r%u=%08x",n,r->regs[n]);putchar('\n');
    printf("ORDER context%u instruction%08x bytes%u object%08x objectBytes%u linked%08x linkedBytes%u stack%08x stackBytes%u afterLR%08x objectHex",ordering.count,r->instruction,r->instruction_bytes,r->object_address,r->object_bytes,r->linked_address,r->linked_bytes,r->stack_address,r->stack_bytes,r->after_lr);
    for(unsigned n=0;n<sizeof r->object;n++)printf("%02x",r->object[n]);printf(" linkedHex");for(unsigned n=0;n<sizeof r->linked;n++)printf("%02x",r->linked[n]);printf(" stackHex");for(unsigned n=0;n<sizeof r->stack;n++)printf("%02x",r->stack[n]);printf(" codeHex");for(unsigned n=0;n<sizeof r->code;n++)printf("%02x",r->code[n]);putchar('\n');
   }
   service_boundary();if(result==6)break;
  }
  if(result!=6)stop("run call cap",0,0,0);
 }
 uint8_t serial[65537];int n=emu_serial_read(serial,65536);serial[n]=0;
 printf("SERIAL\n%s\nFINAL result%u cycles%llu ticks%llu amcR%u amcW%u sramR%u sramW%u resets%u descriptors%u expectedBytes%u initialByteReads%u\n",serial,result,(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,amcR,amcW,sramR,sramW,clockW,descriptors,expectedBytes,initialReads);
 dumpfile("sram.bin",sram.bytes,AMC_SRAM_SIZE);dumpfile("expected.bin",expected.bytes,AMC_SRAM_SIZE);dumpfile("predicted.bin",predicted,AMC_SRAM_SIZE);dumpfile("written.bin",written,AMC_SRAM_SIZE);dumpfile("registers.bin",machine.cpu.r,sizeof machine.cpu.r);
 dumpfile("init-written.bin",engine.init_writes,AMC_SRAM_SIZE);dumpfile("pcm-written.bin",engine.pcm_writes,AMC_SRAM_SIZE);dumpfile("host-written.bin",engine.host_writes,AMC_SRAM_SIZE);
 if(engine.queue)dumpfile("owned-queue.bin",engine.queue,engine.queue_used);
 if(first_saved){dumpfile("first-pcm.bin",consumed.expected,CONSUME_BYTES);}
 if(result==5){uint32_t off=publication_buffer?AMC_OUTPUT1:AMC_OUTPUT0;uint32_t countoff=AMC_HEADER+12+2*publication_buffer;uint32_t count=sram.bytes[countoff]|(uint32_t)sram.bytes[countoff+1]<<8;
  if(!count||2u*count>AMC_OUTPUT_BYTES){fputs("invalid publication size\n",stderr);return 9;}
  dumpfile("first-pcm.bin",sram.bytes+off,2u*count);
 }
 printf("ENGINE state configured%u copiedBytes%llu copiedWords%u queueBytes%zu decodedBytes%llu decodedFrames%llu publishedFrames%llu sampleUnits%llu events%08x mask%08x inputPending%u\n",engine.configured,(unsigned long long)engine.copied_bytes,engine.copied_words,engine.queue_used,(unsigned long long)engine.decoded_bytes,(unsigned long long)engine.decoded_frames,(unsigned long long)engine.published_frames,(unsigned long long)engine.sample_units,engine.events,engine.mask,engine.input_pending);
 printf("BOUNDARY result%u interruptedInstruction%u startCycles%llu commandCycles%llu firstInputCycles%llu totalRetired%llu\n",result,in_guest,(unsigned long long)start_cycles,(unsigned long long)command_cycles,(unsigned long long)first_input_cycles,(unsigned long long)(machine.cpu.cycles-start_cycles));
 dumpfile("read-coverage.bin",consumed.coverage,CONSUME_BYTES);dumpfile("read-bytes.bin",consumed.seen,CONSUME_BYTES);dumpfile("read-ledger.bin",read_ledger,consumed.reads*sizeof(read_record));
 printf("CONSUMPTION result%u covered%u reads%u ipc%u copy%u acknowledged%u enabled%u service%u releasePending%u releaseCycles%llu retiredCycles%llu releasePC%08x releaseCPSR%08x index%u driver%08x published%llu\n",result,consumed.covered,consumed.reads,ipc_complete,copy_complete,output_ack,output_enabled,service_requested,release_pending,(unsigned long long)release_cycles,(unsigned long long)machine.cpu.cycles,release_pc,release_cpsr,release_index,release_driver,(unsigned long long)engine.published_frames);
 dumpfile("ordering.bin",ordering.records,ordering.count*sizeof(order_record));
 printf("ORDER summary records%u cap%u\n",ordering.count,ORDER_CAP);
 dumpfile("dma-copies.bin",dma_observed.records,dma_observed.count*sizeof(dma_record));dumpfile("dma-bytes.bin",dma_observed.bytes,dma_observed.used);
 printf("DMA FINAL accepted%u start%08x words%u copiedWords%u current%08x copiedBytes%llu queueBytes%zu decodedBytes%llu pending%u active%u ledgerRecords%u ledgerBytes%u\n",3+later_requests,engine.dma_start,engine.dma_words,engine.copied_words,engine.dma_start+4u*engine.copied_words,(unsigned long long)engine.copied_bytes,engine.queue_used,(unsigned long long)engine.decoded_bytes,engine.input_pending,engine.dma_active,dma_observed.count,dma_observed.used);
 amc_engine_destroy(&engine);machine.bus=original;release_machine();return result==6?0:10;
}
