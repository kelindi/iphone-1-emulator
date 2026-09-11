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

 if(!error){
  uint32_t task=0,map=0,pmap=0,ttbr=0;
  for(unsigned i=0;i<count;i++)if(rows[i].pid==1&&!strcmp(rows[i].name,"launchd"))task=rows[i].task;
  if(!task||!read_word(m,view,task+0x14,&map)||!read_word(m,view,map+0x24,&pmap)||!read_word(m,view,pmap+4,&ttbr))error="launchd-pmap-unreadable";
  else{
   view->cp15.ttbr0=ttbr;arm_mmu_tlb_flush(view);
{static const unsigned char expected[]={240,64,45,233,12,112,141,226,0,13,45,233,4,208,77,226,113,16,239,230,0,128,160,225,1,0,160,227,0,16,141,229,5,62,0,235,0,176,80,226,200,0,0,10,216,0,136,226,50,61,0,235,0,16,80,226,2,0,0,10,11,0,160,225,24,35,159,229,243,61,0,235,208,0,216,229,32,3,160,225,1,0,0,226,70,62,0,235,0,16,80,226,2,0,0,10,11,0,160,225,248,34,159,229,234,61,0,235,180,0,152,229,192,31,160,225,201,61,0,235,0,16,80,226,2,0,0,10,11,0,160,225,220,34,159,229,226,61,0,235,172,0,152,229,0,0,80,227,6,0,0,10,192,31,160,225,191,61,0,235,0,16,80,226,2,0,0,10,11,0,160,225,184,34,159,229,216,61,0,235,196,0,152,229,192,31,160,225,183,61,0,235,0,16,80,226,2,0,0,10,11,0,160,225,156,34,159,229,208,61,0,235,140,0,152,229,0,0,80,227,5,0,0,10,6,61,0,235,0,16,80,226,2,0,0,10,11,0,160,225,124,34,159,229,199,61,0,235,164,0,152,229,0,0,80,227};unsigned char actual[sizeof expected];if(!amc_peek_ram(view,m->ram,m->ram_base,m->ram_size,0x43bc,actual,sizeof actual)||memcmp(actual,expected,sizeof actual)){fprintf(stderr,"Original launchd code mismatch\n");return 19;}}{static const unsigned char expected[]={176,64,45,233,0,64,160,225,208,48,209,225,8,112,141,226,1,80,160,225,0,0,83,227,4,0,0,26,88,0,159,229,0,64,144,229,18,0,0,234,0,64,160,225,16,0,0,234,216,0,128,226,45,60,0,235,0,0,80,227,40,64,148,21,6,0,0,26,10,0,0,234,4,0,160,225,5,16,160,225,234,255,255,235,0,0,80,227,242,255,255,26,4,64,148,229,0,0,84,227,247,255,255,26,180,59,0,235,3,48,160,227,0,48,128,229,4,0,160,225,176,128,189,232};unsigned char actual[sizeof expected];if(!amc_peek_ram(view,m->ram,m->ram_base,m->ram_size,0x48a4,actual,sizeof actual)||memcmp(actual,expected,sizeof actual)){fprintf(stderr,"Original launchd code mismatch\n");return 19;}}{static const unsigned char expected[]={176,64,45,233,8,112,141,226,76,208,77,226,0,64,160,225,0,0,160,227,142,59,0,235,200,48,148,229,7,16,160,227,112,35,159,229,0,80,99,224,4,0,160,225,116,255,255,235,184,0,148,229,0,0,80,227,9,0,0,10,139,58,0,235,0,0,80,227,4,0,0,10,76,3,159,229,76,19,159,229,76,35,159,229,76,51,159,229,203,246,255,235,0,48,160,227,184,48,132,229,188,0,148,229,0,0,80,227,9,0,0,10,126,58,0,235,1,0,112,227,4,0,0,26,24,3,159,229,24,19,159,229,32,35,159,229,32,51,159,229,190,246,255,235,0,48,160,227,188,48,132,229,172,0,148,229,72,16,141,226,0,32,160,227,13,48,160,225,9,59,0,235,1,0,112,227,178,0,0,26,224,2,159,229,224,18,159,229,240,34,159,229,240,50,159,229,176,246,255,235,176,0,0,234,4,0,160,225,7,16,160,227,224,34,159,229,73,255,255,235,128,0,148,229,0,16,160,227,72,32,157,229,185,42,0,235,0,0,80,227,4,0,0,10,160,2,159,229,160,18,159,229,188,34,159,229,188,50,159,229,160,246,255,235,0,48,160,227,128,48,132,229,44,32,148,229,0,48,157,229,168,2,159,229,2,16,131,224,48,32,148,229,4,48,157,229,44,16,132,229,2,48,131,224,0,0,83,225,48,48,132,229,61,57,67,194,1,32,129,194,9,61,67,194,44,32,132,197,48,48,132,197,52,32,148,229,8,48,157,229,2,16,131,224,56,32,148,229,12,48,157,229,52,16,132,229,2,48,131,224,0,0,83,225,56,48,132,229,61,57,67,194,1,32,129,194,9,61,67,194,52,32,132,197,56,48,132,197,60,32,148,229,16,48,157,229,3,48,130,224,60,48,132,229,64,32,148,229,20,48,157,229,3,48,130,224,64,48,132,229,68,32,148,229,24,48,157,229,3,48,130,224,68,48,132,229,72,32,148,229,28,48,157,229,3,48,130,224,72,48,132,229,76,32,148,229,32,48,157,229,3,48,130,224,76,48,132,229,80,32,148,229,36,48,157,229,3,48,130,224,80,48,132,229,84,32,148,229,40,48,157,229,3,48,130,224,84,48,132,229,88,32,148,229,44,48,157,229,3,48,130,224,88,48,132,229,92,32,148,229,48,48,157,229,3,48,130,224,92,48,132,229,96,32,148,229,52,48,157,229,3,48,130,224,96,48,132,229,100,32,148,229,56,48,157,229,3,48,130,224,100,48,132,229,104,32,148,229,60,48,157,229,3,48,130,224,104,48,132,229,108,32,148,229,64,48,157,229,3,48,130,224,108,48,132,229,112,32,148,229,68,48,157,229,3,48,130,224,72,32,157,229,112,48,132,229,127,0,18,227,7,0,0,26,66,52,160,225,255,48,19,226,4,0,0,10,72,33,159,229,4,0,160,225,4,16,160,227,221,254,255,235,72,32,157,229,127,48,18,226,1,48,160,19,127,0,2,226,127,0,80,227,0,48,160,3,1,48,3,18,0,0,83,227,14,0,0,10,15,0,80,227,9,0,80,19,5,0,0,26,93,58,0,235,8,33,159,229,5,16,160,227,0,48,160,225,4,0,160,225,4,0,0,234,87,58,0,235,244,32,159,229,4,16,160,227,0,48,160,225,4,0,160,225,197,254,255,235,208,32,212,229,34,51,160,225,1,0,35,226,9,0,85,227,0,0,160,195,1,0,0,210,0,0,80,227,7,0,0,10,196,32,159,229,4,0,160,225,4,16,160,227,185,254,255,235,208,48,212,229,8,48,131,227,208,48,196,229,115,32,239,230,1,0,18,227,9,0,0,26,8,48,148,229,0,0,83,227,2,0,0,26,32,48,148,229,0,0,83,227,3,0,0,10,4,0,160,225,4,16,160,227,128,32,159,229,169,254,255,235,124,32,159,229,0,48,146,229,1,48,67,226,0,48,130,229,72,48,157,229};unsigned char actual[sizeof expected];if(!amc_peek_ram(view,m->ram,m->ram_base,m->ram_size,0x4e7c,actual,sizeof actual)||memcmp(actual,expected,sizeof actual)){fprintf(stderr,"Original launchd code mismatch\n");return 19;}}{static const unsigned char expected[]={240,64,45,233,12,112,141,226,0,5,45,233,157,221,77,226,52,208,77,226,0,128,160,225,124,48,144,229,0,0,83,227,222,1,0,10,196,7,159,229,196,23,159,229,196,39,159,229,196,55,159,229,189,0,0,234,176,7,159,229,176,23,159,229,184,39,159,229,184,55,159,229,184,0,0,234,8,0,160,225,7,16,160,227,172,39,159,229,206,242,255,235,224,1,0,234,208,32,216,229,8,48,2,226,115,64,239,230,0,0,84,227,21,0,0,10,8,48,194,227,208,48,200,229,8,0,160,225,4,16,160,227,128,39,159,229,10,48,160,227,193,242,255,235,10,48,160,227,0,48,141,229,8,0,160,225,6,16,224,227,17,32,160,227,9,48,67,226,4,128,141,229,14,235,255,235,1,0,112,227,202,1,0,26,48,7,159,229,48,23,159,229,72,39,159,229,72,55,159,229,152,0,0,234,8,0,160,225,7,16,160,227,60,39,159,229,174,242,255,235,209,48,216,229,64,0,19,227,7,0,0,26,8,48,152,229,0,0,83,227,2,0,0,26,32,48,152,229,0,0,83,227,0,0,0,10};unsigned char actual[sizeof expected];if(!amc_peek_ram(view,m->ram,m->ram_base,m->ram_size,0x80e8,actual,sizeof actual)||memcmp(actual,expected,sizeof actual)){fprintf(stderr,"Original launchd code mismatch\n");return 19;}}
   uint32_t root=0,pending[256],seen[256];unsigned todo=0,used=0,found=0;
   if(!read_word(m,view,0x14044,&root)||!root)error="launchd-root-job-unreadable";
   else pending[todo++]=root;
   fprintf(stderr,"LAUNCHD ttbr=%08x root=%08x\n",ttbr,root);
   while(todo&&!error){
    uint32_t job=pending[--todo],child=0,next=0,status=0,pid=0;char label[257]={0};
    if(used==256||!job||job>=0xc0000000||(job&3)){error="job-bound";break;}
    for(unsigned j=0;j<used;j++)if(seen[j]==job)error="job-cycle";
    if(error)break;seen[used++]=job;
    unsigned z;for(z=0;z<256;z++){if(!amc_peek_ram(view,m->ram,m->ram_base,m->ram_size,job+0xd8+z,label+z,1)){error="job-label-unreadable";break;}if(!label[z])break;if((unsigned char)label[z]<32||(unsigned char)label[z]>126){error="job-label-invalid";break;}}
    if(z==256)error="job-label-bound";
    if(error)break;
    if(!read_word(m,view,job+0x28,&child)||!read_word(m,view,job+4,&next)||!read_word(m,view,job+0xb4,&status)||!read_word(m,view,job+0xac,&pid)){error="job-fields-unreadable";break;}
    fprintf(stderr,"JOB %08x label=%s pid=%u status=%08x child=%08x next=%08x\n",job,label,pid,status,child,next);
    if(!strcmp(label,"com.iphone1.local-demo.http")){found++;fprintf(stderr,"TARGET pid=%u raw_status=%08x\n",pid,status);}
    if(todo+!!child+!!next>256){error="job-queue-bound";break;}if(next)pending[todo++]=next;if(child)pending[todo++]=child;
   }
   fprintf(stderr,"JOB_AUDIT count=%u found=%u complete=%u error=%s\n",used,found,!todo&&!error,error?error:"none");
   if(!error&&found!=1)error="target-job-not-unique";
  }
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
