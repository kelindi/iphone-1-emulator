/* Isolated experimental adapter. Linked only to the pinned local GPL FAAD2
 * build described by milestone072; not a production dependency. */
#include "amc_engine.h"
#include <neaacdec.h>
#include <string.h>
#include <stdlib.h>
typedef struct {NeAACDecHandle handle;bool configured,boundary,in_call;} owned_decoder;

static void *open_decoder(void) {
    NeAACDecHandle h=NeAACDecOpen();
    if(!h)return NULL;
    NeAACDecConfigurationPtr cfg=NeAACDecGetCurrentConfiguration(h);
    if(!cfg){NeAACDecClose(h);return NULL;}
    cfg->outputFormat=FAAD_FMT_16BIT;cfg->dontUpSampleImplicitSBR=1;
    if(!NeAACDecSetConfiguration(h,cfg)){NeAACDecClose(h);return NULL;}
    owned_decoder *d=calloc(1,sizeof *d);if(!d){NeAACDecClose(h);return NULL;}d->handle=h;return d;
}
static void close_decoder(void *p){owned_decoder*d=p;if(d){NeAACDecClose(d->handle);free(d);}}
static bool at_boundary(const void*p){const owned_decoder*d=p;return d&&d->configured&&d->boundary&&!d->in_call;}
static bool configure(void *p,unsigned rate,unsigned channels) {
    owned_decoder*d=p;if(!d||d->in_call||rate!=48000||channels!=2)return false;NeAACDecHandle h=d->handle;
    d->configured=d->boundary=false;
    /* Explicit exact-program LC/stereo policy plus device session rate.
     * No original ASC/file/private packet metadata enters this adapter. */
    unsigned object=2, rate_index=3;
    unsigned bits=(object<<11)|(rate_index<<7)|(channels<<3);
    unsigned char asc[2]={(unsigned char)(bits>>8),(unsigned char)bits};
    unsigned long actual_rate=0;unsigned char actual_channels=0;
    d->configured=!NeAACDecInit2(h,asc,sizeof asc,&actual_rate,&actual_channels)&&actual_rate==rate&&actual_channels==channels;
    /* No completed frame exists yet. A save requires a successful decode. */
    return d->configured;
}
static bool decode(void *p,const uint8_t *bytes,size_t n,amc_decoded *out) {
    owned_decoder*d=p;if(!d||!d->configured||d->in_call||!bytes||!n||n>AMC_QUEUE_BYTES||!out)return false;NeAACDecHandle h=d->handle;
    /* The supported raw subset begins with a channel-pair element (ID_CPE=1).
     * Pinned syntax.c rejects PCE after any earlier element. Together this
     * preflight and that parser rule exclude PCE in every position, without
     * reading a private packet table or writing a second AAC parser. */
    if((bytes[0]>>5)!=1)return false;
    NeAACDecFrameInfo f;memset(&f,0,sizeof f);
    d->boundary=false;d->in_call=true;
    void *pcm=NeAACDecDecode(h,&f,(unsigned char *)bytes,(unsigned long)n);
    d->in_call=false;
    if(f.error||!f.bytesconsumed||f.bytesconsumed>n||f.channels!=2||
       f.samplerate!=48000||f.object_type!=LC||f.header_type!=RAW||
       f.sbr!=NO_SBR||f.ps||f.samples>AMC_OUTPUT_BYTES/2||
       (f.samples&1)||(!pcm&&f.samples))return false;
    /* Pinned non-DRM raw_data_block byte-aligns before successful return;
     * decoder.c ends its local bit reader. No in-flight reservoir is retained. */
    d->boundary=true;*out=(amc_decoded){f.bytesconsumed,f.samples,pcm};return true;
}
static bool probe(const uint8_t *bytes,size_t n,unsigned rate,amc_decoded *out) {
    void *h=open_decoder();if(!h)return false;
    amc_decoded d={0};bool okay=configure(h,rate,2)&&decode(h,bytes,n,&d);
    /* Scratch PCM dies here and must never be published. */
    if(okay){d.pcm=NULL;*out=d;}close_decoder(h);return okay;
}
const amc_codec_ops amc_faad_ops={open_decoder,close_decoder,configure,probe,decode,at_boundary};
