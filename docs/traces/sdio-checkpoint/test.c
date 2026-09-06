#include "checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned checks,failed;
#define CHECK(x) do{checks++;if(!(x)){failed++;fprintf(stderr,"FAIL %s:%d %s\n",__func__,__LINE__,#x);}}while(0)
static sdio_config_t config(void){return(sdio_config_t){.present=true,.enumeration=true,.boot_endpoint=true,.data_transfer=true,.endpoint_capacity=512,.io_port=0x10000,.function_init_ticks=1800,.io_functions=1,.assigned_rca=1,.voltage_mask=0x60000,.readable_cccr=0x101,.command_ticks=600,.data_ticks=600};}
static void ready(checkpoint_phone_t*p){sdio_t*s=&p->device;s->card.state=CARD_SELECTED;s->card.rca=1;s->card.selected_voltage=0x60000;s->card.block_size[1]=32;s->endpoint.initialized=s->endpoint.enabled=s->endpoint.ready=s->endpoint.host_power=true;s->endpoint.configuration=2;s->endpoint.capacity=512;s->control=1;}
static void advance(checkpoint_phone_t*p,uint64_t n){p->machine->timer.ticks=n;checkpoint_sync(p);}
static void wr(checkpoint_phone_t*p,unsigned o,unsigned v){p->machine->bus.write32(p->machine->bus.ctx,SDIO_BASE+o,v);}
static void setup(checkpoint_phone_t*p,unsigned mode){sdio_config_t c=config();CHECK(checkpoint_phone_init(p,0x08000000u,1u<<20,&c));if(!p->machine)exit(2);
 uint32_t program[]={0xe2800001u,0xeafffffdu};s5l8900_load(p->machine,0x08000100u,program,sizeof program);p->machine->cpu.r[15]=0x08000100u;
 p->machine->cpu_hz=p->machine->tb_hz=1000;p->machine->pmu.tick_hz=1000;p->machine->cpu.cpsr=ARM_MODE_SVC|ARM_CPSR_I|ARM_CPSR_F;
 for(unsigned i=0;i<512;i++)p->machine->ram[0x1000+i]=(uint8_t)(i*31+7);
 p->machine->bus.write8(p->machine,0x08002000u,0x5a);CHECK(p->machine->bus.read8(p->machine,0x08002000u)==0x5a);
 p->machine->bus.write16(p->machine,0x08002002u,0xabcd);CHECK(p->machine->bus.read16(p->machine,0x08002002u)==0xabcd);
 p->machine->bus.write32(p->machine,0x08002004u,0xdeadbeef);CHECK(p->machine->bus.read32(p->machine,0x08002004u)==0xdeadbeef);
 advance(p,100);sdio_t*s=&p->device;
 if(mode==0)return;
 if(mode==1){s->endpoint.initialized=s->endpoint.enabled=s->endpoint.initializing=true;s->endpoint.capacity=512;s->endpoint.ready_deadline=700;return;}
 ready(p);
 if(mode==2){wr(p,SDIO_CMD,SDIO_EXECUTE|0x40045);return;}
 if(mode==3){wr(p,SDIO_CMD,SDIO_EXECUTE|0x40045);advance(p,700);return;}
 if(mode>=4&&mode<=6){wr(p,SDIO_BUFFER,0x08001000u);wr(p,SDIO_BLOCK_LENGTH,32);wr(p,SDIO_BLOCK_COUNT,2);wr(p,SDIO_ARG,0x9a000002);wr(p,SDIO_CMD,SDIO_EXECUTE|0x501f5);advance(p,700);CHECK(s->data.armed);wr(p,SDIO_IRQMASK,1);wr(p,SDIO_DCTRL,0x10);CHECK(s->data.active);
  if(mode>=5){advance(p,1300);CHECK(s->data_completions==1&&s->endpoint.used==64&&s->irq_pending==1&&(s->events&0x20));}
  if(mode==6){wr(p,SDIO_IRQMASK,0);CHECK(!sdio_irq_level(s));}return;}
 if(mode>=7){s->config.helper_receiver=true;s->config.helper_semantics=true;s->config.complete_main=true;s->config.helper_capacity=4096;s->config.semantic_ticks=600;
  s->helper.terminated=true;s->helper.used=2140;s->helper.frames=37;
  /* Synthetic pending main bytes: semantics operate on the received header,
   * not any copied firmware fixture. No helper identity/execution claim. */
  s->main_model.phase=MAIN_HEADER;s->main_model.requested=16;
  uint8_t h[32]={1,0,0,0,0,0,0,0,0,2};
  CHECK(sdio_endpoint_receive(s,0x10000,h,sizeof h));CHECK(s->main_model.phase==MAIN_HEADER_PENDING);
  if(mode==8)sdio_host_reset(s); /* retained pending frame; deadline reanchors */
  if(mode==9){s->endpoint.interrupt_pending=3;s->endpoint.interrupt_mask=3;s->irq_pending=3;s->irq_mask=3;checkpoint_sync(p);}return;
 }
}
static void fields_equal(checkpoint_phone_t*a,checkpoint_phone_t*b){
#include "independent_fields.inc"
}
static void equal(checkpoint_phone_t*a,checkpoint_phone_t*b){fields_equal(a,b);uint8_t*am,*as,*bm,*bs;size_t an,al,bn,bl;CHECK(checkpoint_pair_save(a,&am,&an,&as,&al));CHECK(checkpoint_pair_save(b,&bm,&bn,&bs,&bl));CHECK(an==bn&&memcmp(am,bm,an)==0);CHECK(al==bl&&memcmp(as,bs,al)==0);free(am);free(as);free(bm);free(bs);}
static void run(checkpoint_phone_t*p,unsigned count){for(unsigned j=0;j<count;j++){arm_status_t st=ARM_OK;CHECK(s5l8900_run(p->machine,1,&st)==1);CHECK(st==ARM_OK);checkpoint_sync(p);}}
static void writefile(const char*dir,const char*name,const void*b,size_t n){char path[512];snprintf(path,sizeof path,"%s/%s",dir,name);FILE*f=fopen(path,"wb");CHECK(f!=NULL);if(f){CHECK(fwrite(b,1,n,f)==n);CHECK(fclose(f)==0);}}
static void scenario(unsigned mode,const char*dir){checkpoint_phone_t a={0};setup(&a,mode);
 sdio_config_t c=config();s5l8900_t*same=a.machine;CHECK(!checkpoint_phone_init(&a,0x08000000u,1u<<20,&c));CHECK(a.machine==same);uint8_t*m=NULL,*s=NULL;size_t mn=0,sn=0;CHECK(checkpoint_pair_save(&a,&m,&mn,&s,&sn));if(!m||!s)exit(3);
 checkpoint_phone_t*b=checkpoint_pair_load(m,mn,s,sn);CHECK(b!=NULL);if(!b)exit(4);
 CHECK(b->device.dma_ctx==b&&b->device.dma_reader!=NULL);CHECK(b->machine->bus.ctx==b->machine&&b->machine->cpu.bus==&b->machine->bus);CHECK(!b->machine->bus.host_ram_write);CHECK(b->machine->bus.host_ram==b->underlying.host_ram);
 CHECK(b->device.dma_reader(b->device.dma_ctx,0x08001000u,512)==b->machine->ram+0x1000);CHECK(!b->device.dma_reader(b->device.dma_ctx,UINT32_MAX-4,512));equal(&a,b);
 if(mode==4&&dir){mkdir(dir,0700);writefile(dir,"machine.snap",m,mn);writefile(dir,"sdio.side",s,sn);}
 /* Repeated normal instruction-run exits, with virtual time carried unchanged.
  * Each prefix compares the entire saved machine/device, not selected counters. */
 unsigned prefixes[]={1,7,63,600};for(unsigned i=0;i<4;i++){run(&a,prefixes[i]);run(b,prefixes[i]);equal(&a,b);CHECK(sdio_irq_level(&a.device)==sdio_irq_level(&b->device));}
 uint32_t ar=sdio_read(&a.device,SDIO_STATUS,4,a.machine->timer.ticks),br=sdio_read(&b->device,SDIO_STATUS,4,b->machine->timer.ticks);CHECK(ar==br);
 wr(&a,SDIO_ACK,ar);wr(b,SDIO_ACK,br);wr(&a,SDIO_IRQ,3);wr(b,SDIO_IRQ,3);equal(&a,b);
 sdio_host_reset(&a.device);sdio_host_reset(&b->device);checkpoint_sync(&a);checkpoint_sync(b);equal(&a,b);
 sdio_card_reset(&a.device);sdio_card_reset(&b->device);checkpoint_sync(&a);checkpoint_sync(b);equal(&a,b);
 checkpoint_phone_free(b);free(b);
 /* Failed loads allocate/discard a separate phone; the existing one is unchanged. */
 uint8_t*beforeM,*beforeS;size_t beforeN,beforeL;CHECK(checkpoint_pair_save(&a,&beforeM,&beforeN,&beforeS,&beforeL));
 uint8_t old=s[8];s[8]=2;CHECK(!checkpoint_pair_load(m,mn,s,sn));s[8]=old;
 old=s[48];s[48]=2;CHECK(!checkpoint_pair_load(m,mn,s,sn));s[48]=old;
 for(unsigned cut=0;cut<48;cut++)CHECK(!checkpoint_pair_load(m,mn,s,cut));
 old=m[mn-1];m[mn-1]^=1;CHECK(!checkpoint_pair_load(m,mn,s,sn));m[mn-1]=old;
 uint8_t*afterM,*afterS;size_t afterN,afterL;CHECK(checkpoint_pair_save(&a,&afterM,&afterN,&afterS,&afterL));CHECK(beforeN==afterN&&!memcmp(beforeM,afterM,beforeN));CHECK(beforeL==afterL&&!memcmp(beforeS,afterS,beforeL));
 free(beforeM);free(beforeS);free(afterM);free(afterS);free(m);free(s);checkpoint_phone_free(&a);
}
static checkpoint_phone_t*original_phone;
static void original_sync(void){checkpoint_sync(original_phone);}
static void original_observer(checkpoint_phone_t*p,bool w,uint32_t a,uint32_t v,unsigned n,bool accepted){(void)p;checkpoint_original_event(w,a,v,n,accepted);}
static void safe_boundary(const char*dir){checkpoint_phone_t p={0};setup(&p,0);original_phone=&p;p.observe=original_observer;
 uint32_t program[]={0xe59f1008u,0xe5912000u,0xe2800001u,0xeafffffbu,SDIO_BASE+SDIO_STATUS};
 s5l8900_load(p.machine,0x08000100u,program,sizeof program);
 uint8_t*m,*s;size_t mn,sn;CHECK(checkpoint_pair_save(&p,&m,&mn,&s,&sn));
 char prefix[512];snprintf(prefix,sizeof prefix,"%s/synthetic-continuation-%ld",dir?dir:".context/sdio-first/checkpoint/fixture",(long)getpid());
 CHECK(checkpoint_continue_compare(p.machine,&p.device,original_sync,m,mn,s,sn,0x08000108u,64,prefix));
 CHECK(p.machine->cpu.r[15]==0x08000108u&&p.machine->cpu.r[0]==0&&p.machine->pre_step_handled==0);
 arm_status_t status=ARM_OK;CHECK(s5l8900_run(p.machine,1,&status)==1&&status==ARM_OK);CHECK(p.machine->cpu.r[15]==0x0800010cu&&p.machine->cpu.r[0]==1);
 free(m);free(s);checkpoint_phone_free(&p);
}
static void checksum(uint8_t*b,size_t n){uint64_t h=1469598103934665603ull;for(size_t i=40;i<n-8;i++){h^=b[i];h*=1099511628211ull;}for(unsigned i=0;i<8;i++)b[n-8+i]=(uint8_t)(h>>(8*i));}
static void warm_mmu(const char*dir){checkpoint_phone_t p={0};setup(&p,0);original_phone=&p;p.observe=original_observer;
 /* Two section translations: real RAM and real device aperture. */
 p.machine->bus.write32(p.machine,0x08004000u+0x80*4,0x08000c02u);
 p.machine->bus.write32(p.machine,0x08004000u+0x38d*4,0x38d00c02u);
 p.machine->cpu.cp15.ttbr0=0x08004000u;p.machine->cpu.cp15.dacr=3;p.machine->cpu.cp15.sctlr|=1;
 uint32_t program[]={0xe59f1008u,0xe5912000u,0xe2800001u,0xeafffffbu,SDIO_BASE+SDIO_STATUS};
 s5l8900_load(p.machine,0x08000100u,program,sizeof program);run(&p,4);
 CHECK(p.machine->cpu.r[15]==0x08000100u);CHECK(p.machine->cpu.tlb_misses>0&&p.machine->cpu.fetch_host);
 uint8_t*m,*s;size_t mn,sn;CHECK(checkpoint_pair_save(&p,&m,&mn,&s,&sn));
 checkpoint_phone_t*b=checkpoint_pair_load(m,mn,s,sn);CHECK(b!=NULL);if(!b)exit(4);equal(&p,b);
 CHECK(!b->machine->cpu.fetch_host);run(&p,1);run(b,1);
 uint8_t*am,*as,*bm,*bs;size_t an,al,bn,bl;CHECK(checkpoint_pair_save(&p,&am,&an,&as,&al));CHECK(checkpoint_pair_save(b,&bm,&bn,&bs,&bl));
 CHECK(an==bn&&memcmp(am,bm,an)!=0);CHECK(p.machine->cpu.tlb_misses!=b->machine->cpu.tlb_misses||p.machine->cpu.tlb_hits!=b->machine->cpu.tlb_hits||p.machine->cpu.tlb_flushes!=b->machine->cpu.tlb_flushes);
 CHECK(checkpoint_arch_equal(am,an,&p.machine->cpu,bm,bn,&b->machine->cpu));CHECK(al==bl&&!memcmp(as,bs,al));
 /* Negative exceptions: rehash the mutation so integrity alone cannot hide a
  * counter-window mismatch, architecture change, RAM or device change. */
 size_t ram=40;while(memcmp(bm+ram,"RAM ",4)){uint64_t len=0;for(unsigned j=0;j<8;j++)len|=(uint64_t)bm[ram+8+j]<<(8*j);ram+=16+(size_t)len;}
 size_t mutations[]={162,566,574,582,600,ram+16,an-9};for(unsigned i=0;i<7;i++){bm[mutations[i]]^=1;checksum(bm,bn);CHECK(!checkpoint_arch_equal(am,an,&p.machine->cpu,bm,bn,&b->machine->cpu));bm[mutations[i]]^=1;checksum(bm,bn);}
 bm[bn-1]^=1;CHECK(!checkpoint_arch_equal(am,an,&p.machine->cpu,bm,bn,&b->machine->cpu));bm[bn-1]^=1;
 writefile(dir,"warm-original.snap",am,an);writefile(dir,"warm-restored.snap",bm,bn);
 free(am);free(as);free(bm);free(bs);checkpoint_phone_free(b);free(b);free(m);free(s);
 /* Capture the still-warm original at its current next instruction. */
 CHECK(checkpoint_pair_save(&p,&m,&mn,&s,&sn));char prefix[512];snprintf(prefix,sizeof prefix,"%s/warm-continuation-%ld",dir,(long)getpid());
 CHECK(checkpoint_continue_compare(p.machine,&p.device,original_sync,m,mn,s,sn,0x08000108u,64,prefix));CHECK(p.machine->cpu.r[0]==1);
 free(m);free(s);checkpoint_phone_free(&p);
}
static void reject_n82(void){checkpoint_phone_t p={0};setup(&p,0);uint8_t*pm,*ps;size_t pn,pl;CHECK(checkpoint_pair_save(&p,&pm,&pn,&ps,&pl));
 s5l8900_t*n=calloc(1,sizeof *n);CHECK(n!=NULL);if(!n)exit(2);CHECK(s5l8900_init(n,0x08000000u,1u<<20));
 n->timer.ticks=p.machine->timer.ticks;n->cpu.cycles=p.machine->cpu.cycles;n->cpu.r[15]=p.machine->cpu.r[15];n->cpu_hz=n->tb_hz=1000;n->pmu.tick_hz=1000;
 uint8_t*nm;size_t nn;CHECK(snapshot_save_mem(n,&nm,&nn)==SNAP_OK);
 CHECK(snapshot_load_mem(p.machine,nm,nn)==SNAP_ERR_GEOMETRY);CHECK(!checkpoint_pair_load(nm,nn,ps,pl));
 uint8_t*am,*as;size_t an,al;CHECK(checkpoint_pair_save(&p,&am,&an,&as,&al));CHECK(pn==an&&!memcmp(pm,am,pn));CHECK(pl==al&&!memcmp(ps,as,pl));
 checkpoint_phone_t fake={.machine=n,.device=p.device};uint8_t*ignored=NULL;size_t iz=0;CHECK(!checkpoint_pair_save(&fake,&ignored,&iz,&ignored,&iz));
 free(pm);free(ps);free(nm);free(am);free(as);s5l8900_free(n);free(n);checkpoint_phone_free(&p);
}
int main(int argc,char**argv){for(unsigned mode=0;mode<10;mode++)scenario(mode,argc>1?argv[1]:NULL);reject_n82();warm_mmu(argc>1?argv[1]:".context/sdio-first/checkpoint/fixture");safe_boundary(argc>1?argv[1]:NULL);printf("Private checkpoint synthetic: %u checks, %u failures\n",checks,failed);return failed?1:0;}
