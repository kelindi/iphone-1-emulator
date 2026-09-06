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
 arm_cpu_t*original=malloc(sizeof*original);arm_cpu_t*view=malloc(sizeof*view);if(!original||!view)return 10;
 memcpy(original,&m->cpu,sizeof*original);memcpy(view,original,sizeof*view);
 /* Inspection is deliberately privileged, in a COPIED CPU only. Kernel
  * mappings remain in the actual captured address space; each helper read
  * flushes only its own further clone and uses a RAM-only page-table bus. */
 view->cpsr=(view->cpsr&~31u)|ARM_MODE_SVC;
 process rows[300];unsigned count=0;uint32_t p=0;const char*error=NULL;
 if(!read_word(m,view,0xc01e042cu,&p))error="allproc-unreadable";
 while(!error&&p){
  if(count==300){error="process-limit";break;}
  if(!kernel_pointer(p)){error="invalid-process-pointer";break;}
  for(unsigned i=0;i<count;i++)if(rows[i].address==p)error="process-list-cycle";
  if(error)break;
  process*r=&rows[count];memset(r,0,sizeof*r);r->address=p;uint32_t next;
  if(!read_word(m,view,p,&next)||!read_word(m,view,p+0x24u,&r->pid)||
     !read_word(m,view,p+0x138u,&r->task)||!kernel_pointer(r->task)||
     !read_word(m,view,r->task+0x30u,&r->threads)||
     !amc_peek_ram(view,m->ram,m->ram_base,m->ram_size,p+0xebu,r->name,16)){
    error="process-fields-unreadable";break;}
  if(r->pid>0x7fffffffu||r->threads>65536u){error="process-field-bound";break;}
  for(unsigned i=0;i<16&&r->name[i];i++)if((unsigned char)r->name[i]<32||(unsigned char)r->name[i]>126)error="non-ascii-process-name";
  for(unsigned i=0;i<count;i++)if(rows[i].pid==r->pid)error="duplicate-process-id";
  if(!r->name[0])error="empty-process-name";
  if(error)break;count++;p=next;
 }
 int complete=!error&&!p;
 if(!error&&!count)error="empty-process-list";
 if(memcmp(original,&m->cpu,sizeof*original))error="inspection-mutated-live-loaded-CPU";
 if(!error)for(int i=2;i<argc;i++){unsigned found=0;for(unsigned j=0;j<count;j++)if(!strcmp(argv[i],rows[j].name)&&rows[j].threads)found++;if(found!=1){error="required-process-not-present-with-threads";break;}}
 printf("{\"passed\":%s,\"error\":",error?"false":"true");if(error)string(error);else printf("null");
 printf(",\"snapshotBytes\":%zu,\"cycles\":%llu,\"capturedTTBR\":\"%08x\",\"capturedCPSR\":\"%08x\",\"loadedCPUUnchanged\":%s,\"completeList\":%s,\"processes\":[",n,(unsigned long long)m->cpu.cycles,original->cp15.ttbr0,original->cpsr,memcmp(original,&m->cpu,sizeof*original)?"false":"true",complete?"true":"false");
 for(unsigned i=0;i<count;i++){process*r=&rows[i];if(i)putchar(',');printf("{\"address\":\"%08x\",\"pid\":%u,\"name\":",r->address,r->pid);string(r->name);printf(",\"task\":\"%08x\",\"threads\":%u}",r->task,r->threads);}
 printf("],\"scope\":\"Read-only bounded process-list presence and thread counts; no scheduler or service-response proof.\"}\n");
 free(view);free(original);s5l8900_free(m);free(m);return error?11:0;
}
