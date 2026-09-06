/* Observation-only continuation. It never supplies input/configuration to the
 * engine, never changes guest results, and never restores unsaved FAAD state. */
static uint8_t compare_buffer[AMC_SRAM_SIZE];
static void named_path(char*out,size_t size,const char*directory,const char*name){
 if(snprintf(out,size,"%s/%s",directory,name)>=(int)size)stop("artifact path bound",0,0,0);
}
static void reference_equal(const char*name,const void*data,size_t bytes){
 char path[1024];named_path(path,sizeof path,reference,name);
 if(bytes>sizeof compare_buffer||!load_exact(path,compare_buffer,bytes)||memcmp(data,compare_buffer,bytes))stop("075 reference mismatch",0,0,0);
}
static void first_file(const char*name,const void*data,size_t bytes){
 reference_equal(name,data,bytes);char path[128];snprintf(path,sizeof path,"first/%s",name);dumpfile(path,data,bytes);
}
static void copy_first_file(const char*name,size_t bytes){
 static uint8_t data[AMC_SRAM_SIZE];char path[1024];named_path(path,sizeof path,output,name);
 if(bytes>sizeof data||!load_exact(path,data,bytes))stop("first artifact read",0,0,0);
 first_file(name,data,bytes);
}
static void first_boundary(void){
 if(machine.cpu.cycles!=9873234585ull||machine.timer.ticks!=1756770461ull||command_cycles!=9873013642ull||first_input_cycles!=9873233416ull||
    service_calls!=220944||engine.decoded_frames!=2||engine.decoded_bytes!=348||engine.copied_bytes!=16384||engine.queue_used!=16036||
    engine.sample_units!=2048||engine.events!=0x600||engine.mask!=0x2000||!engine.input_pending||amcR!=3||amcW!=41||sramR!=5565||sramW!=17687||initialReads!=11136)stop("075 counter boundary differs",0,0,0);
 first_file("sram.bin",sram.bytes,AMC_SRAM_SIZE);first_file("expected.bin",expected.bytes,AMC_SRAM_SIZE);first_file("predicted.bin",predicted,AMC_SRAM_SIZE);first_file("written.bin",written,AMC_SRAM_SIZE);
 first_file("init-written.bin",engine.init_writes,AMC_SRAM_SIZE);first_file("pcm-written.bin",engine.pcm_writes,AMC_SRAM_SIZE);first_file("host-written.bin",engine.host_writes,AMC_SRAM_SIZE);
 first_file("registers.bin",machine.cpu.r,sizeof machine.cpu.r);first_file("owned-queue.bin",engine.queue,engine.queue_used);first_file("first-pcm.bin",sram.bytes+AMC_OUTPUT0,CONSUME_BYTES);
 const char*inputs[]={"input-final.bin","input-rounded.bin","input-virtual.bin","input-capacity.bin","packet-data.bin","input-driver-prefix.bin","input-descriptor-prefix.bin","packet-descriptor-prefix.bin","input-vtable-prefix.bin"};
 const unsigned sizes[]={16384,16384,16384,65536,18732,0x180,0x80,0x80,0x104};
 for(unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++)copy_first_file(inputs[i],sizes[i]);
 const char*uploads[]={"upload/sram.bin","upload/expected.bin","upload/predicted.bin","upload/written.bin"};
 for(unsigned i=0;i<4;i++)copy_first_file(uploads[i],AMC_SRAM_SIZE);
 printf("ENGINE state configured%u copiedBytes%llu copiedWords%u queueBytes%zu decodedBytes%llu decodedFrames%llu publishedFrames%llu sampleUnits%llu events%08x mask%08x inputPending%u\n",engine.configured,(unsigned long long)engine.copied_bytes,engine.copied_words,engine.queue_used,(unsigned long long)engine.decoded_bytes,(unsigned long long)engine.decoded_frames,(unsigned long long)engine.published_frames,(unsigned long long)engine.sample_units,engine.events,engine.mask,engine.input_pending);
 printf("BOUNDARY result5 interruptedInstruction0 startCycles%llu commandCycles%llu firstInputCycles%llu totalRetired%llu\n",(unsigned long long)start_cycles,(unsigned long long)command_cycles,(unsigned long long)first_input_cycles,(unsigned long long)(machine.cpu.cycles-start_cycles));
 fflush(stdout);char src[1024],dst[1024];named_path(src,sizeof src,output,"trace.log");named_path(dst,sizeof dst,output,"first/trace.log");
 FILE*from=fopen(src,"rb"),*to=fopen(dst,"wb");if(!from||!to)stop("prefix trace open",0,0,0);
 char block[4096];size_t count;while((count=fread(block,1,sizeof block,from)))if(fwrite(block,1,count,to)!=count)stop("prefix trace write",0,0,0);
 if(ferror(from)||fclose(from)||fclose(to))stop("prefix trace close",0,0,0);
 first_saved=true;first_publication_cycles=machine.cpu.cycles;consume_begin(&consumed,sram.bytes+AMC_OUTPUT0);
 uint32_t targets[]={0xc0271fa4,0xc0273898,0xc0271ee0,0xc0271f30,0xc027249c,0xc027251c,0xc02726d4,0xc0273a50,0xc0273a54,0xc0278084,0xc01533c2,0x3123f7f0,0x3123cdc8,0x3123ce18,0x3123f95c,0xc0272224};
 if(!s5l8900_set_pre_step_hook(&machine,continuation_hook,NULL,targets,sizeof targets/sizeof targets[0]))stop("continuation hook install",0,0,0);
 puts("CONTINUE frozen075 boundary matched; real pending IRQ and normal guest execution follow");
}
static unsigned first_owner(void){return sram.bytes[AMC_HEADER+8]|(unsigned)sram.bytes[AMC_HEADER+9]<<8;}
static void continuation_read(uint32_t address,unsigned width,uint32_t value){
 if(prefix_saved)return;
 unsigned owner=first_owner();consume_result r=consume_read(&consumed,address,width,value,true,owner);
 if(r==CONSUME_REJECT)stop("PCM read observation mismatch/cap",address,value,width);
 if(r==CONSUME_ACCEPT){
  if(consumed.reads==1){
   if(address!=CONSUME_BASE||width!=4||!destination_begin(&destination,&machine.cpu,machine.ram,machine.ram_base,machine.ram_size))stop("first PCM destination observer rejected",address,value,width);
   if(!transport_export(&machine,&destination,output,"copy",false))stop("transport copy export failed",0,0,0);
   destination_export();printf("DESTINATION BEGIN cycles%llu source%08x destination%08x readerPC%08x cpsr%08x savedLR%08x callerWindowAvailable%u ttbr0%08x ttbr1%08x ttbcr%08x dacr%08x\n",(unsigned long long)destination.cycles,destination.regs[1],destination.regs[0],destination.regs[15],destination.cpsr,destination.regs[14],destination.caller_available,destination.ttbr0,destination.ttbr1,destination.ttbcr,destination.dacr);
  }
  read_record*record=&read_ledger[consumed.reads-1];*record=(read_record){address,width,value,machine.cpu.r[15],machine.cpu.cpsr,owner,machine.cpu.cycles};
  if(consumed.reads<=8||consumed.covered==CONSUME_BYTES)printf("PCM READ address%08x width%u value%08x pc%08x cpsr%08x covered%u\n",address,width,value,record->pc,record->cpsr,consumed.covered);
 }
}
static bool continuation_hook(void*ctx){(void)ctx;uint32_t pc=machine.cpu.r[15];
 if(prefix_saved&&post_target(pc)){if(!post_begin(&post,&machine.cpu,machine.ram,machine.ram_base,machine.ram_size))stop("post observer binding/bounds",pc,post.count,4);return false;}
 if(order_target(pc)){
  if(pc==0x3123f7f0&&!user_boundary_saved){
   if(machine.cpu.cycles!=9873717675ull||machine.timer.ticks!=1756777496ull||ordering.count!=1)stop("Stage O user attempt prefix differs",pc,ordering.count,4);
   later_boundary(6);dumpfile("user-return/dma-copies.bin",dma_observed.records,dma_observed.count*sizeof(dma_record));dumpfile("user-return/dma-bytes.bin",dma_observed.bytes,dma_observed.used);
   user_boundary_saved=true;
  }
  if(order_begin(&ordering,&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,verified_driver)!=ORDER_OK)stop("ordering observer invalid/cap/peek",pc,ordering.count,4);
  return false;
 }

 printf("CONSUMER HOOK pc%08x r0%08x r1%08x r2%08x r3%08x cpsr%08x\n",pc,machine.cpu.r[0],machine.cpu.r[1],machine.cpu.r[2],machine.cpu.r[3],machine.cpu.cpsr);
 if(pc==0xc0278084)stop("second input requires new witness",machine.cpu.r[0],machine.cpu.r[1],4);
 if(pc==0xc0271ee0&&machine.cpu.r[0]==verified_driver&&machine.cpu.r[1]==1&&machine.cpu.r[2]<=1&&machine.cpu.r[3]==2048){
  if((machine.cpu.cpsr&63)!=0x13||!stream_ipc(&streamed,machine.cpu.r[2],machine.cpu.cycles,false))stop("stream IPC entry generation",pc,0,4);
 }
 if(pc==0xc0271f30&&streamed.ipc_pending){unsigned generation=streamed.ipc_pending;if(machine.cpu.r[0]||(machine.cpu.cpsr&63)!=0x13||!stream_ipc(&streamed,0,machine.cpu.cycles,true))stop("stream IPC completion differs",pc,machine.cpu.r[0],4);if(prefix_saved)printf("STREAM IPC generation%u cycles%llu return0\n",generation,(unsigned long long)machine.cpu.cycles);}
 if(pc==0xc0271ee0){ipc_pending=machine.cpu.r[0]==verified_driver&&machine.cpu.r[1]==1&&machine.cpu.r[2]==0&&machine.cpu.r[3]==2048;}
 if(pc==0xc0271f30&&ipc_pending){if(machine.cpu.r[0])stop("IPC notification failed",pc,machine.cpu.r[0],4);ipc_complete=true;ipc_pending=false;puts("HANDOFF IPC type1 index0 count2048 original kernel return0");}
 if(pc==0xc027251c){release_driver=machine.cpu.r[0];release_index=machine.cpu.r[1];release_path=release_driver==verified_driver&&release_index==0;}
 if(pc==0xc02726d4)release_path=false;
 if(pc==0xc0273a50){uint32_t physical_source;
  copy_dst=machine.cpu.r[0];copy_src=machine.cpu.r[1];copy_count=machine.cpu.r[2];copy_cpsr=machine.cpu.cpsr;
  copy_pending=copy_count==CONSUME_BYTES&&amc_peek_translate(&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,copy_src,&physical_source)&&physical_source==CONSUME_BASE;
  printf("COPY call pcc0273a50 source%08x destination%08x bytes%u cpsr%08x supported%u\n",copy_src,copy_dst,copy_count,copy_cpsr,copy_pending);
 }
 if(pc==0xc0273a54&&copy_pending){uint8_t bytes[CONSUME_BYTES];
  if(!amc_peek_ram(&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,copy_dst,bytes,sizeof bytes))stop("copy destination inspection unavailable",copy_dst,0,4);
  if(memcmp(bytes,consumed.expected,sizeof bytes))stop("copy destination differs",copy_dst,0,4);
  dumpfile("copy-destination.bin",bytes,sizeof bytes);copy_complete=true;copy_pending=false;
  printf("HANDOFF COPY return pcc0273a54 source%08x destination%08x bytes%u cpsr%08x exact1\n",copy_src,copy_dst,copy_count,machine.cpu.cpsr);
 }
 return false;
}
static void continuation_done(void){
 if(!release_finish_valid(&current_step,release_cycles,release_pc,release_cpsr,release_instruction,machine.cpu.cycles,machine.cpu.r[15],machine.cpu.cpsr,in_guest))stop("release store not retired",0,0,0);
 printf("RELEASE RETIRED startCycles%llu callbackCycles%llu endCycles%llu startPC%08x afterPC%08x startCPSR%08x afterCPSR%08x instruction%08x emuRunReturned%u inGuest%u\n",(unsigned long long)current_step.cycles,(unsigned long long)release_cycles,(unsigned long long)machine.cpu.cycles,current_step.pc,machine.cpu.r[15],current_step.cpsr,machine.cpu.cpsr,release_instruction,current_step.returned,in_guest);
 if(!consume_complete(&consumed)||(!ipc_complete&&!copy_complete)||!output_ack||!output_enabled||!service_requested||!release_path||
    (release_pc!=0xc0272690&&release_pc!=0xc02726a8&&release_pc!=0xc02726c4))stop("release without complete handoff evidence",release_pc,consumed.covered,0);
 if(memcmp(sram.bytes+AMC_OUTPUT0,consumed.expected,CONSUME_BYTES)||first_owner()!=1)stop("released PCM changed",0,0,0);
 if(!destination_finish(&destination,machine.ram,machine.ram_base,machine.ram_size,consumed.expected,machine.cpu.cycles))stop("destination copy bytes/addressspace differ",destination.regs[0],0,4096);
 if(!transport_export(&machine,&destination,output,"release",true))stop("transport release export failed",0,0,0);
 destination_export();printf("DESTINATION VERIFIED source%08x destination%08x bytes4096 captureCycles%llu finishCycles%llu retainedUserContext1\n",destination.regs[1],destination.regs[0],(unsigned long long)destination.cycles,(unsigned long long)destination.finished_cycles);
 printf("PASS first PCM read and ownership returned cycles%llu releaseStoreCycles%llu pc%08x reads%u covered%u ipc%u copy%u published%llu; no next guest instruction or service\n",(unsigned long long)machine.cpu.cycles,(unsigned long long)release_cycles,release_pc,consumed.reads,consumed.covered,ipc_complete,copy_complete,(unsigned long long)engine.published_frames);result=6;
}

static void save_consumption_prefix(void){
 const char*original_output=output;static char directory[1024];named_path(directory,sizeof directory,output,"consumption-prefix");
 output=directory;write_report();output=original_output;
 fflush(stdout);char src[1024],dst[1024];named_path(src,sizeof src,output,"trace.log");named_path(dst,sizeof dst,directory,"trace.log");
 FILE*from=fopen(src,"rb"),*to=fopen(dst,"wb");if(!from||!to)stop("consumption prefix trace open",0,0,0);
 char block[4096];size_t count;while((count=fread(block,1,sizeof block,from)))if(fwrite(block,1,count,to)!=count)stop("consumption prefix trace write",0,0,0);
 if(ferror(from)||fclose(from)||fclose(to))stop("consumption prefix trace close",0,0,0);
 /* Reset only observer control, never device service, queue, ownership or IRQ. */
 prefix_saved=true;release_pending=false;result=0;
 uint32_t targets[]={0xc0271fa4,0xc0273898,0xc0271ee0,0xc0271f30,0xc027249c,0xc027251c,0xc02726d4,0xc0273a50,0xc0273a54,0xc0278084,0x312288f0,0x3122c174,0x31236650,0x312366a0,0x31236734,0x312367f8};
 if(!s5l8900_set_pre_step_hook(&machine,continuation_hook,NULL,targets,16))stop("post hook install",0,0,0);
 printf("POST START cycles%llu pc%08x cpsr%08x engineService%u events%08x nextOutput%u owner%u published%llu queue%zu; first receipt frozen no device change\n",(unsigned long long)machine.cpu.cycles,machine.cpu.r[15],machine.cpu.cpsr,engine.service,engine.events,engine.next_output,first_owner(),(unsigned long long)engine.published_frames,engine.queue_used);
}
