static void binding_export(void){
 char path[1024];named_path(path,sizeof path,output,"binding.json");FILE*f=fopen(path,"w");if(!f)stop("binding report unavailable",0,0,0);
 const binding_diagnostic*d=&bound;
 fprintf(f,"{\"attempted\":%u,\"reason\":%u,\"pc\":%u,\"cpsr\":%u,\"cycles\":%llu,\"count\":%u,\"pending\":%u,\"target\":%u,\"expectedAvailable\":%u,\"actualFetchExecuted\":false,\"step\":{\"cycles\":%llu,\"pc\":%u,\"cpsr\":%u,\"active\":%u,\"returned\":%u,\"inGuest\":%u},\"registers\":[",d->attempted,d->reason,d->pc,d->cpsr,(unsigned long long)d->cycles,d->count,d->pending,d->target,d->expected_available,(unsigned long long)current_step.cycles,current_step.pc,current_step.cpsr,current_step.active,current_step.returned,in_guest);
 for(unsigned i=0;i<16;i++)fprintf(f,"%s%u",i?",":"",d->regs[i]);
 fprintf(f,"],\"cp15\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\"expectedHex\":\"",d->sctlr,d->ttbr0,d->ttbr1,d->ttbcr,d->dacr,d->context_id,d->dfsr,d->ifsr,d->dfar,d->ifar);
 for(unsigned i=0;i<32;i++)fprintf(f,"%02x",d->expected[i]);fputs("\",\"walks\":[",f);
 for(unsigned i=0;i<4;i++){
  const binding_walk*w=&d->walk[i];fprintf(f,"%s{\"access\":%u,\"freshClone\":%u,\"reason\":%u,\"available\":%u,\"codeHex\":\"",i?",":"",w->access,w->fresh,w->reason,w->available);
  for(unsigned n=0;n<32;n++)fprintf(f,"%02x",w->code[n]);fputs("\",\"spans\":[",f);
  for(unsigned n=0;n<w->spans;n++){const binding_span*s=&w->span[n];fprintf(f,"%s[%u,%u,%u,%u,%u]",n?",":"",s->va,s->bytes,s->pa,s->fault,s->available);}fputs("],\"tables\":[",f);
  for(unsigned n=0;n<w->words;n++){const binding_word*r=&w->table[n];fprintf(f,"%s[%u,%u,%u]",n?",":"",r->address,r->value,r->available);}fputs("]}",f);
 }
 fputs("]}\n",f);fclose(f);
 dumpfile("binding-code.bin",d->walk[0].code,d->walk[0].available?32:0);dumpfile("binding-expected.bin",d->expected,d->expected_available?32:0);
 printf("BINDING reason%u pc%08x cpsr%08x cycles%llu count%u pending%u originalWindowAvailable%u actualFetchExecuted0 cp15",d->reason,d->pc,d->cpsr,(unsigned long long)d->cycles,d->count,d->pending,d->walk[0].available);
 printf(" %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",d->sctlr,d->ttbr0,d->ttbr1,d->ttbcr,d->dacr,d->context_id,d->dfsr,d->ifsr,d->dfar,d->ifar);
}
