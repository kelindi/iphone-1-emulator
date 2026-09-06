/* Disposable native probe. longjmp is used only to terminate an unsupported
 * bus request before its device mutation, or immediately after upload sentinel.
 * The interrupted instruction is NOT a resumable snapshot and is never saved. */
#include "emu_api.c"
#include "amc_sram.h"
#include "amc_idle.h"
#include <setjmp.h>
static arm_bus_t original;
static amc_sram_t sram, expected;
static amc_idle_t idle;
static jmp_buf finish;
static unsigned amcR,amcW,sramR,sramW,clockW,descriptors,expectedBytes;
static unsigned initialReads;
static uint8_t written[AMC_SRAM_SIZE], predicted[AMC_SRAM_SIZE], footprint[AMC_SRAM_SIZE];
static unsigned result;
static bool enabled, upload_verified;
static uint64_t upload_cycles;
static const char *output;
static void stop(const char *why,uint32_t a,uint32_t v,unsigned w){
 printf("STOP %s cycles%llu ticks%llu pc%08x lr%08x address%08x width%u value%08x\n",why,(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,machine.cpu.r[15],machine.cpu.r[14],a,w,v);
 result=1;longjmp(finish,1);
}
static bool overlap(uint32_t a,unsigned w,uint32_t base,uint32_t size){return (uint64_t)a+w>base && a<(uint64_t)base+size;}
static bool isAMC(uint32_t a,unsigned w){return overlap(a,w,0x38500000,0x3000);}
static uint32_t rawread(void*c,uint32_t a,unsigned w){return w==4?original.read32(c,a):w==2?original.read16(c,a):original.read8(c,a);}
static void rawwrite(void*c,uint32_t a,unsigned w,uint32_t v){if(w==4)original.write32(c,a,v);else if(w==2)original.write16(c,a,v);else original.write8(c,a,v);}
static void dependencies(void*c){amc_idle_dependencies(&idle,original.read32(c,0x3c500048),machine.power.state);}
static uint32_t readbus(void*c,uint32_t a,unsigned w){uint32_t v;
 if(isAMC(a,w)){
  amcR++;if(enabled){dependencies(c);if(amc_idle_read(&idle,a-0x38500000,w,&v)!=AMC_ACCEPT)stop("unsupported AMC read",a,0,w);}else v=rawread(c,a,w);
  if(amcR<200)printf("AMC R %08x width%u value%08x pc%08x tick%llu\n",a,w,v,machine.cpu.r[15],(unsigned long long)machine.timer.ticks);
  return v;
 }
 if(overlap(a,w,AMC_SRAM_BASE,AMC_SRAM_SIZE)){
  sramR++;if(!amc_sram_read(&sram,a-AMC_SRAM_BASE,w,&v))stop("invalid SRAM read",a,0,w);
  for(unsigned i=0;i<w;i++)if(!written[a-AMC_SRAM_BASE+i])initialReads++;
  if(sramR<10)printf("SRAM R %08x width%u value%08x\n",a,w,v);return v;
 }
 return rawread(c,a,w);
}
static void writebus(void*c,uint32_t a,unsigned w,uint32_t v){
 if(isAMC(a,w)){
  amcW++;printf("AMC W %08x width%u value%08x pc%08x tick%llu\n",a,w,v,machine.cpu.r[15],(unsigned long long)machine.timer.ticks);
  if(!enabled){rawwrite(c,a,w,v);return;}
  dependencies(c);amc_result_t r=amc_idle_write(&idle,a-0x38500000,w,v);
  if(r==AMC_REJECT)stop("unsupported AMC write",a,v,w);
  if(r==AMC_UPLOAD_END){
   if(!descriptors||!expectedBytes||memcmp(&sram,&expected,sizeof sram))stop("upload bytes differ",a,v,w);
   for(unsigned i=0;i<AMC_SRAM_SIZE;i++)if(predicted[i]&&!written[i])stop("declared byte not guest-written",AMC_SRAM_BASE+i,0,1);
   printf("PASS original upload bytes%u descriptors%u SRAMwrites%u bit9zero synchronous-reset-approximation\n",expectedBytes,descriptors,sramW);
   upload_verified=true;upload_cycles=machine.cpu.cycles;
   puts("OBSERVE upload marker verified; returning callback without execution/status/event transition");
   return;
  }
  return;
 }
 if(overlap(a,w,AMC_SRAM_BASE,AMC_SRAM_SIZE)){
  if(upload_verified)stop("SRAM write after verified upload",a,v,w);
  if(!idle.upload)stop("SRAM write before supported upload",a,v,w);
  if(w!=4 || (a&3) || a<AMC_SRAM_BASE || (uint64_t)a+w>(uint64_t)AMC_SRAM_BASE+AMC_SRAM_SIZE)stop("invalid SRAM write",a,v,w);
  for(unsigned i=0;i<w;i++)if(!footprint[a-AMC_SRAM_BASE+i])stop("undeclared SRAM write",a,v,w);
  if(!amc_sram_write(&sram,a-AMC_SRAM_BASE,w,v))stop("invalid SRAM write",a,v,w);
  sramW++;for(unsigned i=0;i<w;i++)written[a-AMC_SRAM_BASE+i]=1;
  if(sramW<10)printf("SRAM W %08x width%u value%08x pc%08x\n",a,w,v,machine.cpu.r[15]);return;
 }
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
static bool hook(void*c){(void)c;uint32_t pc=machine.cpu.r[15];printf("HOOK pc%08x lr%08x r0%08x r1%08x r2%08x r3%08x\n",pc,machine.cpu.r[14],machine.cpu.r[0],machine.cpu.r[1],machine.cpu.r[2],machine.cpu.r[3]);
 if(pc==0xc0274a34){uint32_t driver=machine.cpu.r[4],params=machine.cpu.r[6];printf("CALLER driver%08x params%08x",driver,params);for(unsigned i=0;i<7;i++)printf(" p%u:%08x",i,word(ptradd(params,4u*i)));puts("");}
 if(pc==0xc0274ad8){uint32_t obj=machine.cpu.r[0],vt=word(obj);printf("TASK object%08x vtable%08x method%08x argument%u\n",obj,vt,word(ptradd(vt,8)),machine.cpu.r[1]);}
 if(pc==0xc0277908||pc==0xc0277930||pc==0xc027795c)describe(pc);
 if(pc==0xc027721c||pc==0xc0277f94||pc==0xc0278084)stop("start/transfer boundary",pc,0,0);
 return false;
}
static void dumpfile(const char *name,const void *data,size_t n){char path[512];snprintf(path,sizeof path,"%s/%s",output,name);FILE*f=fopen(path,"wb");if(f){fwrite(data,1,n,f);fclose(f);}}
int main(int argc,char**argv){
 if(argc!=4)return 2;enabled=atoi(argv[2])!=0;output=argv[3];setvbuf(stdout,NULL,_IOLBF,0);
 amc_idle_init(&idle);amc_sram_init(&sram);amc_sram_init(&expected);
 machine_ready=s5l_board_init(s5l_board_find("m68"),&machine,256u<<20);if(!machine_ready||snapshot_load(&machine,argv[1]))return 3;
 if(amc_sram_ram_conflict(machine.ram_base,machine.ram_size))return 4;
 machine.mtz1.uptime_ms=machine.timer.ticks/6000;machine.mtz1.uptime_tick_acc=machine.timer.ticks%6000;
 reset_host_inputs();emu_release_inputs();s5l8900_set_uart0_host(&machine,serial_tx,NULL);s5l8900_set_direct_ram_writes(&machine,false);
 original=machine.bus;machine.bus.read32=r32;machine.bus.read16=r16;machine.bus.read8=r8;machine.bus.write32=w32;machine.bus.write16=w16;machine.bus.write8=w8;
 uint32_t pcs[]={0xc0274a34,0xc0274ad8,0xc0277ea0,0xc02718f8,0xc027744c,0xc0277844,0xc02778b8,0xc0277908,0xc0277930,0xc027795c,0xc027721c,0xc0277f94,0xc0278084};
 s5l8900_set_pre_step_hook(&machine,hook,NULL,pcs,sizeof pcs/sizeof pcs[0]);
 printf("START cycles%llu ticks%llu enabled%u\n",(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,enabled);
 if(!setjmp(finish)){
  emu_touch(1,140,140);for(unsigned i=0;i<100;i++)emu_run(50000);emu_touch(0,140,140);
  for(unsigned i=0;i<3006;i++){if(!emu_run(50000))break;if(!enabled&&amcR>=12)break;if(upload_verified&&machine.cpu.cycles-upload_cycles>=5000000)stop("post-upload budget",0,0,0);}
 }
 uint8_t serial[65537];int n=emu_serial_read(serial,65536);serial[n]=0;
 printf("SERIAL\n%s\nFINAL result%u cycles%llu ticks%llu amcR%u amcW%u sramR%u sramW%u resets%u descriptors%u expectedBytes%u initialByteReads%u\n",serial,result,(unsigned long long)machine.cpu.cycles,(unsigned long long)machine.timer.ticks,amcR,amcW,sramR,sramW,clockW,descriptors,expectedBytes,initialReads);
 dumpfile("sram.bin",sram.bytes,AMC_SRAM_SIZE);dumpfile("expected.bin",expected.bytes,AMC_SRAM_SIZE);dumpfile("predicted.bin",predicted,AMC_SRAM_SIZE);dumpfile("written.bin",written,AMC_SRAM_SIZE);dumpfile("registers.bin",machine.cpu.r,sizeof machine.cpu.r);
 machine.bus=original;release_machine();return result==2?0:result==1?10:0;
}
