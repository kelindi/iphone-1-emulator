#define _POSIX_C_SOURCE 200809L
#include "adapter.h"
#include "ui.h"
#include "ui-data.h"
#include "board.h"
#include "snapshot.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
static exit_adapter*a;static FILE*serial;static char line[512];static unsigned line_n;static bool serial_bad;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(double)t.tv_sec+t.tv_nsec/1e9;}
static void uart(void*ctx,uint8_t c){(void)ctx;if(fputc(c,serial)==EOF)serial_bad=true;if(c=='\n'){line[line_n]=0;if(strstr(line,"Graphics hang with pending 2D blit")&&!a->stop){a->stop=true;snprintf(a->reason,sizeof a->reason,"original-pending-2D-graphics-hang");}line_n=0;}else if(c!='\r'){if(line_n+1<sizeof line)line[line_n++]=(char)c;else{line_n=0;serial_bad=true;}}}
static bool frame(s5l8900_t*m,uint8_t*p){uint32_t w,h,k=s5l_clcd_active_window(&m->clcd);return k!=CLCD_WIN_NONE&&s5l_clcd_scanout(&m->clcd,k,m->ram,m->ram_base,m->ram_size,p,320u*480u*3u,&w,&h)&&w==320&&h==480;}
static bool ppm(const char*dir,const char*name,const uint8_t*p){char path[1024];if(snprintf(path,sizeof path,"%s/%s.ppm",dir,name)>=(int)sizeof path)return false;FILE*f=fopen(path,"wb");if(!f)return false;bool ok=fprintf(f,"P6\n320 480\n255\n")>0&&fwrite(p,1,460800,f)==460800;return fclose(f)==0&&ok;}
static bool snapshot_input(s5l8900_t*m,const char*path){gzFile f=gzopen(path,"rb");if(!f)return false;size_t n=0,cap=1u<<20;uint8_t*p=malloc(cap);if(!p){gzclose(f);return false;}int got;
 for(;;){if(cap-n<1u<<20){if(cap>=512u*1024u*1024u){free(p);gzclose(f);return false;}cap*=2;void*q=realloc(p,cap);if(!q){free(p);gzclose(f);return false;}p=q;}got=gzread(f,p+n,1u<<20);if(got<=0)break;n+=(unsigned)got;}
 int closed=gzclose(f);bool ok=got==0&&closed==Z_OK&&n==245071842&&snapshot_load_mem(m,p,n)==SNAP_OK;free(p);return ok;
}
static bool save(s5l8900_t*m,const char*dir){char path[1024];if(snprintf(path,sizeof path,"%s/boundary.snap.gz.partial",dir)>=(int)sizeof path)return false;uint8_t*p=NULL;size_t n=0;alarm(120);snapshot_status_t s=snapshot_save_mem(m,&p,&n);if(s!=SNAP_OK){alarm(0);return false;}gzFile g=gzopen(path,"wb1");bool ok=g!=NULL;for(size_t off=0;ok&&off<n;){unsigned chunk=n-off>1048576?1048576:(unsigned)(n-off);ok=gzwrite(g,p+off,chunk)==(int)chunk;off+=chunk;}if(g&&gzclose(g)!=Z_OK)ok=false;free(p);alarm(0);return ok;}
int main(int argc,char**argv){bool preflight=argc==5&&!strcmp(argv[4],"--preflight");if(argc!=4&&!preflight){fprintf(stderr,"usage: runner EXACT_LISTENER_GZIP NEW_OUTPUT EXISTING_REFERENCE_DIR\n");return 2;}if(mkdir(argv[2],0700)){perror("output must be fresh");return 2;}double start=now();char path[1024];s5l8900_t*m=calloc(1,sizeof*m);exit_adapter ad;ui_state ui={0};a=&ad;
 if(!m||!s5l_board_init(s5l_board_find("m68"),m,256u<<20)||!snapshot_input(m,argv[1])||m->cpu.cycles!=9197000000ull||!m->mtz1.present||m->ram_size!=(256u<<20))return 3;
 snprintf(path,sizeof path,"%s/events.jsonl",argv[2]);FILE*events=fopen(path,"wb");snprintf(path,sizeof path,"%s/serial.log",argv[2]);serial=fopen(path,"wb");if(!events||!serial)return 4;
 uint8_t*refs[8]={0},*pixels=malloc(460800);if(!pixels)return 5;for(unsigned i=0;i<8;i++){snprintf(path,sizeof path,"%s/%s.rgb",argv[3],references[i].name);FILE*f=fopen(path,"rb");refs[i]=malloc(460800);if(!f||!refs[i]||fread(refs[i],1,460800,f)!=460800||fgetc(f)!=EOF)return 6;fclose(f);}
 if(!exit_adapter_init(&ad,m,events)||!s5l8900_set_uart0_host(m,uart,NULL))return 7;ui.stage_ticks=m->timer.ticks;uint64_t initial=m->cpu.cycles,steps=0,scan=0;arm_status_t status=ARM_OK;bool scan_ok=frame(m,pixels);if(scan_ok)ppm(argv[2],"initial",pixels);
 if(preflight){unsigned error=scan_ok?ui_error(pixels,refs[0],&references[0]):UINT32_MAX;snprintf(path,sizeof path,"%s/preflight.json",argv[2]);FILE*f=fopen(path,"wb");if(f){fprintf(f,"{\"scope\":\"Original snapshot load/source bindings/scanout only, ZERO guest instructions\",\"cyclesBefore\":%llu,\"cyclesAfter\":%llu,\"sourceBindings\":true,\"initialFrameMAEMilli\":%u,\"initialFrameMatches\":%s}\n",(unsigned long long)initial,(unsigned long long)m->cpu.cycles,error,error<=2000?"true":"false");fclose(f);}fclose(events);fclose(serial);for(unsigned i=0;i<8;i++)free(refs[i]);free(pixels);s5l8900_free(m);free(m);return error<=2000?0:9;}
 const char*reason=NULL;while(!reason){
  if(ad.stop){reason=ad.reason;break;}if(serial_bad){reason="serial-output-or-line-cap";break;}if(ui.failed){reason=ui.reason;break;}if(steps>=8000000000ull){reason="8B-instruction-cap";break;}if(now()-start>=450){reason="450-second-wall-cap";break;}
  if(steps>=scan){struct statvfs disk;if(statvfs(argv[2],&disk)||disk.f_bavail*(uint64_t)disk.f_frsize<4300000000ull){reason="disk-floor-4.3GB";break;}scan=steps+2000000;scan_ok=frame(m,pixels);if((ui.phase==0||ui.phase==3)&&ui.stage<8){unsigned id=ui.phase==0?stages[ui.stage].before:stages[ui.stage].after;unsigned error=scan_ok?ui_error(pixels,refs[id],&references[id]):UINT32_MAX;ui_observe(&ui,error<=2000,m->timer.ticks);fprintf(events,"{\"kind\":\"scanout\",\"steps\":%llu,\"ticks\":%llu,\"stage\":%u,\"phase\":%u,\"reference\":\"%s\",\"meanAbsoluteErrorMilli\":%u,\"stable\":%u}\n",(unsigned long long)steps,(unsigned long long)m->timer.ticks,ui.stage,ui.phase,references[id].name,error,ui.stable);}}
  unsigned stage_before=ui.stage;tap point={0};int action=ui_step(&ui,steps,m->timer.ticks,ad.owner_executed,&point);if(action){s5l_mt_contact_t c={1,MTZ2_PHASE_MAKE_TOUCH,point.x,point.y,255,8,8};bool ok=action==1?s5l8900_set_touch(m,&c,1):s5l8900_set_touch(m,NULL,0);fprintf(events,"{\"kind\":\"touch\",\"steps\":%llu,\"ticks\":%llu,\"down\":%s,\"x\":%u,\"y\":%u,\"accepted\":%s}\n",(unsigned long long)steps,(unsigned long long)m->timer.ticks,action==1?"true":"false",action==1?point.x:0,action==1?point.y:0,ok?"true":"false");if(!ok){reason="touch-device-refusal";break;}}
  if(stage_before!=ui.stage&&scan_ok){char name[80];snprintf(name,sizeof name,"stage-%02u-%s",stage_before,stages[stage_before].name);if(!ppm(argv[2],name,pixels)){reason="frame-write-failure";break;}}
  unsigned n=s5l8900_run(m,1024,&status);steps+=n;exit_adapter_sample(&ad);if(status!=ARM_OK||!n){reason="core-status-or-zero-progress";break;}
 }
 /* Every stop comes back through ordinary s5l8900_run; hooks always false.
  * The final slice can retire at most1024 extra instructions after a witness.
  * Preserve active contacts as captured, do not add a cleanup guest event. */
 uint64_t end=m->cpu.cycles;bool final_frame=frame(m,pixels)&&ppm(argv[2],"boundary",pixels);double run_seconds=now()-start;bool saved=save(m,argv[2]);snprintf(path,sizeof path,"%s/report.json",argv[2]);FILE*r=fopen(path,"wb");if(r){fprintf(r,"{\"reason\":\"%s\",\"guestGatePassed\":false,\"normalOutsideRunStop\":true,\"initialCycles\":%llu,\"finalCycles\":%llu,\"additionalInstructions\":%llu,\"stage\":%u,\"phase\":%u,\"ownerExecuted\":%s,\"adapterPhase\":%u,\"runSeconds\":%.6f,\"finalFrame\":%s,\"snapshotPartialComplete\":%s,\"maximumPostWitnessSlice\":1024}\n",reason,(unsigned long long)initial,(unsigned long long)end,(unsigned long long)steps,ui.stage,ui.phase,ad.owner_executed?"true":"false",ad.phase,run_seconds,final_frame?"true":"false",saved?"true":"false");fclose(r);}fclose(events);fclose(serial);for(unsigned i=0;i<8;i++)free(refs[i]);free(pixels);s5l8900_free(m);free(m);return saved&&final_frame?0:8;
}
