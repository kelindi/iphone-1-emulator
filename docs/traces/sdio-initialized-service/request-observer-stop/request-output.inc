static bool request_outputs(checkpoint_phone_t*p,const runtime_t*r,const char*dir){
 if(!positive_mode)return true;bool ok=dump(dir,"virtual-frame.bin",request.virtual_bytes,320)&&dump(dir,"virtual-trigger.bin",trigger_virtual,320)&&dump(dir,"physical-source.bin",request.physical_bytes,320);
 for(unsigned i=0;i<2;i++){char name[32];snprintf(name,sizeof name,"runtime-slot%u.bin",i);if(!r->slots[i].allocation||r->slots[i].capacity!=320||!dump(dir,name,r->slots[i].allocation,352))ok=false;}
 char path[512];snprintf(path,sizeof path,"%s/request.json",dir);FILE*f=fopen(path,"wbx");if(!f)return false;sdio_t*s=&p->device;
 fputs("{\n",f);
 #define FIELD(name,value) fprintf(f,"\""name"\":%"PRIu64",\n",(uint64_t)(value))
 FIELD("passed",request_final(p,r));FIELD("observerInvalid",request_bad);FIELD("prefix",request.prefix);FIELD("prefixTransactions",prefix_transactions);FIELD("prefixInstructions",prefix_instructions);
 FIELD("prefixCycles",request.prefix_cycles);FIELD("reg6Cycles",request.reg6_cycles);FIELD("returnCycles",request.return_cycles);FIELD("overwriteCycles",request.overwrite_cycles);FIELD("verifyCycles",request.verify_cycles);FIELD("setupCycles",request.setup_cycles);FIELD("builderCycles",request.builder_cycles);FIELD("frameCycles",request.frame_cycles);FIELD("triggerCycles",request.trigger_cycles);
 FIELD("reg6Value",request.reg6_value);FIELD("ignoredWrapperReturn",request.ignored_return);FIELD("driver",driver_address);FIELD("pulldowns",pulldowns);
 FIELD("hostBlockObject",host_block_object);FIELD("hostBlockCommitted",host_block_committed);FIELD("hostBlockCycles",host_block_cycles);FIELD("setupCardBlock",setup_card_block);
 FIELD("setupCCCR",setup_cccr);FIELD("setupCardMask",setup_card_mask);FIELD("setupControllerMask",setup_controller_mask);
 FIELD("object",frame_object);FIELD("virtual",frame_virtual);FIELD("physical",frame_physical);FIELD("capacity",frame_capacity);FIELD("offset",frame_offset);FIELD("logical",frame_logical);FIELD("opcode",request.opcode);FIELD("sequence",request.sequence);
 FIELD("argument",request.argument);FIELD("descriptor",request.descriptor);FIELD("source",request.source);FIELD("blockLength",request.block_length);FIELD("blockCount",request.block_count);FIELD("cardBlockSize",request.card_block_size);FIELD("requestCount",request_count);
 FIELD("triggerPC",request.trigger_pc);FIELD("triggerCPSR",request.trigger_cpsr);FIELD("previousPC",previous_pc);FIELD("previousCPSR",previous_cpsr);FIELD("lastCount",last_count);FIELD("lastInstructionOK",last_ok);FIELD("finalPC",p->machine->cpu.r[15]);FIELD("finalCPSR",p->machine->cpu.cpsr);FIELD("finalCycles",p->machine->cpu.cycles);
 FIELD("busy",s->busy);FIELD("now",p->machine->timer.ticks);FIELD("deadline",s->deadline);FIELD("pendingCommand",s->pending_command);FIELD("pendingArgument",s->pending_argument);
 FIELD("triggerCompletions",request.completions);FIELD("finalCompletions",s->completions);FIELD("triggerDataCompletions",request.data_completions);FIELD("finalDataCompletions",s->data_completions);FIELD("triggerDataStarts",request.data_starts);FIELD("finalDataStarts",s->data_starts);FIELD("dataArmed",s->data.armed);FIELD("dataActive",s->data.active);
 FIELD("triggerEvents",request.events);FIELD("finalEvents",s->events);FIELD("triggerIRQ",request.irq_pending);FIELD("finalIRQ",s->irq_pending);FIELD("endpointUsed",s->endpoint.used);FIELD("mainPhase",s->main_model.phase);
 FIELD("slot0Phase",r->slots[0].phase);FIELD("slot1Phase",r->slots[1].phase);FIELD("slot0Used",r->slots[0].used);FIELD("slot1Used",r->slots[1].used);FIELD("runtimeReady",runtime_ready(r));
 #undef FIELD
 fputs("\"builders\":[",f);for(unsigned i=0;i<2;i++){builder_t*b=&builders[i];fprintf(f,"%s{\"entered\":%u,\"finished\":%u,\"driver\":%u,\"args\":[%u,%u,%u],\"sequence\":%u,\"object\":%u,\"buffer\":%u,\"cycles\":%"PRIu64"}",i?",":"",b->entered,b->finished,b->driver,b->args[0],b->args[1],b->args[2],b->sequence,b->object,b->buffer,b->cycles);}
 fputs("],\n\"scope\":\"Original command source and retired request only; source RAM observation is not a card transfer. No response/DMA, opcode execution, card CPU or network interface.\"\n}\n",f);
 if(fclose(f))ok=false;return ok;
}
