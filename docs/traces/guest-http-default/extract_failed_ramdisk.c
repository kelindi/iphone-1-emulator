/* Read-only original-OS1 process inventory on a separately loaded snapshot.
 * Historical layout provenance: .context/organizer-debug/processes.c;
 * exact kernel binding belongs to the accompanying launcher receipt.
 * No guest instructions, host clock, MMIO or original CPU mutations.
 */
#include "board.h"
#include "snapshot.h"
#include "peek.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint32_t address,pid,task,threads;char name[17];} process;
static int read_word(const s5l8900_t*m,const arm_cpu_t*view,uint32_t va,uint32_t*out){
 return !(va&3u)&&amc_peek_ram(view,m->ram,m->ram_base,m->ram_size,va,out,4);
}
static int kernel_pointer(uint32_t p){return p>=0xc0000000u&&p<=0xfffff000u&&!(p&3u);}
static void string(const char*s){putchar('"');for(;*s;s++){if(*s=='"'||*s=='\\')putchar('\\');putchar(*s);}putchar('"');}
int main(int argc,char**argv){
 if(argc<2){fprintf(stderr,"usage: processes SNAPSHOT|- [required-exact-name ...]\n");return 2;}
 FILE*input=!strcmp(argv[1],"-")?stdin:fopen(argv[1],"rb");if(!input)return 3;
 size_t cap=1u<<20,n=0;uint8_t*raw=malloc(cap);if(!raw)return 4;
 for(;;){if(n==cap){if(cap>=512u*1024u*1024u)return 5;cap*=2;void*p=realloc(raw,cap);if(!p)return 6;raw=p;}
  size_t got=fread(raw+n,1,cap-n,input);n+=got;if(!got)break;}
 if(ferror(input))return 7;if(input!=stdin)fclose(input);
 if(n<40||memcmp(raw,"S5LBox SNAPSHOT",16)||raw[16]!=39||raw[17]||raw[18]||raw[19]){free(raw);return 7;}
 s5l8900_t*m=calloc(1,sizeof*m);if(!m||!s5l_board_init(s5l_board_find("m68"),m,256u<<20))return 8;
 snapshot_status_t loaded=snapshot_load_mem(m,raw,n);free(raw);
 if(loaded!=SNAP_OK){fprintf(stderr,"snapshot load failed: %d\n",loaded);s5l8900_free(m);free(m);return 9;}
 if(m->ram_size!=(256u<<20)||!m->mtz1.present){fprintf(stderr,"expected M68 256MiB snapshot\n");s5l8900_free(m);free(m);return 9;}
 const uint32_t start=0x085d0000u, size=0x0a800000u;
 if(m->ram_base!=0x08000000u || start<m->ram_base || (uint64_t)(start-m->ram_base)+size>m->ram_size)return 12;
 const unsigned char *image=m->ram+(start-m->ram_base);
 if(memcmp(image+1024,"HX",2)||image[1064]!=0||image[1065]!=0||image[1066]!=0x10||image[1067]!=0)return 13;
 int ok=fwrite(image,1,size,stdout)==size&&!fflush(stdout);
 s5l8900_free(m);free(m);return ok?0:14;
}
