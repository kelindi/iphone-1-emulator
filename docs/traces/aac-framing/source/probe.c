/* Offline decoder/framing feasibility. No device, guest RAM, MMIO or private
 * packet metadata in streaming mode. The packetized control is a separate mode. */
#include <neaacdec.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#c); exit(2); } } while(0)
static unsigned char asc[64]; static size_t asc_n;
static unsigned attempts, committed;
static unsigned char *read_file(const char *path, size_t *n, size_t cap) {
    FILE *f=fopen(path,"rb"); CHECK(f); CHECK(!fseek(f,0,SEEK_END));
    long len=ftell(f); CHECK(len>=0 && (unsigned long)len<=cap); rewind(f);
    unsigned char *b=malloc(len ? (size_t)len : 1); CHECK(b);
    CHECK(fread(b,1,len,f)==(size_t)len); CHECK(!fclose(f)); *n=len; return b;
}
static NeAACDecHandle decoder(void) {
    NeAACDecHandle h=NeAACDecOpen(); CHECK(h);
    NeAACDecConfigurationPtr c=NeAACDecGetCurrentConfiguration(h); CHECK(c);
    c->outputFormat=FAAD_FMT_16BIT; c->dontUpSampleImplicitSBR=1;
    CHECK(NeAACDecSetConfiguration(h,c));
    unsigned long rate=0; unsigned char channels=0;
    CHECK(!NeAACDecInit2(h,asc,asc_n,&rate,&channels));
    CHECK(rate==48000 && channels==2); return h;
}
static int valid(NeAACDecFrameInfo *i, size_t supplied) {
    return !i->error && i->bytesconsumed && i->bytesconsumed<=supplied &&
        i->channels==2 && i->samplerate==48000 && i->object_type==LC &&
        i->header_type==RAW && i->sbr==NO_SBR && !i->ps &&
        i->samples<=4096 && !(i->samples&1);
}
static size_t discover(unsigned char *b, size_t n, NeAACDecFrameInfo *info) {
    CHECK(n<=65536); CHECK(++attempts<=2200);
    NeAACDecHandle scratch=decoder();
    memset(info,0,sizeof(*info));
    NeAACDecDecode(scratch,info,b,n);
    size_t consumed=valid(info,n) ? info->bytesconsumed : 0;
    NeAACDecClose(scratch); return consumed;
}
static void commit(NeAACDecHandle h,unsigned char *b,size_t n,size_t offset,FILE *pcm,FILE *trace) {
    NeAACDecFrameInfo i; memset(&i,0,sizeof(i));
    void *samples=NeAACDecDecode(h,&i,b,n);
    CHECK(valid(&i,n)); CHECK(i.bytesconsumed==n); CHECK(++committed<=1069);
    CHECK(!i.samples || samples);
    CHECK(fwrite(samples,2,i.samples,pcm)==i.samples);
    fprintf(trace,"%u,%zu,%zu,%lu,%lu,%u,%u,%u,%u\n",committed,offset,n,i.samples,i.samplerate,i.channels,i.object_type,i.sbr,i.ps);
}
int main(int argc,char **argv) {
    CHECK(argc>=6);
    struct rlimit address={256u*1024u*1024u,256u*1024u*1024u};
    struct rlimit cpu={120,120}; CHECK(!setrlimit(RLIMIT_AS,&address)); CHECK(!setrlimit(RLIMIT_CPU,&cpu)); alarm(180);
    size_t n; unsigned char *data=read_file(argv[2],&n,1024*1024);
    unsigned char *config=read_file(argv[3],&asc_n,sizeof(asc)); memcpy(asc,config,asc_n); free(config);
    FILE *pcm=fopen(argv[4],"wb"), *trace=fopen(argv[5],"w"); CHECK(pcm&&trace);
    fprintf(trace,"frame,offset,consumed,samples,rate,channels,object,sbr,ps\n");
    NeAACDecHandle live=decoder(); size_t offset=0;
    if (!strcmp(argv[1],"packetized")) {
        CHECK(argc==7); FILE *sizes=fopen(argv[6],"r"); CHECK(sizes); unsigned size;
        while(fscanf(sizes,"%u",&size)==1) {
            CHECK(size>0&&size<=65536&&size<=n-offset);
            commit(live,data+offset,size,offset,pcm,trace); offset+=size;
        }
        CHECK(feof(sizes)); fclose(sizes); CHECK(offset==n);
    } else if (!strcmp(argv[1],"raw") || !strcmp(argv[1],"cut")) {
        CHECK(argc==6); int cut=!strcmp(argv[1],"cut"); size_t available=cut ? 16384 : n; int crossed=0;
        CHECK(available<=n);
        while(offset<n) {
            size_t amount=available-offset; if(amount>65536)amount=65536;
            NeAACDecFrameInfo i; size_t size=discover(data+offset,amount,&i);
            if(!size) {
                fprintf(stderr,"UNRESOLVED offset%zu available%zu error%u reportedConsumed%lu committed%u\n",offset,amount,i.error,i.bytesconsumed,committed);
                CHECK(cut && !crossed && available==16384);
                /* This delivery schedule is explicit offline fixture input.
                 * It is never a model read from the original media file. */
                available+=52; crossed=1;
                fprintf(stderr,"APPEND original52 bytes; live decoder untouched\n");
                continue;
            }
            commit(live,data+offset,size,offset,pcm,trace); offset+=size;
            if(cut && crossed==1 && offset==available) {
                fprintf(stderr,"CONTINUATION boundary%zu committed%u\n",offset,committed);
                crossed=2; available=n;
            }
        }
        CHECK(!cut || crossed==2);
    } else if(!strcmp(argv[1],"truncate")) {
        CHECK(argc==6 && n==371);
        for(size_t cut=0;cut<=n;cut++) {
            NeAACDecFrameInfo i; size_t size=discover(data,cut,&i);
            fprintf(trace,"%zu,%zu,%u,%lu\n",cut,size,i.error,i.bytesconsumed);
            CHECK(cut==n ? size==n : size==0);
        }
    } else CHECK(0);
    NeAACDecClose(live); CHECK(!fclose(pcm)); CHECK(!fclose(trace)); free(data);
    printf("PASS mode%s committed%u consumed%zu attempts%u\n",argv[1],committed,offset,attempts);
    return 0;
}
