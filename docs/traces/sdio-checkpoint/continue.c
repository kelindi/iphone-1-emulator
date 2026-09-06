#include "checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
/* Single-threaded probe-only observation; never part of a saved device. */
typedef struct {uint64_t cycles,ticks;uint32_t pc,address,value,flags;} event_t;
static struct {bool active,ok; s5l8900_t*m;event_t events[64];unsigned put,get;uint64_t transactions;FILE*original,*restored,*counters;} trace;
static event_t event(s5l8900_t*m,bool wr,uint32_t a,uint32_t v,unsigned width,bool accepted){return(event_t){m->cpu.cycles,m->timer.ticks,m->cpu.r[15],a,v,(wr?1u:0u)|(width<<1)|(accepted?256u:0u)};}
static void emit(FILE*f,event_t e){uint8_t bytes[32];uint64_t values[]={e.cycles,e.ticks,e.pc,e.address,e.value,e.flags};unsigned sizes[]={8,8,4,4,4,4},pos=0;for(unsigned i=0;i<6;i++)for(unsigned j=0;j<sizes[i];j++)bytes[pos++]=(uint8_t)(values[i]>>(8*j));if(f&&fwrite(bytes,1,sizeof bytes,f)!=sizeof bytes)trace.ok=false;}
void checkpoint_original_event(bool wr,uint32_t a,uint32_t v,unsigned width,bool accepted){if(!trace.active)return;if(trace.put==64){trace.ok=false;return;}event_t e=event(trace.m,wr,a,v,width,accepted);trace.events[trace.put++]=e;emit(trace.original,e);}
static void restored_event(checkpoint_phone_t*p,bool wr,uint32_t a,uint32_t v,unsigned width,bool accepted){event_t e=event(p->machine,wr,a,v,width,accepted);emit(trace.restored,e);if(trace.get>=trace.put){trace.ok=false;return;}event_t a0=trace.events[trace.get++];if(a0.cycles!=e.cycles||a0.ticks!=e.ticks||a0.pc!=e.pc||a0.address!=e.address||a0.value!=e.value||a0.flags!=e.flags)trace.ok=false;trace.transactions++;}
static bool equal(s5l8900_t*m,sdio_t*s,checkpoint_phone_t*b,bool initial){checkpoint_phone_t*v=calloc(1,sizeof *v);if(!v)return false;v->machine=m;v->device=*s;uint8_t*am=NULL,*as=NULL,*bm=NULL,*bs=NULL;size_t an=0,al=0,bn=0,bl=0;bool ok=checkpoint_pair_save(v,&am,&an,&as,&al)&&checkpoint_pair_save(b,&bm,&bn,&bs,&bl)&&an==bn&&al==bl&&(initial?!memcmp(am,bm,an):checkpoint_arch_equal(am,an,&m->cpu,bm,bn,&b->machine->cpu))&&!memcmp(as,bs,al);if(trace.counters)fprintf(trace.counters,"{\"cycles\":%"PRIu64",\"initialRaw\":%s,\"passed\":%s,\"original\":[%"PRIu64",%"PRIu64",%"PRIu64"],\"restored\":[%"PRIu64",%"PRIu64",%"PRIu64"]}\n",m->cpu.cycles,initial?"true":"false",ok?"true":"false",m->cpu.tlb_hits,m->cpu.tlb_misses,m->cpu.tlb_flushes,b->machine->cpu.tlb_hits,b->machine->cpu.tlb_misses,b->machine->cpu.tlb_flushes);free(v);free(am);free(as);free(bm);free(bs);return ok;}
bool checkpoint_continue_compare(s5l8900_t*m,sdio_t*s,void(*sync_original)(void),const uint8_t*mb,size_t mn,const uint8_t*sb,size_t sn,uint32_t stop,uint64_t limit,const char*prefix){
 checkpoint_phone_t*b=checkpoint_pair_load(mb,mn,sb,sn);if(!b)return false;
 memset(&trace,0,sizeof trace);trace.m=m;trace.ok=true;char path[512];
 snprintf(path,sizeof path,"%s-cache-counters.jsonl",prefix);trace.counters=fopen(path,"wbx");trace.ok=trace.counters&&equal(m,s,b,true);
 snprintf(path,sizeof path,"%s-original-transactions.bin",prefix);trace.original=fopen(path,"wbx");
 snprintf(path,sizeof path,"%s-restored-transactions.bin",prefix);trace.restored=fopen(path,"wbx");
 if(!trace.original||!trace.restored)trace.ok=false;
 uint64_t instructions=0,irq_edges=0;unsigned full_comparisons=1;bool old_irq=sdio_irq_level(s);
 /* Observation hooks are host state excluded from snapshots. Never return true
  * to fake a yield, never longjmp; target is tested after normal run exits. */
 s5l8900_set_pre_step_hook(m,NULL,NULL,NULL,0);b->observe=restored_event;trace.active=true;
 while(trace.ok&&instructions<limit&&m->cpu.r[15]!=stop){
  trace.put=trace.get=0;arm_status_t astatus=ARM_OK,bstatus=ARM_OK;
  unsigned ar=s5l8900_run(m,1,&astatus);sync_original();
  unsigned br=s5l8900_run(b->machine,1,&bstatus);checkpoint_sync(b);
  if(ar!=1||br!=1||astatus!=ARM_OK||bstatus!=ARM_OK||trace.put!=trace.get||m->cpu.r[15]!=b->machine->cpu.r[15]||m->timer.ticks!=b->machine->timer.ticks||m->cpu.cycles!=b->machine->cpu.cycles||sdio_irq_level(s)!=sdio_irq_level(&b->device)||((m->vic[1].raw>>10)&1u)!=((b->machine->vic[1].raw>>10)&1u))trace.ok=false;
  bool irq=sdio_irq_level(s);if(irq!=old_irq){irq_edges++;old_irq=irq;}
  instructions++;if(instructions==1||instructions==1024||instructions==32768){trace.ok=trace.ok&&equal(m,s,b,false);full_comparisons++;}
  if(trace.transactions>8388608u)trace.ok=false; /*256MiB per bounded trace*/
 }
 bool reached=m->cpu.r[15]==stop&&b->machine->cpu.r[15]==stop;
 trace.ok=trace.ok&&reached&&equal(m,s,b,false);full_comparisons++;trace.active=false;
 if(trace.original&&fclose(trace.original))trace.ok=false;
 if(trace.restored&&fclose(trace.restored))trace.ok=false;
 if(trace.counters&&fclose(trace.counters))trace.ok=false;
 trace.counters=NULL;
 snprintf(path,sizeof path,"%s-comparison.json",prefix);FILE*f=fopen(path,"wbx");if(!f)trace.ok=false;
 if(f){fprintf(f,"{\"passed\":%s,\"stopReached\":%s,\"pc\":\"%08x\",\"r4\":%u,\"additionalInstructions\":%"PRIu64",\"instructionLimit\":%"PRIu64",\"transactionsCompared\":%"PRIu64",\"irqEdges\":%"PRIu64",\"architecturalStateComparisons\":%u,\"timerTicks\":%"PRIu64",\"totalInstructions\":%"PRIu64",\"commandCompletions\":%"PRIu64",\"dmaCompletions\":%"PRIu64",\"mainPhase\":%u,\"requested\":%u}\n",trace.ok?"true":"false",reached?"true":"false",m->cpu.r[15],m->cpu.r[4],instructions,limit,trace.transactions,irq_edges,full_comparisons,m->timer.ticks,m->cpu.cycles,s->completions,s->data_completions,s->main_model.phase,s->main_model.requested);if(fclose(f))trace.ok=false;}
 bool ok=trace.ok;checkpoint_phone_free(b);free(b);return ok;
}
