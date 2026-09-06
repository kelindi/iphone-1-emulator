#include "board.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint64_t digest(const uint8_t*p,size_t n){uint64_t h=UINT64_C(1469598103934665603);for(size_t i=0;i<n;i++)h=(h^p[i])*UINT64_C(1099511628211);return h;}
int main(int argc,char**argv){if(argc<2)return 2;s5l8900_t*m=calloc(1,sizeof*m);if(!m||!s5l_board_init(s5l_board_find("m68"),m,256u<<20))return 3;uint8_t*data=NULL;size_t n=0;
 if(!strcmp(argv[1],"emit")){if(argc<3)return 4;m->ram[0x12000]=73;if(snapshot_save_mem(m,&data,&n)!=SNAP_OK)return 5;fprintf(stderr,"RAW bytes=%zu digest=%016llx\n",n,(unsigned long long)digest(data,n));FILE*f=fopen(argv[2],"wb");if(!f)return 6;size_t wrote=argc==4?37:n;int ok=fwrite(data,1,wrote,f)==wrote;if(fclose(f))ok=0;free(data);s5l8900_free(m);free(m);return argc==4?7:ok?0:8;}
 if(!strcmp(argv[1],"verify")){size_t cap=1024*1024;data=malloc(cap);if(!data)return 9;for(;;){if(n==cap){if(cap>=512u*1024*1024)return 10;cap*=2;void*p=realloc(data,cap);if(!p)return 11;data=p;}size_t r=fread(data+n,1,cap-n,stdin);n+=r;if(!r)break;}if(ferror(stdin))return 12;snapshot_status_t st=snapshot_load_mem(m,data,n);fprintf(stderr,"VERIFY status=%d bytes=%zu digest=%016llx\n",st,n,(unsigned long long)digest(data,n));free(data);s5l8900_free(m);free(m);return st==SNAP_OK?0:13;}
 return 14;
}
