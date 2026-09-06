static uint8_t save_copy_coverage[0xc000];
typedef struct {uint32_t address,width,value,pc,cpsr,opcode;uint64_t cycles;} save_copy_read;
static save_copy_read save_copy_reads[0xc000];static unsigned save_copy_count;
static struct {uint32_t off,value,pc,cpsr,opcode;uint64_t cycles;} save_reads[9];
static unsigned save_read_count;
static bool save_peek(uint32_t a,void*data,size_t n){return amc_peek_ram(&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,a,data,n);}
static void save_prefix(void){
 if(save_prefix_saved||machine.cpu.cycles!=9874729099ull||machine.cpu.r[15]!=0xc0277dfc||machine.cpu.r[14]!=0xc0271af8||(machine.cpu.cpsr&63)!=0x13||!locks[2].retired)stop("fresh V save prefix differs",0,0,0);
 puts("SAVE PREFIX actual V first save read before admission");
 const char*saved=output;char path[1024];named_path(path,sizeof path,output,"save-prefix");output=path;
 write_report();stream_export();lock_export();dumpfile("post-records.bin",post.records,post.count*sizeof(post_record));
 if(!transport_export(&machine,&destination,output,"stop",false))stop("save prefix transport capture",0,0,0);
 output=saved;save_prefix_saved=true;
}
static uint32_t save_read(uint32_t a,unsigned w){
 if(!save_prefix_saved)save_prefix();
 if(save_read_count>=9||!in_guest||(machine.cpu.cpsr&63)!=0x13)stop("save read mode/count",a,0,w);
 uint8_t code[4];if(!save_peek(machine.cpu.r[15],code,4))stop("save mapped read unavailable",a,0,w);
 uint32_t value=0;if(!bsu_save_read(&saving,&engine,a-0x38500000,w,&value))stop("invalid save resource/transaction",a,0,w);
 unsigned i=save_read_count++;save_reads[i].off=a-0x38500000;save_reads[i].value=value;save_reads[i].pc=machine.cpu.r[15];save_reads[i].cpsr=machine.cpu.cpsr;memcpy(&save_reads[i].opcode,code,4);save_reads[i].cycles=machine.cpu.cycles;
 printf("SAVE READ index%u offset%03x value%08x pc%08x cpsr%08x opcode%08x cycles%llu returned1\n",i,a-0x38500000,value,save_reads[i].pc,save_reads[i].cpsr,save_reads[i].opcode,(unsigned long long)save_reads[i].cycles);return value;
}
static void save_note_sram(uint32_t a,unsigned w,uint32_t v){
 if(saving.phase!=9)return;uint32_t off=a-AMC_SRAM_BASE,index;
 if(off>=0x28000&&off<0x2c000){if(w>0x2c000-off)stop("save copy boundary",a,v,w);index=off-0x28000;}
 else if(off>=0x14000&&off<0x1c000){if(w>0x1c000-off)stop("save copy boundary",a,v,w);index=0x4000+off-0x14000;}
 else return;
 if((w!=1&&w!=2&&w!=4)||save_copy_count>=0xc000||!in_guest||(machine.cpu.cpsr&63)!=0x13)stop("save copy reader cap/mode",a,v,w);
 uint8_t code[4];if(!save_peek(machine.cpu.r[15],code,4))stop("save copy code unavailable",a,v,w);
 for(unsigned i=0;i<w;i++){if((uint8_t)(v>>(8*i))!=saving.sram.bytes[off+i])stop("save copy actual byte differs",a,v,w);save_copy_coverage[index+i]=1;}
 save_copy_read*r=&save_copy_reads[save_copy_count++];r->address=a;r->width=w;r->value=v;r->pc=machine.cpu.r[15];r->cpsr=machine.cpu.cpsr;memcpy(&r->opcode,code,4);r->cycles=machine.cpu.cycles;
}
static void save_export(void){
 if(!save_prefix_saved)return;
 bool resource=bsu_save_valid(&saving,&engine);uint8_t context[64]={0},fields[8]={0},copies[0xc000]={0},code[4]={0};
 bool available=save_peek(verified_driver+0x184,context,64)&&save_peek(verified_driver+0x1c4,fields,8)&&save_peek(0xc02b6334,copies,0x4000)&&save_peek(0xc02ae334,copies+0x4000,0x8000)&&save_peek(machine.cpu.r[15],code,4);
 if(!available){memset(context,0,64);memset(fields,0,8);memset(copies,0,sizeof copies);memset(code,0,4);}
 dumpfile("saved-copy-coverage.bin",save_copy_coverage,sizeof save_copy_coverage);dumpfile("saved-copy-reads.bin",save_copy_reads,save_copy_count*sizeof(save_copy_read));
 dumpfile("saved-context.bin",context,64);dumpfile("saved-dma-fields.bin",fields,8);dumpfile("saved-sram-copies.bin",copies,sizeof copies);dumpfile("saved-stop-code.bin",code,4);
 char path[1024];named_path(path,sizeof path,output,"save.json");FILE*f=fopen(path,"w");if(!f)return;
 fprintf(f,"{\"prefixSaved\":%u,\"owned\":%u,\"phase\":%u,\"resourceUnchanged\":%u,\"fieldsAvailable\":%u,\"generation\":%llu,\"decoded\":%llu,\"copied\":%llu,\"queue\":%zu,\"pc\":%u,\"cpsr\":%u,\"cycles\":%llu,\"reads\":[",save_prefix_saved,saving.owned,saving.phase,resource,available,(unsigned long long)engine.generation,(unsigned long long)engine.decoded_bytes,(unsigned long long)engine.copied_bytes,engine.queue_used,machine.cpu.r[15],machine.cpu.cpsr,(unsigned long long)machine.cpu.cycles);
 for(unsigned i=0;i<save_read_count;i++)fprintf(f,"%s{\"off\":%u,\"value\":%u,\"pc\":%u,\"cpsr\":%u,\"opcode\":%u,\"cycles\":%llu}",i?",":"",save_reads[i].off,save_reads[i].value,save_reads[i].pc,save_reads[i].cpsr,save_reads[i].opcode,(unsigned long long)save_reads[i].cycles);
 fputs("]}\n",f);fclose(f);
}
