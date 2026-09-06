/* Standalone real-codec ownership feasibility; releases here are test calls,
 * never evidence of guest consumption. Engine source remains unchanged. */
#define main retained_contract_main
#include "retained-engine-contract.c"
#undef main
static void conservation(void){
 CHECK(engine.copied_bytes>=engine.decoded_bytes&&engine.copied_bytes-engine.decoded_bytes==engine.queue_used);
 CHECK(engine.queue_used<=AMC_QUEUE_BYTES&&engine.copied_bytes<=81920);
 CHECK(!memcmp(engine.queue,stream+engine.decoded_bytes,engine.queue_used));
 CHECK(engine.sample_units==engine.published_frames*2048);
}
int main(int argc,char**argv){
 struct rlimit a={256u*1024u*1024u,256u*1024u*1024u},c={30,30};CHECK(!setrlimit(RLIMIT_AS,&a));CHECK(!setrlimit(RLIMIT_CPU,&c));alarm(45);
 CHECK(argc==5);CHECK(load(argv[1],original,sizeof original)==sizeof original);CHECK(load(argv[2],coverage,sizeof coverage)==sizeof coverage);stream_n=load(argv[3],stream,sizeof stream);pcm_n=load(argv[4],pcm,sizeof pcm);CHECK(stream_n==364719&&pcm_n==4374528);
 fresh();setup();configure();memcpy(memory,stream,65536);stage_dma(physical.base,16384,true);
 for(unsigned i=0;!engine.published_frames&&i<20;i++){amc_engine_result r=amc_engine_step(&engine,&physical);CHECK(r==AMC_ENGINE_OK||r==AMC_ENGINE_PUBLISHED);conservation();}
 CHECK(engine.published_frames==1);compare_publication(0);
 while(engine.dma_active){CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);conservation();compare_publication(0);}
 memcpy(memory,stream+65536,16384);stage_dma(physical.base,4096,false);
 CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);CHECK(engine.queue_used==65536&&engine.copied_words==87);conservation();
 save_before();for(unsigned i=0;i<8;i++)CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);unchanged();
 /* Without owner0 release, ack/service may publish slot1 only, then must wait. */
 write_reg(4,1);write_reg(0xb04,0x400);CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_PUBLISHED);compare_publication(0);compare_publication(1);conservation();
 write_reg(4,1);write_reg(0xb04,0x400);
 while(engine.dma_active&&engine.queue_used+4<=AMC_QUEUE_BYTES){CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);conservation();}
 save_before();for(unsigned i=0;i<8;i++)CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);unchanged();compare_publication(0);compare_publication(1);
 reject_sram(AMC_HEADER+8,2,0);reject_sram(AMC_HEADER+8,4,1);
 /* Recycle only canonical owned slots through the same public register API. */
 CHECK(amc_engine_sram_write(&engine,AMC_HEADER+8,2,1)==AMC_ENGINE_OK);
 for(unsigned publication=2;publication<64;publication++){
  if(publication>2)consume_publication(publication-1);
  unsigned slot=publication&1;CHECK(half(AMC_HEADER+8+2*slot)==1);
  amc_engine_result r=amc_engine_step(&engine,&physical);CHECK(r==AMC_ENGINE_PUBLISHED);CHECK(engine.published_frames==publication+1);compare_publication(publication);conservation();
  /* Keep the other slot reusable after its already-verified previous data. */
  unsigned other=slot^1;if(half(AMC_HEADER+8+2*other)==0)CHECK(amc_engine_sram_write(&engine,AMC_HEADER+8+2*other,2,1)==AMC_ENGINE_OK);
  memcpy(prior_sram,sram.bytes,AMC_SRAM_SIZE);
  /* Pending output IRQ blocks another publication even after DMA progress. */
  uint64_t count=engine.published_frames;CHECK(amc_engine_step(&engine,&physical)==AMC_ENGINE_WAIT);CHECK(engine.published_frames==count&&!memcmp(prior_sram,sram.bytes,AMC_SRAM_SIZE));conservation();
 }
 CHECK(engine.published_frames==64);masks();amc_engine_destroy(&engine);CHECK(allocations==frees);
 printf("PASS %u checks:64 real-codec publications, no owned-slot overwrite, exact PCM/queue, partial DMA backpressure; test-driven releases only, no guest\n",checks);
}
