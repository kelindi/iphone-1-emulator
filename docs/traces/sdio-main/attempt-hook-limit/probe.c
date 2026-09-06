#include "board.h"
#include "bringup_os1.h"
#include "sdio.h"
#include "devicetree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <setjmp.h>
static s5l8900_t machine;
static arm_bus_t underlying;
static sdio_t sdio;
static FILE *trace_file,*serial_file,*wire_file,*request_wire_file;
static bool main_first;
static uint64_t seen_publications, main_call_returns, main_length_pairs;
static bool main_call_ok=true, main_sequence_ok=true;
static bool no_consume, main_negative, wire_ok=true, all_transfers_ok=true;
static uint64_t completion_returns, source_wire_bytes, last_request_trigger, request_frames;
static const char *output_prefix;
static bool baseline, gate_reached, cccr_entered, local_mac;
static uint32_t matched_provider;
static bool helper_frame_captured, irq_entered, dma_entered;
static bool last_irq, dma_return_pending;
static uint64_t dma_source_reads;
static bool no_calibration, calibration_absent_path, mac_accepted_path;
static uint64_t accesses, status_reads, forwarded[6], card_errors;
static uint32_t last_status=UINT32_MAX;
static jmp_buf gate_stop;
static void sync_sdio(void){
 if(baseline)return;
 uint64_t before=sdio.data_completions;sdio_step(&sdio,machine.timer.ticks);
 if(sdio.data_completions!=before)fprintf(trace_file,"data copy committed ticks=%"PRIu64" count=%"PRIu64" fifoUsed=%u helperUsed=%u frames=%u terminated=%u protocolErrors=%"PRIu64" status=%08x irq=%08x\n",machine.timer.ticks,sdio.data_completions,sdio.endpoint.used,sdio.helper.used,sdio.helper.frames,sdio.helper.terminated,sdio.helper_protocol_errors,sdio.events,sdio.irq_pending);
 if(sdio.main_model.publications!=seen_publications){seen_publications=sdio.main_model.publications;fprintf(trace_file,"modeled helper request published ticks=%"PRIu64" phase=%u length=%u headers=%"PRIu64" bodies=%"PRIu64" copied=%"PRIu64" publications=%"PRIu64"; semantic model not helper CPU execution\n",machine.timer.ticks,sdio.main_model.phase,sdio.main_model.requested,sdio.main_model.headers,sdio.main_model.bodies,sdio.main_model.copied,seen_publications);}
 bool level=sdio_irq_level(&sdio);s5l_vic_set_line(&machine.vic[1],10,level);
 if(level!=last_irq){fprintf(trace_file,"SDIO IRQ42 level=%u ticks=%"PRIu64" pc=%08x\n",level,machine.timer.ticks,machine.cpu.r[15]);last_irq=level;}
}
static const uint8_t *dma_source(void *ctx,uint32_t address,size_t length){
 dma_source_reads++;
 const uint8_t *p=underlying.host_ram?underlying.host_ram(ctx,address,length):NULL;
 fprintf(trace_file,"whole DMA source lookup address=%08x length=%zu valid=%u ticks=%"PRIu64"\n",address,length,p!=NULL,machine.timer.ticks);
 if(p){if(fwrite(p,1,length,wire_file)!=length)wire_ok=false;source_wire_bytes+=length;}
 return p;
}
static bool in_sdio(uint32_t address){return address>=SDIO_BASE&&address-SDIO_BASE<SDIO_SIZE;}
static void log_access(bool write,uint32_t address,uint32_t value,unsigned width,bool accepted){
 accesses++;
 if(!write&&address==SDIO_BASE+SDIO_RESP0&&((sdio.command&63u)==52u||(sdio.command&63u)==53u)&&(value&0xcb00u))card_errors++;
 if(!write&&address==SDIO_BASE+SDIO_STATUS){status_reads++;if(value==last_status)return;last_status=value;}
 fprintf(trace_file,"%"PRIu64" ticks=%"PRIu64" pc=%08x %c%u %08x=%08x accepted=%u card=%u busy=%u triggers=%"PRIu64" complete=%"PRIu64"\n",
 machine.cpu.cycles,machine.timer.ticks,machine.cpu.r[15],write?'W':'R',width,address,value,accepted,sdio.card.state,sdio.busy,sdio.triggers,sdio.completions);
}
static uint32_t read32(void *ctx,uint32_t a){if(!in_sdio(a)){forwarded[0]++;return underlying.read32(ctx,a);}uint32_t v=baseline?underlying.read32(ctx,a):sdio_read(&sdio,a-SDIO_BASE,4,machine.timer.ticks);log_access(false,a,v,4,true);sync_sdio();return v;}
static uint16_t read16(void *ctx,uint32_t a){if(!in_sdio(a)){forwarded[1]++;return underlying.read16(ctx,a);}uint16_t v=baseline?underlying.read16(ctx,a):(uint16_t)sdio_read(&sdio,a-SDIO_BASE,2,machine.timer.ticks);log_access(false,a,v,2,true);return v;}
static uint8_t read8(void *ctx,uint32_t a){if(!in_sdio(a)){forwarded[2]++;return underlying.read8(ctx,a);}uint8_t v=baseline?underlying.read8(ctx,a):(uint8_t)sdio_read(&sdio,a-SDIO_BASE,1,machine.timer.ticks);log_access(false,a,v,1,true);return v;}
static void write32(void *ctx,uint32_t a,uint32_t v){if(!in_sdio(a)){forwarded[3]++;underlying.write32(ctx,a,v);return;}bool ok=true;if(baseline)underlying.write32(ctx,a,v);else ok=sdio_write(&sdio,a-SDIO_BASE,v,4,machine.timer.ticks);log_access(true,a,v,4,ok);sync_sdio();}
static void write16(void *ctx,uint32_t a,uint16_t v){if(!in_sdio(a)){forwarded[4]++;underlying.write16(ctx,a,v);return;}bool ok=true;if(baseline)underlying.write16(ctx,a,v);else ok=sdio_write(&sdio,a-SDIO_BASE,v,2,machine.timer.ticks);log_access(true,a,v,2,ok);}
static void write8(void *ctx,uint32_t a,uint8_t v){if(!in_sdio(a)){forwarded[5]++;underlying.write8(ctx,a,v);return;}bool ok=true;if(baseline)underlying.write8(ctx,a,v);else ok=sdio_write(&sdio,a-SDIO_BASE,v,1,machine.timer.ticks);log_access(true,a,v,1,ok);}
static void serial(void *ctx,uint8_t ch){(void)ctx;fputc(ch,serial_file);}
static bool probe_hook(void *ctx){
 (void)ctx;uint32_t pc=machine.cpu.r[15];
 if(pc==0xc0379780u){cccr_entered=true;fprintf(trace_file,"genuine CCCR initializer entered pc=%08x\n",pc);}
 if(!baseline&&pc==0xc0381bacu){
  matched_provider=machine.cpu.r[0];
  fprintf(trace_file,"genuine AppleMRVL868x provider cast returned pc=%08x provider=%08x\n",pc,matched_provider);
 }
 if(pc==0xc0381c20u){calibration_absent_path=true;fprintf(trace_file,"genuine missing-calibration branch accepted pc=%08x; no RF calibration supplied\n",pc);}
 if(pc==0xc0381ce0u){mac_accepted_path=true;fprintf(trace_file,"genuine nonzero-MAC branch accepted pc=%08x\n",pc);}
 if(!baseline&&pc==0xc0381bdcu){
  fprintf(trace_file,"genuine AppleMRVL868x early failure pc=%08x provider=%08x message=%08x\n",pc,matched_provider,machine.cpu.r[0]);
  longjmp(gate_stop,1);
 }
 if(!baseline&&!local_mac&&pc==0xc0381cdcu){
  gate_reached=matched_provider && (machine.cpu.cpsr & 0x40000000u) && sdio.cccr_read_mask==0x101u;
  fprintf(trace_file,"genuine AppleMRVL868x zero-MAC rejection pc=%08x provider=%08x cpsr=%08x message=%08x passed=%u\n",pc,matched_provider,machine.cpu.cpsr,machine.cpu.r[0],gate_reached);
  longjmp(gate_stop,1);
 }
 if(local_mac&&matched_provider&&calibration_absent_path&&mac_accepted_path&&pc==0xc0431528u&&
    sdio.triggers!=last_request_trigger&&sdio.busy&&(sdio.pending_command&63u)==53u){
  last_request_trigger=sdio.triggers;
  unsigned length=sdio.block_length==32u&&(sdio.block_count==1u||sdio.block_count==2u||(main_first&&sdio.block_count==16u))?32u*sdio.block_count:0u;
  const uint8_t *source=length&&underlying.host_ram?underlying.host_ram(machine.bus.ctx,sdio.buffer_address,length):NULL;
  if(source){
   bool captured=fwrite(source,1,length,request_wire_file)==length;
   wire_ok=wire_ok&&captured;helper_frame_captured=helper_frame_captured||captured;request_frames++;
  } else wire_ok=false;
  fprintf(trace_file,"CMD53 request committed pc=%08x descriptor=%08x argument=%08x buffer=%08x length=%u capturedFrames=%"PRIu64"; continuing through genuine DMA path\n",pc,sdio.pending_command,sdio.pending_argument,sdio.buffer_address,length,request_frames);
 }
 if(pc==0xc0431b04u&&helper_frame_captured)fprintf(trace_file,"genuine data-start write committed pc=%08x starts=%"PRIu64" due=%"PRIu64" used=%u\n",pc,sdio.data_starts,sdio.data.deadline,sdio.endpoint.used);
 if(pc==0xc0432108u&&sdio.data_completions){irq_entered=true;fprintf(trace_file,"genuine SDIO IRQ handler entered pc=%08x pending=%08x mask=%08x\n",pc,sdio.irq_pending,sdio.irq_mask);}
 if(pc==0xc0431b4cu&&sdio.data_completions){dma_entered=true;dma_return_pending=true;fprintf(trace_file,"genuine completeDMA entered pc=%08x\n",pc);}
 if(pc==0xc0379684u&&dma_return_pending){
  dma_return_pending=false;completion_returns++;
  bool good=machine.cpu.r[0]==1&&!sdio.irq_pending&&!(sdio.events&0x20u);
  all_transfers_ok=all_transfers_ok&&good;
  fprintf(trace_file,"genuine completeDMA consumed return pc=%08x r0=%u returns=%"PRIu64" copies=%"PRIu64" helperUsed=%u frames=%u terminated=%u irq=%08x status=%08x good=%u\n",pc,machine.cpu.r[0],completion_returns,sdio.data_completions,sdio.helper.used,sdio.helper.frames,sdio.helper.terminated,sdio.irq_pending,sdio.events,good);
  if(sdio.helper_protocol_errors){fprintf(trace_file,"bounded framing diagnostic stop; physical transfer and receiver protocol remain separate\n");longjmp(gate_stop,1);}
  if(!no_consume&&sdio.helper.terminated&&sdio.data_completions==37){
   gate_reached=wire_ok&&all_transfers_ok&&helper_frame_captured&&irq_entered&&sdio.helper.used==2140&&sdio.helper.frames==37&&sdio.data_completions==37&&sdio.data_starts==37&&completion_returns==37&&dma_source_reads==37&&source_wire_bytes==2336&&!card_errors&&!sdio.transport_errors&&!sdio.rejected_writes&&!sdio.data_errors;
   fprintf(trace_file,"full helper receive/termination gate passed=%u; no helper execution or firmware-ready marker\n",gate_reached);
   if((!main_negative&&!main_first)||!gate_reached)longjmp(gate_stop,1);
   gate_reached=false; /* Separate negative continuation must reach the real next failure. */
  }
 }
 if(main_first&&pc==0xc038a79cu){
  main_sequence_ok=main_sequence_ok&&machine.cpu.r[10]==16;
  fprintf(trace_file,"genuine initial main request consumed pc=%08x requested=%u good=%u\n",pc,machine.cpu.r[10],main_sequence_ok);
 }
 if(main_first&&pc==0xc038a840u){
  main_call_returns++;main_call_ok=main_call_ok&&machine.cpu.r[0]!=0;
  fprintf(trace_file,"genuine main transfer return pc=%08x r0=%u returns=%"PRIu64" priorOffset=%u logicalLength=%u good=%u\n",pc,machine.cpu.r[0],main_call_returns,machine.cpu.r[11],machine.cpu.r[10],main_call_ok);
 }
 if(main_first&&pc==0xc038a8c8u){
  main_length_pairs++;unsigned expected=main_length_pairs==1?512:16;
  main_sequence_ok=main_sequence_ok&&machine.cpu.r[10]==expected;
  fprintf(trace_file,"genuine next main request pair consumed pc=%08x offset=%u requested=%u pairs=%"PRIu64" good=%u\n",pc,machine.cpu.r[11],machine.cpu.r[10],main_length_pairs,main_sequence_ok);
  if(sdio.main_model.bodies||main_length_pairs>=2){
   gate_reached=main_sequence_ok&&main_call_ok&&main_call_returns==2&&main_length_pairs==2&&machine.cpu.r[11]==528&&machine.cpu.r[10]==16&&sdio.main_model.headers==1&&sdio.main_model.bodies==1&&sdio.main_model.copied==508&&sdio.main_model.requested==16&&!sdio.main_model.errors&&sdio.data_completions==39&&completion_returns==39&&all_transfers_ok&&wire_ok&&source_wire_bytes==2880&&!card_errors&&!sdio.transport_errors&&!sdio.rejected_writes&&!sdio.data_errors;
   fprintf(trace_file,"first modeled header/body consumed gate passed=%u; stop before second header, no CPU execution or firmware-ready claim\n",gate_reached);
   longjmp(gate_stop,1);
  }
 }
 if(main_first&&sdio.main_model.errors){fprintf(trace_file,"bounded semantic model diagnostic=%u pending=%u; physical status independent\n",sdio.main_model.diagnostic,sdio.main_model.pending_length);longjmp(gate_stop,1);}
 if(no_consume&&pc==0xc0431598u&&card_errors&&(sdio.command&63u)==53u){
  gate_reached=sdio.data_completions==1&&sdio.endpoint.used==64&&card_errors==1;
  fprintf(trace_file,"frozen endpoint no-space R5 response consumed pc=%08x response=%08x copies=%"PRIu64" fifoUsed=%u passed=%u; Applewrapper ignores carderrors\n",pc,sdio.response[0],sdio.data_completions,sdio.endpoint.used,gate_reached);
  longjmp(gate_stop,1);
 }
 if(pc==0xc0380300u)fprintf(trace_file,"original helper frame return r0=%u priorPayloadOffset=%u payloadLength=%u\n",machine.cpu.r[0],machine.cpu.r[6],machine.cpu.r[4]);
 if(main_negative&&pc==0xc038a788u){
  gate_reached=all_transfers_ok&&sdio.helper.terminated&&sdio.helper.used==2140&&sdio.data_completions==37&&!sdio.helper_protocol_errors&&!card_errors&&machine.cpu.r[6]==100u;
  fprintf(trace_file,"genuine main-loader no-requested-length failure pc=%08x polls=%u helperUsed=%u passed=%u; received helper remains unexecuted\n",pc,machine.cpu.r[6],sdio.helper.used,gate_reached);
  longjmp(gate_stop,1);
 }
 if(baseline&&pc==0xc0431504u&&status_reads>=100001u){gate_reached=true;longjmp(gate_stop,1);}
 return false;
}
static uint8_t *load(const char *path,size_t *size){FILE*f=fopen(path,"rb");if(!f)return NULL;if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);if(n<=0||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}uint8_t*p=malloc((size_t)n);if(!p||fread(p,1,(size_t)n,f)!=(size_t)n){free(p);p=NULL;}fclose(f);*size=p?(size_t)n:0;return p;}
int main(int argc,char**argv){
 main_first=argc>1&&!strcmp(argv[1],"--main-first");
 baseline=argc>1&&!strcmp(argv[1],"--baseline");
 no_consume=argc>1&&!strcmp(argv[1],"--no-consume");main_negative=argc>1&&!strcmp(argv[1],"--main-negative");
 local_mac=main_first||no_consume||main_negative||(argc>1&&!strcmp(argv[1],"--local-mac"));
 no_calibration=local_mac||(argc>1&&!strcmp(argv[1],"--no-calibration"));
 const char *rootfs=argc>2?argv[2]:".context/organizer-debug/rootfs-compact168-checked.img";
 const char *prefix=main_first?".context/sdio-first/main-first/positive":no_consume?".context/sdio-first/main-first/no-consume":main_negative?".context/sdio-first/main-first/main-negative":local_mac?".context/sdio-first/main-first/local-mac":no_calibration?".context/sdio-first/main-first/no-calibration":baseline?".context/sdio-first/main-first/baseline":".context/sdio-first/main-first/candidate";
 output_prefix=prefix;
 char path[256];snprintf(path,sizeof path,"%s-mmio.log",prefix);trace_file=fopen(path,"wb");snprintf(path,sizeof path,"%s-serial.log",prefix);serial_file=fopen(path,"wb");if(!trace_file||!serial_file)return 2;
 snprintf(path,sizeof path,"%s-source-wire.bin",prefix);wire_file=fopen(path,"wb");if(!wire_file)return 22;
 snprintf(path,sizeof path,"%s-request-wire.bin",prefix);request_wire_file=fopen(path,"wb");if(!request_wire_file)return 26;
 setvbuf(trace_file,NULL,_IOLBF,0);setvbuf(serial_file,NULL,_IOLBF,0);
 size_t kn,dn,rn;uint8_t*k=load("web/firmware/kernel_1A543a.macho",&kn),*d=load("web/firmware/devicetree_m68ap.bin",&dn),*r=load(rootfs,&rn);if(!k||!d||!r)return 3;
 const s5l_board_t *board=s5l_board_find("m68");char why[256];
 if(!board||!s5l_board_check_devicetree(board,d,dn,why,sizeof why)||!s5l_board_init(board,&machine,256u<<20))return 4;
 if(no_calibration){
  dt_t tree;dt_node_t rt,node;const uint8_t *value;uint32_t len;
  if(dt_parse(d,dn,&tree,&rt)!=DT_OK||dt_path(&tree,&rt,"arm-io/sdio",&node)!=DT_OK||
     dt_property(&tree,&node,"tx-calibration",&value,&len)!=DT_OK||len!=1024)return 14;
  size_t old_size=dn,offset=(size_t)(value-d)-36u,removed=36u+((len+3u)&~3u);
  uint32_t props=node.n_props;if(!props||offset<node.props_off||offset+removed>dn)return 15;
  for(unsigned i=0;i<len;i++)if(value[i])return 16;
  memmove(d+offset,d+offset+removed,dn-offset-removed);dn-=removed;
  uint32_t remaining=props-1u;for(unsigned i=0;i<4;i++)d[node.offset+i]=(uint8_t)(remaining>>(8u*i));
  if(dt_parse(d,dn,&tree,&rt)!=DT_OK||dt_path(&tree,&rt,"arm-io/sdio",&node)!=DT_OK||
     dt_property(&tree,&node,"tx-calibration",&value,&len)!=DT_ERR_NOT_FOUND||node.n_props!=remaining)return 17;
  snprintf(path,sizeof path,"%s-dt-removal.json",prefix);FILE*dp=fopen(path,"wb");if(!dp)return 18;
  fprintf(dp,"{\"scope\":\"loaded private DT copy only; no RF calibration\",\"removedProperty\":\"arm-io/sdio/tx-calibration\",\"recordOffset\":%zu,\"removedRecordBytes\":%zu,\"originalSize\":%zu,\"newSize\":%zu,\"nodeOffset\":%zu,\"originalPropertyCount\":%u,\"newPropertyCount\":%u}\n",offset,removed,old_size,dn,node.offset,props,remaining);fclose(dp);
 }
 if(local_mac){
  dt_t tree;dt_node_t rt,node;const uint8_t *value;uint32_t len;
  const uint8_t mac[6]={2,0,0,0,1,1};
  if(dt_parse(d,dn,&tree,&rt)!=DT_OK||dt_path(&tree,&rt,"arm-io/sdio",&node)!=DT_OK||
     dt_property(&tree,&node,"local-mac-address",&value,&len)!=DT_OK||len!=6)return 11;
  for(unsigned i=0;i<6;i++)if(value[i]!=0)return 12;
  size_t offset=(size_t)(value-d);memcpy(d+offset,mac,6);
  snprintf(path,sizeof path,"%s-dt-property.json",prefix);FILE*dp=fopen(path,"wb");if(!dp)return 13;
  fprintf(dp,"{\"scope\":\"loaded private DT copy only\",\"path\":\"arm-io/sdio/local-mac-address\",\"offset\":%zu,\"before\":\"00:00:00:00:00:00\",\"after\":\"02:00:00:00:01:01\",\"locallyAdministeredUnicast\":true}\n",offset);fclose(dp);
 }
 if(no_calibration){snprintf(path,sizeof path,"%s-loaded-dt.bin",prefix);FILE*dp=fopen(path,"wb");if(!dp||fwrite(d,1,dn,dp)!=dn)return 19;fclose(dp);}
 const char*unmatch[]={"arm-io/usb-otg"};
 s5l_os1_request_t req={.kernel=k,.kernel_size=kn,.devicetree=d,.devicetree_size=dn,.ramdisk=r,.ramdisk_size=rn,.framebuffer=true,.fb_width=320,.fb_height=480,.debug_enabled=true,.unmatch=unmatch,.unmatch_count=1};s5l_os1_result_t boot;
 if(!s5l_os1_boot(&machine,&req,&boot)){fprintf(stderr,"boot: %s\n",boot.detail);return 5;}free(k);free(d);free(r);
 sdio_config_t config={.present=true,.enumeration=true,.boot_endpoint=true,.data_transfer=true,.helper_receiver=!no_consume,.helper_semantics=main_first,.semantic_ticks=600,.helper_capacity=4096,.data_ticks=600,.endpoint_capacity=main_first?512:64,.io_port=0x10000,.function_init_ticks=1800,.io_functions=1,.assigned_rca=1,.voltage_mask=0x60000,.readable_cccr=0x101,.cccr={0},.command_ticks=600};
 if(!sdio_init(&sdio,&config))return 6;
 /* Revocation is explicit before interposing. Every non-SDIO operation and
  * host RAM read lookup retains the original machine context/callback. */
 if(!s5l8900_set_direct_ram_writes(&machine,false))return 7;
 underlying=machine.bus;
 sdio_bind_dma_reader(&sdio,dma_source,machine.bus.ctx);
 machine.bus.read32=read32;machine.bus.read16=read16;machine.bus.read8=read8;
 machine.bus.write32=write32;machine.bus.write16=write16;machine.bus.write8=write8;
 if(machine.bus.ctx!=&machine||machine.cpu.bus!=&machine.bus||machine.bus.host_ram!=underlying.host_ram||machine.bus.host_ram_write)return 8;
 s5l8900_set_uart0_host(&machine,serial,NULL);
 uint32_t targets[]={0xc0379780u,0xc0381bacu,0xc0381cdcu,0xc0431504u,0xc0431528u,0xc0381bdcu,0xc0381c20u,0xc0381ce0u,0xc0431b04u,0xc0432108u,0xc0431b4cu,0xc0379684u,0xc0431598u,0xc0380300u,0xc038a788u,0xc038a79cu,0xc038a840u,0xc038a8c8u};
 if(!s5l8900_set_pre_step_hook(&machine,probe_hook,NULL,targets,sizeof targets/sizeof targets[0]))return 9;
 arm_status_t status=ARM_OK;
 if(!setjmp(gate_stop)) while(machine.cpu.cycles<((main_negative||main_first)?2000000000u:1500000000u)&&!gate_reached){unsigned n=s5l8900_run(&machine,machine.cpu.cycles>=351000000u?1000:1000000,&status);sync_sdio();if(!n||status!=ARM_OK)break;}
 snprintf(path,sizeof path,"%s-report.json",prefix);FILE*out=fopen(path,"wb");if(!out)return 10;
 fprintf(out,"{\n\"baseline\":%s,\"gateReached\":%s,\"cccrInitializerEntered\":%s,\"pc\":\"%08x\",\"instructions\":%"PRIu64",\"ticks\":%"PRIu64",\"cpuHz\":%u,\"commandTicks\":600,\"accesses\":%"PRIu64",\"statusReads\":%"PRIu64",\"triggers\":%"PRIu64",\"completedResponses\":%"PRIu64",\"transportErrors\":%"PRIu64",\"rejectedWrites\":%"PRIu64",\"cardErrorResponses\":%"PRIu64",\"cccrReads\":%"PRIu64",\"cccrMask\":%u,\"cardState\":%u,\"rca\":%u,\"irq\":%s,\"forwarded\":[%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"]\n}\n",
 baseline?"true":"false",gate_reached?"true":"false",cccr_entered?"true":"false",machine.cpu.r[15],machine.cpu.cycles,machine.timer.ticks,machine.cpu_hz,accesses,status_reads,sdio.triggers,sdio.completions,sdio.transport_errors,sdio.rejected_writes,card_errors,sdio.cccr_reads,sdio.cccr_read_mask,sdio.card.state,sdio.card.rca,sdio_irq_level(&sdio)?"true":"false",forwarded[0],forwarded[1],forwarded[2],forwarded[3],forwarded[4],forwarded[5]);
 fclose(out);
 snprintf(path,sizeof path,"%s-endpoint.json",prefix);out=fopen(path,"wb");if(!out)return 20;
 fprintf(out,"{\"tbHz\":%u,\"functionInitTicks\":%"PRIu64",\"iorxReads\":%"PRIu64",\"iorxWaitReads\":%"PRIu64",\"iorxReadyReads\":%"PRIu64",\"functionReads\":%"PRIu64",\"initialized\":%u,\"enabled\":%u,\"ready\":%u,\"hostPower\":%u,\"capacity\":%u,\"used\":%u,\"pendingCommand\":%u,\"pendingArgument\":%u,\"bufferAddress\":%u,\"blockLength\":%u,\"blockCount\":%u,\"captured64\":%u}\n",machine.tb_hz,sdio.config.function_init_ticks,sdio.iorx_reads,sdio.iorx_wait_reads,sdio.iorx_ready_reads,sdio.function_reads,sdio.endpoint.initialized,sdio.endpoint.enabled,sdio.endpoint.ready,sdio.endpoint.host_power,sdio.endpoint.capacity,sdio.endpoint.used,sdio.pending_command,sdio.pending_argument,sdio.buffer_address,sdio.block_length,sdio.block_count,helper_frame_captured);
 fclose(out);
 snprintf(path,sizeof path,"%s-data.json",prefix);out=fopen(path,"wb");if(!out)return 21;
 fprintf(out,"{\"dataStarts\":%"PRIu64",\"dataCompletions\":%"PRIu64",\"dataErrors\":%"PRIu64",\"dmaSourceLookups\":%"PRIu64",\"irqHandlerEntered\":%s,\"completeDMAEntered\":%s,\"dataTicks\":600,\"used\":%u,\"irqPending\":%u,\"events\":%u}\n",sdio.data_starts,sdio.data_completions,sdio.data_errors,dma_source_reads,irq_entered?"true":"false",dma_entered?"true":"false",sdio.endpoint.used,sdio.irq_pending,sdio.events);fclose(out);
 snprintf(path,sizeof path,"%s-received-helper.bin",output_prefix);out=fopen(path,"wb");if(!out)return 23;
 if(fwrite(sdio.helper.memory,1,sdio.helper.used,out)!=sdio.helper.used)return 24;fclose(out);
 snprintf(path,sizeof path,"%s-helper.json",output_prefix);out=fopen(path,"wb");if(!out)return 25;
 fprintf(out,"{\"noConsume\":%s,\"mainNegative\":%s,\"helperBytes\":%u,\"helperFrames\":%u,\"terminated\":%s,\"protocolErrors\":%"PRIu64",\"completionReturns\":%"PRIu64",\"sourceWireBytes\":%"PRIu64",\"wireOK\":%s,\"allTransfersOK\":%s}\n",no_consume?"true":"false",main_negative?"true":"false",sdio.helper.used,sdio.helper.frames,sdio.helper.terminated?"true":"false",sdio.helper_protocol_errors,completion_returns,source_wire_bytes,wire_ok?"true":"false",all_transfers_ok?"true":"false");fclose(out);
 if(no_consume){snprintf(path,sizeof path,"%s-retained-fifo.bin",prefix);out=fopen(path,"wb");if(!out)return 27;if(fwrite(sdio.endpoint.receive,1,sdio.endpoint.used,out)!=sdio.endpoint.used)return 28;fclose(out);}
 if(main_first){
  snprintf(path,sizeof path,"%s-main-model.json",prefix);out=fopen(path,"wb");if(!out)return 29;
  fprintf(out,"{\"semanticModelOnly\":true,\"phase\":%u,\"requested\":%u,\"headers\":%"PRIu64",\"bodies\":%"PRIu64",\"copied\":%"PRIu64",\"errors\":%"PRIu64",\"publications\":%"PRIu64",\"mainCallReturns\":%"PRIu64",\"mainLengthPairs\":%"PRIu64",\"mainSequenceOK\":%s,\"mainCallsOK\":%s}\n",sdio.main_model.phase,sdio.main_model.requested,sdio.main_model.headers,sdio.main_model.bodies,sdio.main_model.copied,sdio.main_model.errors,sdio.main_model.publications,main_call_returns,main_length_pairs,main_sequence_ok?"true":"false",main_call_ok?"true":"false");fclose(out);
  snprintf(path,sizeof path,"%s-card-ram.bin",prefix);out=fopen(path,"wb");if(!out)return 30;
  if(fwrite(sdio.main_model.memory,1,sizeof sdio.main_model.memory,out)!=sizeof sdio.main_model.memory)return 31;fclose(out);
 }
 fclose(request_wire_file);fclose(wire_file);fclose(trace_file);fclose(serial_file);s5l8900_free(&machine);return gate_reached?0:1;
}
