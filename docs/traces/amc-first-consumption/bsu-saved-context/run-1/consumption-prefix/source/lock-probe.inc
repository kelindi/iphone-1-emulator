static void lock_export(void){
 char path[1024];named_path(path,sizeof path,output,"lock.json");FILE*f=fopen(path,"w");if(!f)return;
 fprintf(f,"{\"prefixSaved\":%u,\"locked\":%u,\"dmaPaused\":%u,\"dmaActive\":%u,\"transport\":%u,\"inputPending\":%u,\"copied\":%llu,\"decoded\":%llu,\"queue\":%zu,\"current\":%u,\"start\":%u,\"words\":%u,\"copiedWords\":%u,\"events\":%u,\"mask\":%u,\"samples\":%llu,\"publications\":%llu,\"reads\":[",lock_prefix_saved,engine.bsu_locked,engine.dma_paused,engine.dma_active,engine.transport_enabled,engine.input_pending,(unsigned long long)engine.copied_bytes,(unsigned long long)engine.decoded_bytes,engine.queue_used,engine.dma_start+4*engine.copied_words,engine.dma_start,engine.dma_words,engine.copied_words,engine.events,engine.mask,(unsigned long long)engine.sample_units,(unsigned long long)engine.published_frames);
 for(unsigned i=0;i<lock_read_count;i++)fprintf(f,"%s{\"address\":%u,\"value\":%u,\"pc\":%u,\"cpsr\":%u,\"cycles\":%llu}",i?",":"",lock_reads[i].address,lock_reads[i].value,lock_reads[i].pc,lock_reads[i].cpsr,(unsigned long long)lock_reads[i].cycles);
 fputs("],\"receipts\":[",f);
 for(unsigned i=0;i<3;i++){const lock_receipt*r=&locks[i];fprintf(f,"%s{\"pending\":%u,\"retired\":%u,\"start\":%llu,\"callback\":%llu,\"end\":%llu,\"pc\":%u,\"cpsr\":%u,\"opcode\":%u,\"address\":%u,\"value\":%u,\"afterPC\":%u,\"afterCPSR\":%u}",i?",":"",r->pending,r->retired,(unsigned long long)r->start.cycles,(unsigned long long)r->callback,(unsigned long long)r->end,r->pc,r->cpsr,r->opcode,r->address,r->value,r->after_pc,r->after_cpsr);}
 uint8_t fields[8]={0},code[4]={0};bool available=engine.bsu_locked&&machine.cpu.r[15]==0xc0277dfc&&(machine.cpu.cpsr&63)==0x13&&machine.cpu.r[0]==verified_driver+0x184&&machine.cpu.r[5]==verified_driver+0x184&&amc_peek_ram(&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,verified_driver+0x1c4,fields,8)&&amc_peek_ram(&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,machine.cpu.r[15],code,4);
 if(!available){memset(fields,0,sizeof fields);memset(code,0,sizeof code);}dumpfile("locked-driver-fields.bin",fields,sizeof fields);dumpfile("locked-read-code.bin",code,sizeof code);
 fprintf(f,"],\"driverFieldsAvailable\":%u,\"driver\":%u,\"savePointer\":%u,\"pc\":%u,\"cpsr\":%u,\"cycles\":%llu}\n",available,verified_driver,machine.cpu.r[0],machine.cpu.r[15],machine.cpu.cpsr,(unsigned long long)machine.cpu.cycles);fclose(f);
}
static void lock_save_prefix(void){
 if(lock_prefix_saved||machine.cpu.cycles!=9874729042ull||machine.cpu.r[15]!=0xc0277ce8||machine.cpu.r[14]!=0xc0271ac4||streamed.count!=53||streamed.release_count!=51||engine.copied_bytes!=83304||engine.decoded_bytes!=17770||engine.queue_used!=65534||engine.bsu_locked||engine.dma_paused)stop("fresh U lock prefix differs",0,0,0);
 puts("LOCK PREFIX first actual unsupported U request preserved; model admission follows");
 const char*saved=output;char path[1024];named_path(path,sizeof path,output,"lock-prefix");output=path;
 /* Current live artifacts only; result remains ongoing, never forge old STOP. */
 write_report();stream_export();dumpfile("post-records.bin",post.records,post.count*sizeof(post_record));
 if(!transport_export(&machine,&destination,output,"stop",false))stop("lock prefix transport capture",0,0,0);
 output=saved;lock_prefix_saved=true;
}
static void lock_store_begin(uint32_t a,unsigned w,uint32_t v){
 unsigned index=a==0x38500400?0:a==0x38501000?1:2;
 if(w!=4||(index==0?v!=1:v!=0)||locks[index].pending||locks[index].retired||(index&&!locks[index-1].retired))stop("lock store order/value",a,v,w);
 if(!index)lock_save_prefix();uint8_t code[4];if(!amc_peek_ram(&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,machine.cpu.r[15],code,4))stop("lock store mapped code unavailable",a,v,w);
 uint32_t op=code[0]|(uint32_t)code[1]<<8|(uint32_t)code[2]<<16|(uint32_t)code[3]<<24;
 if(!in_guest||!lock_begin(&locks[index],&current_step,machine.cpu.cycles,machine.cpu.r[15],machine.cpu.cpsr,op,a,v))stop("lock store mapped instruction/receipt",a,v,w);
}
