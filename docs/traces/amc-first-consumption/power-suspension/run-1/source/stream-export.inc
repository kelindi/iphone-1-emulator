/* Observer artifacts only; no guest reads, DMA or engine changes. */
static void stream_gate(void){
 if(!prefix_saved||machine.cpu.cycles!=9873752646ull||machine.cpu.r[15]!=0xc0277fbc||engine.published_frames!=2||!engine.service||engine.queue_used!=65533||engine.copied_bytes!=66212||engine.decoded_bytes!=679)stop("fresh Stage T boundary differs",0,0,0);
 const char*saved=output;char path[1024];named_path(path,sizeof path,output,"stream-gate");output=path;
 dumpfile("sram.bin",sram.bytes,AMC_SRAM_SIZE);dumpfile("registers.bin",machine.cpu.r,sizeof machine.cpu.r);dumpfile("owned-queue.bin",engine.queue,engine.queue_used);
 dumpfile("init-written.bin",engine.init_writes,AMC_SRAM_SIZE);dumpfile("pcm-written.bin",engine.pcm_writes,AMC_SRAM_SIZE);dumpfile("host-written.bin",engine.host_writes,AMC_SRAM_SIZE);
 dumpfile("dma-copies.bin",dma_observed.records,dma_observed.count*sizeof(dma_record));dumpfile("dma-bytes.bin",dma_observed.bytes,dma_observed.used);
 output=saved;stream_gate_saved=true;
 printf("STREAM GATE cycles%llu pc%08x publications%u releases%u queued%zu copied%llu decoded%llu service%u; exact T state captured before newly allowed work\n",(unsigned long long)machine.cpu.cycles,machine.cpu.r[15],streamed.count,streamed.release_count,engine.queue_used,(unsigned long long)engine.copied_bytes,(unsigned long long)engine.decoded_bytes,engine.service);
}
static void stream_export(void){
 char path[1024];named_path(path,sizeof path,output,"stream.json");FILE*f=fopen(path,"w");if(!f)return;
 fprintf(f,"{\"published\":%u,\"released\":%u,\"readRecords\":%u,\"pendingGeneration\":%u,\"ipcPending\":%u,\"gateSaved\":%s,\"generations\":[",streamed.count,streamed.release_count,streamed.read_count,streamed.pending_generation,streamed.ipc_pending,stream_gate_saved?"true":"false");
 for(unsigned i=0;i<streamed.count;i++){
  stream_generation*g=&streamed.gen[i];char name[64];snprintf(name,sizeof name,"stream-pcm-%u.bin",i+1);dumpfile(name,g->reads.expected,4096);
  snprintf(name,sizeof name,"stream-seen-%u.bin",i+1);dumpfile(name,g->reads.seen,4096);snprintf(name,sizeof name,"stream-coverage-%u.bin",i+1);dumpfile(name,g->reads.coverage,4096);
  fprintf(f,"%s{\"ordinal\":%u,\"slot\":%u,\"published\":%llu,\"decoded\":%llu,\"copied\":%llu,\"samples\":%llu,\"events\":%u,\"mask\":%u,\"ipc\":%u,\"ack\":%u,\"released\":%u,\"pending\":%u,\"ipcCycles\":%llu,\"ackCycles\":%llu,\"releaseStart\":%llu,\"releaseCallback\":%llu,\"releaseEnd\":%llu,\"releasePC\":%u,\"releaseCPSR\":%u,\"releaseOpcode\":%u,\"afterPC\":%u,\"afterCPSR\":%u,\"releaseAddress\":%u,\"driver\":%u,\"index\":%u,\"covered\":%u,\"reads\":%u}",i?",":"",g->ordinal,g->slot,(unsigned long long)g->published,(unsigned long long)g->decoded,(unsigned long long)g->copied,(unsigned long long)g->samples,g->events,g->mask,g->ipc,g->ack,g->released,g->pending,(unsigned long long)g->ipc_cycles,(unsigned long long)g->ack_cycles,(unsigned long long)g->release_step.cycles,(unsigned long long)g->release_callback,(unsigned long long)g->release_end,g->release_pc,g->release_cpsr,g->release_opcode,g->after_pc,g->after_cpsr,g->release_address,g->driver,g->index,g->reads.covered,g->reads.reads);
 }
 fputs("]}\n",f);fclose(f);dumpfile("stream-reads.bin",streamed.ledger,streamed.read_count*sizeof(stream_read_record));
 printf("STREAM FINAL published%u released%u readRecords%u pending%u gate%u\n",streamed.count,streamed.release_count,streamed.read_count,streamed.pending_generation,stream_gate_saved);
}
