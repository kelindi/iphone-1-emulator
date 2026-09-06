static void fetch_export(void){
 char path[1024];named_path(path,sizeof path,output,"fetch.json");FILE*f=fopen(path,"w");if(!f)stop("fetch report path",0,0,0);
 fprintf(f,"{\"count\":%u,\"faults\":%u,\"pending\":%u,\"mapped\":%u,\"cycles\":%llu,\"pc\":%u,\"cpsr\":%u,\"records\":[",fetched.count,fetched.faults,fetched.pending,fetched.mapped,(unsigned long long)machine.cpu.cycles,machine.cpu.r[15],machine.cpu.cpsr);
 for(unsigned i=0;i<fetched.count+(fetched.pending?1:0);i++){fetch_receipt*r=&fetched.records[i];fprintf(f,"%s{\"start\":%llu,\"end\":%llu,\"pc\":%u,\"cpsr\":%u,\"ifsr\":%u,\"ifar\":%u,\"irq\":%u,\"fiq\":%u,\"sctlr\":%u,\"fetchPresent\":%u,\"fetchBlock\":%u,\"fetchGeneration\":%u,\"tlbGeneration\":%u,\"fetchPrivileged\":%u,\"afterPC\":%u,\"afterCPSR\":%u,\"afterIFSR\":%u,\"afterIFAR\":%u,\"afterLR\":%u,\"afterSPSR\":%u,\"kind\":%u,\"returned\":%u}",i?",":"",(unsigned long long)r->start,(unsigned long long)r->end,r->pc,r->cpsr,r->ifsr,r->ifar,r->irq,r->fiq,r->sctlr,r->fetch_present,r->fetch_block,r->fetch_gen,r->tlb_gen,r->fetch_priv,r->after_pc,r->after_cpsr,r->after_ifsr,r->after_ifar,r->after_lr,r->after_spsr,r->kind,r->returned);}
 fputs("]}\n",f);fclose(f);
}
static void fetch_prefix(void){
 if(fetch_prefix_saved||machine.cpu.cycles!=9874747384ull||machine.cpu.r[15]!=0x31236650||bound.reason!=BIND_FAULT||!amc_suspend_valid(&suspended,&saving,&engine))stop("fresh Z prefetch prefix differs",0,0,0);
 binding_export();puts("FETCH PREFIX actual Z before releasing original CPU fetch");
 const char*saved=output;char path[1024];named_path(path,sizeof path,output,"fetch-prefix");output=path;
 write_report();stream_export();suspend_export();binding_export();dumpfile("post-records.bin",post.records,post.count*sizeof(post_record));
 if(!transport_export(&machine,&destination,output,"stop",false))stop("fetch prefix transport",0,0,0);output=saved;fetch_prefix_saved=true;
}
static bool fetch_entry(void){
 (void)post_binding_probe(&post,&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,&bound);
 if(bound.reason==BIND_OK)for(unsigned i=1;i<4;i++)binding_read(&bound.walk[i],&machine.cpu,machine.ram,machine.ram_base,machine.ram_size,machine.cpu.r[15],i&1?ARM_ACCESS_FETCH:ARM_ACCESS_READ,i>=2);
 unsigned decision=fetch_prepare(&fetched,&machine.cpu,&bound);
 if(!fetch_prefix_saved){if(decision!=FETCH_ALLOW)stop("initial real fetch refused",0,decision,0);fetch_prefix();}
 else {const char*saved=output;char path[1024],name[32];snprintf(name,sizeof name,"fetch-attempt-%u",fetched.count);named_path(path,sizeof path,output,name);output=path;binding_export();output=saved;}
 if(decision==FETCH_MAPPED){puts("FETCH MAPPED original window available; target instruction not executed");stop("mapped original retry before instruction",machine.cpu.r[15],0,4);}
 if(decision!=FETCH_ALLOW)stop("real fetch context/cache/fault refusal",machine.cpu.r[15],bound.reason,4);
 printf("FETCH ALLOW index%u pc%08x cpsr%08x cycles%llu liveCacheHit0; original CPU handles interrupts and fetch\n",fetched.count,machine.cpu.r[15],machine.cpu.cpsr,(unsigned long long)machine.cpu.cycles);return false;
}
static void fetch_after_step(void){
 if(!fetched.pending)return;if(!fetch_finish(&fetched,&machine.cpu,&current_step,in_guest))stop("real fetch exception receipt differs",0,0,0);
 fetch_receipt*r=&fetched.records[fetched.count-1];printf("FETCH RETURN index%u kind%u start%llu end%llu pc%08x cpsr%08x afterPC%08x afterCPSR%08x ifsr%08x ifar%08x lr%08x spsr%08x emuRunReturned1 targetInstructionRetired0\n",fetched.count-1,r->kind,(unsigned long long)r->start,(unsigned long long)r->end,r->pc,r->cpsr,r->after_pc,r->after_cpsr,r->after_ifsr,r->after_ifar,r->after_lr,r->after_spsr);
}
