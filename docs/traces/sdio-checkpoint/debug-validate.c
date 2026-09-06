#include "checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t le(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int main(void){uint8_t h[8];if(fread(h,1,8,stdin)!=8)return 1;size_t mn=le(h),sn=le(h+4);if(!mn||mn>300u*1024*1024||!sn||sn>300000)return 2;
 uint8_t*m=malloc(mn),*s=malloc(sn);if(!m||!s){free(m);free(s);return 3;}if(fread(m,1,mn,stdin)!=mn||fread(s,1,sn,stdin)!=sn||fgetc(stdin)!=EOF){free(m);free(s);return 4;}
 checkpoint_phone_t*p=checkpoint_pair_load(m,mn,s,sn);if(!p){free(m);free(s);return 5;}
 uint8_t*om=NULL,*os=NULL;size_t on=0,ol=0;bool ok=checkpoint_pair_save(p,&om,&on,&os,&ol)&&on==mn&&ol==sn&&!memcmp(m,om,mn)&&!memcmp(s,os,sn);
 FILE*f=fopen(".context/sdio-first/checkpoint/full-attempt-1/debug-restored.snap","wb");if(f){fwrite(om,1,on,f);fclose(f);}f=fopen(".context/sdio-first/checkpoint/full-attempt-1/debug-restored.side","wb");if(f){fwrite(os,1,ol,f);fclose(f);}fprintf(stderr,"canonical machine=%d side=%d lengths=%zu/%zu %zu/%zu\n",on==mn&&!memcmp(m,om,mn),ol==sn&&!memcmp(s,os,sn),on,mn,ol,sn);free(m);free(s);free(om);free(os);checkpoint_phone_free(p);free(p);if(!ok)return 6;puts("PASS canonical machine and sidecar bytes after fresh-machine load");return 0;}
