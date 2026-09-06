/* Full corpus replay of the actual engine adapter. No packet-size table or
 * original ASC is supplied: configuration uses the declared policy fields. */
#include "amc_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#c);exit(2);}}while(0)
static uint8_t input[400000];
int main(int argc,char **argv) {
    CHECK(argc==4);struct rlimit a={256u*1024u*1024u,256u*1024u*1024u},c={120,120};
    CHECK(!setrlimit(RLIMIT_AS,&a));CHECK(!setrlimit(RLIMIT_CPU,&c));alarm(180);
    FILE *f=fopen(argv[1],"rb");CHECK(f);size_t n=fread(input,1,sizeof input,f);CHECK(feof(f));fclose(f);
    FILE *pcm=fopen(argv[2],"wb"),*csv=fopen(argv[3],"w");CHECK(pcm&&csv);
    void *live=amc_faad_ops.open();CHECK(live);CHECK(amc_faad_ops.configure(live,48000,2));
    size_t offset=0;unsigned frames=0;uint64_t samples=0;
    fprintf(csv,"frame,offset,bytes,samples\n");
    while(offset<n) {
        size_t available=n-offset;if(available>AMC_QUEUE_BYTES)available=AMC_QUEUE_BYTES;
        amc_decoded p={0},d={0};CHECK(++frames<=1069);
        CHECK(amc_faad_ops.probe(input+offset,available,48000,&p));
        CHECK(p.consumed&&p.consumed<=available);
        CHECK(amc_faad_ops.decode(live,input+offset,p.consumed,&d));
        CHECK(d.consumed==p.consumed&&d.samples<=4096&&!(d.samples&1));
        for(size_t i=0;i<d.samples;i++) {
            uint16_t value=(uint16_t)d.pcm[i];
            CHECK(fputc(value&255,pcm)!=EOF);CHECK(fputc(value>>8,pcm)!=EOF);
        }
        fprintf(csv,"%u,%zu,%zu,%zu\n",frames,offset,d.consumed,d.samples);
        offset+=d.consumed;samples+=d.samples;
    }
    amc_faad_ops.close(live);CHECK(!fclose(pcm));CHECK(!fclose(csv));
    CHECK(frames==1069);printf("PASS actual adapter1069 frames bytes%zu sampleUnits%llu; no ASC or packet metadata supplied\n",offset,(unsigned long long)samples);
    return 0;
}
