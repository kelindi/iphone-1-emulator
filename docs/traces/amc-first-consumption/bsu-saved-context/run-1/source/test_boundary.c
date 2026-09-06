#include "amc_engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;static uint8_t stream[400000],expected[8192];
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"boundary:%u %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
 const amc_codec_ops*o=&amc_faad_ops;FILE*f=fopen(".context/amc-first/stage-e/framing/fixture/stream.aac","rb");CHECK(f);size_t length=fread(stream,1,sizeof stream,f);fclose(f);CHECK(length==364719);
 FILE*s=fopen(".context/amc-first/stage-e/framing/fixture/sizes.oracle","r"),*pcm=fopen(".context/amc-first/stage-e/framing/run-1/packetized.pcm","rb");CHECK(s&&pcm);void*h=o->open();CHECK(h&&!o->at_frame_boundary(h));CHECK(o->configure(h,48000,2)&&!o->at_frame_boundary(h));
 size_t offset=0,total=0;unsigned n,frames=0;while(fscanf(s,"%u",&n)==1){amc_decoded d={0};CHECK(offset+n<=length&&n<=4096);CHECK(o->decode(h,stream+offset,n,&d));CHECK(d.consumed==n&&o->at_frame_boundary(h));CHECK(d.samples*2<=sizeof expected);CHECK(fread(expected,1,d.samples*2,pcm)==d.samples*2);CHECK(!d.samples||!memcmp(expected,d.pcm,d.samples*2));total+=d.samples*2;offset+=n;frames++;}
 CHECK(frames==1069&&offset==length&&total==4374528&&fgetc(pcm)==EOF);fclose(s);fclose(pcm);
 /* A disposable incomplete probe cannot move or poison this live history. */
 for(unsigned cut=0;cut<371;cut++){amc_decoded d={0};CHECK(!o->probe(stream+16065,cut,48000,&d));CHECK(o->at_frame_boundary(h));}
 /* A live parser failure explicitly loses the save boundary. */
 amc_decoded d={0};CHECK(!o->decode(h,stream+16065,1,&d));CHECK(!o->at_frame_boundary(h));o->close(h);
 printf("PASS %u actual-FAAD boundary checks,1069 exact PCM frames,371 disposable partial probes; no guest\n",checks);
}
