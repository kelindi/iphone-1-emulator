/* Plain controller observation never calls a live MMIO read. */
static bool gate_clock(uint32_t*out){
 for(unsigned i=0;i<machine.stub_count;i++)if(machine.stubs[i].base==0x3c500000&&machine.stubs[i].regs&&machine.stubs[i].nregs>0x48/4){*out=machine.stubs[i].regs[0x48/4];return true;}return false;
}
static void suspend_prefix(void){
 if(power_prefix_saved||machine.cpu.cycles!=9874735590ull||machine.cpu.r[15]!=0xc0411f3c||machine.cpu.r[14]!=0xc0411de4||(machine.cpu.cpsr&63)!=0x13||!bsu_save_valid(&saving,&engine))stop("fresh X suspension prefix differs",0,0,0);
 puts("SUSPEND PREFIX actual X ONCTRL request before forwarding; domains0/1 already ungated");
 const char*saved=output;char path[1024];named_path(path,sizeof path,output,"power-prefix");output=path;
 write_report();stream_export();save_export();dumpfile("post-records.bin",post.records,post.count*sizeof(post_record));
 if(!transport_export(&machine,&destination,output,"stop",false))stop("suspension prefix transport capture",0,0,0);
 output=saved;power_prefix_saved=true;
}
static void gate_writer(void*c,uint32_t a,uint32_t v){rawwrite(c,a,4,v);}
static void suspend_write(void*c,uint32_t a,unsigned w,uint32_t v){
 if(!power_prefix_saved)suspend_prefix();
 uint32_t clock,opcode;if(!gate_clock(&clock)||!save_peek(machine.cpu.r[15],&opcode,4))stop("suspension controller/code unavailable",a,v,w);
 unsigned i=suspended.phase;gate_receipt receipt={0};
 if(i>=4||!gate_begin(&receipt,&current_step,machine.cpu.cycles,machine.cpu.r[15],machine.cpu.cpsr,opcode,a,v))stop("suspension store receipt unavailable",a,v,w);
 uint32_t power=machine.power.state;
 if(!amc_suspend_write(&suspended,&saving,&engine,clock,power,a,w,v,gate_writer,c))stop("unsupported suspension transition",a,v,w);
 gates[i]=receipt;uint32_t after;if(!gate_clock(&after)||after!=suspended.clock||machine.power.state!=suspended.power)stop("controller applied state differs",a,v,w);
 printf("SUSPEND WRITE index%u address%08x value%08x clockBefore%08x clockAfter%08x powerBefore%08x powerAfter%08x pc%08x cpsr%08x opcode%08x cycles%llu owned%u active%u\n",i,a,v,clock,after,power,machine.power.state,receipt.pc,receipt.cpsr,opcode,(unsigned long long)receipt.callback,suspended.owned,engine.initialized);
}
static void suspend_before_step(void){
 if(!suspended.owned||machine.cpu.r[15]!=0xc0272640)return;
 uint32_t opcode,previous;
 if(driver_state.retired||driver_state.pending||suspended.phase!=4||machine.cpu.r[4]!=verified_driver||machine.cpu.r[3]!=5||!save_peek(machine.cpu.r[15],&opcode,4)||opcode!=0xe58430e0||!save_peek(verified_driver+0xe0,&previous,4))stop("driver suspended state observation differs",0,0,0);
 if(!gate_begin(&driver_state,&current_step,current_step.cycles+1,current_step.pc,current_step.cpsr,opcode,verified_driver+0xe0,5))stop("driver suspended store staging differs",0,0,0);
 driver_state_before=previous;
}
static void suspend_after_step(void){
 for(unsigned i=0;i<4;i++)if(gates[i].pending){
  gate_receipt*r=&gates[i];if(!gate_finish(r,&current_step,machine.cpu.cycles,machine.cpu.r[15],machine.cpu.cpsr,in_guest))stop("suspension write retirement differs",r->address,r->value,4);
  printf("SUSPEND RETIRED index%u start%llu callback%llu end%llu pc%08x cpsr%08x opcode%08x address%08x value%08x afterPC%08x afterCPSR%08x emuRunReturned1\n",i,(unsigned long long)r->step.cycles,(unsigned long long)r->callback,(unsigned long long)r->end,r->pc,r->cpsr,r->opcode,r->address,r->value,r->after_pc,r->after_cpsr);
 }
 if(driver_state.pending){uint32_t actual;if(!gate_finish(&driver_state,&current_step,machine.cpu.cycles,machine.cpu.r[15],machine.cpu.cpsr,in_guest)||!save_peek(driver_state.address,&actual,4)||actual!=5)stop("driver suspended state not retired",driver_state.address,0,4);
  printf("SUSPEND DRIVER stateBefore%u stateAfter%u address%08x pc%08x cpsr%08x opcode%08x start%llu end%llu afterPC%08x afterCPSR%08x emuRunReturned1\n",driver_state_before,actual,driver_state.address,driver_state.pc,driver_state.cpsr,driver_state.opcode,(unsigned long long)driver_state.step.cycles,(unsigned long long)driver_state.end,driver_state.after_pc,driver_state.after_cpsr);
 }
}
static void suspend_export(void){
 if(!power_prefix_saved)return;
 uint32_t clock=0;bool controller=gate_clock(&clock);bool valid=suspended.owned?amc_suspend_valid(&suspended,&saving,&engine):bsu_save_valid(&saving,&engine);
 const amc_engine*r=suspended.owned?&suspended.retained:&engine;
 /* These are live retained resources, not copies read from earlier artifacts. */
 dumpfile("suspended-sram.bin",r->sram->bytes,AMC_SRAM_SIZE);dumpfile("suspended-queue.bin",r->queue,r->queue_used);
 uint32_t actual_driver=0;bool state_available=save_peek(verified_driver+0xe0,&actual_driver,4);
 uint32_t scalars[]={r->initialized,r->configured,r->service,r->dma_active,r->input_pending,r->events,r->mask,r->input_mask,r->staged_address,r->staged_words,r->dma_start,r->dma_words,r->copied_words,r->transport_enabled,r->bsu_locked,r->dma_paused,r->next_output,r->setup_phase,r->input_phase,r->session_phase};dumpfile("suspended-control.bin",scalars,sizeof scalars);
 char path[1024];named_path(path,sizeof path,output,"suspend.json");FILE*f=fopen(path,"w");if(!f)stop("suspension export unavailable",0,0,0);
 fprintf(f,"{\"phase\":%u,\"owned\":%u,\"resourcesValid\":%u,\"controllerAvailable\":%u,\"clock\":%u,\"power\":%u,\"activeInitialized\":%u,\"activeConfigured\":%u,\"activeDecoder\":%u,\"activeQueue\":%u,\"activeIRQ\":%u,\"activeEvents\":%u,\"activeMask\":%u,\"activeService\":%u,\"activeDMA\":%u,\"activeIdleEnabled\":%u,\"activeResetComplete\":%u,\"driverStateAvailable\":%u,\"driverState\":%u,\"driverStoreRetired\":%u,\"generation\":%llu,\"copied\":%llu,\"decoded\":%llu,\"queue\":%zu,\"suspendedBoundaries\":%llu,\"pc\":%u,\"cpsr\":%u,\"cycles\":%llu}\n",suspended.phase,suspended.owned,valid,controller,clock,machine.power.state,engine.initialized,engine.configured,engine.decoder!=NULL,engine.queue!=NULL,amc_engine_irq(&engine),engine.events,engine.mask,engine.service,engine.dma_active,idle.enabled,idle.de_complete,state_available,actual_driver,driver_state.retired,(unsigned long long)r->generation,(unsigned long long)r->copied_bytes,(unsigned long long)r->decoded_bytes,r->queue_used,(unsigned long long)suspended_boundaries,machine.cpu.r[15],machine.cpu.cpsr,(unsigned long long)machine.cpu.cycles);fclose(f);
 printf("SUSPEND FINAL phase%u owned%u resourcesValid%u activeInitialized%u activeIRQ%u clock%08x power%08x boundaries%llu\n",suspended.phase,suspended.owned,valid,engine.initialized,amc_engine_irq(&engine),clock,machine.power.state,(unsigned long long)suspended_boundaries);
}
