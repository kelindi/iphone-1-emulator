#define _POSIX_C_SOURCE 200809L
#include <time.h>
static double wall_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
#define IPHONE1_TEST_EMSCRIPTEN_H
#define EMSCRIPTEN_KEEPALIVE
static double emscripten_get_now(void){return wall_ms();}
#include "../../web/emu_api.c"
#include <math.h>
static unsigned char pixels[320*480*4],home[320*480*4],before[320*480*4],previous[320*480*4];
static unsigned width,height;static double began;static unsigned long long retired;
static void checked(int x){if(x<0){fprintf(stderr,"API error %d %s\n",x,error_text);exit(1);}}
static unsigned char*load(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f){perror(p);exit(1);}fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);unsigned char*b=malloc(*n);if(!b||fread(b,1,*n,f)!=*n)exit(1);fclose(f);return b;}
static void advance(void){double delay=emu_clock_delay();if(delay>0){struct timespec ts={0,(long)(fmin(delay,2)*1e6)};nanosleep(&ts,NULL);return;}unsigned n=emu_run(50000);retired+=n;if(emu_status()!=0){fprintf(stderr,"Gueststatus %d\n",emu_status());exit(1);}if(wall_ms()-began>240000){fprintf(stderr,"240s cap\n");exit(1);}}
static void wait_ms(double ms){double end=wall_ms()+ms;while(wall_ms()<end)advance();}
static void frame(void){if(emu_frame(pixels,&width,&height)!=1||width!=320||height!=480){fprintf(stderr,"frame unavailable\n");exit(1);}}
static double diff(unsigned char*a,unsigned char*b){unsigned n=0,total=0;for(int y=30;y<480;y+=4)for(int x=2;x<320;x+=4){int i=(y*320+x)*4;total++;if(abs(a[i]-b[i])>25||abs(a[i+1]-b[i+1])>25||abs(a[i+2]-b[i+2])>25)n++;}return(double)n/total;}
static double white(void){unsigned n=0,total=0;for(int y=30;y<480;y+=4)for(int x=2;x<320;x+=4){int i=(y*320+x)*4;total++;if(pixels[i]>235&&pixels[i+1]>235&&pixels[i+2]>235)n++;}return(double)n/total;}
static void shot(const char*dir,int round,int kind){char p[1024];snprintf(p,sizeof p,"%s/%d-%s.rgba",dir,round,kind?"home":"settings");FILE*f=fopen(p,"wb");if(!f||fwrite(pixels,1,sizeof pixels,f)!=sizeof pixels)exit(1);fclose(f);}
int main(int argc,char**argv){if(argc!=6)return 2;size_t kn,dn,sn;unsigned char*k=load(argv[1],&kn),*d=load(argv[2],&dn),*s=load(argv[3],&sn);checked(emu_init(k,kn,d,dn,NULL,0,256));checked(emu_load_snapshot(s,sn));free(k);free(d);free(s);checked(emu_host_clock(1));began=wall_ms();wait_ms(1000);frame();memcpy(home,pixels,sizeof home);int rounds=atoi(argv[5]);
 for(int round=0;round<rounds;round++)for(int kind=0;kind<2;kind++){
 frame();memcpy(before,pixels,sizeof before);memcpy(previous,pixels,sizeof previous);double start=wall_ms(),stable=start,ready=-1,first=-1,next=start+50;unsigned long long initial=retired;
 if(kind)checked(emu_button(1,1));else checked(emu_touch(1,275,250));wait_ms(30);if(kind)checked(emu_button(1,0));else checked(emu_touch(0,275,250));double release=wall_ms();
 for(;;){advance();double now=wall_ms();if(now-start>15000){fprintf(stderr,"readiness timeout round%d kind%d white%f diff%f\n",round,kind,white(),diff(home,pixels));shot(argv[4],round,kind);return 1;}if(now<next)continue;next=now+50;frame();double changed=diff(before,pixels);if(first<0&&changed>.015)first=now;int isReady=kind?diff(home,pixels)<.04:white()>.25;
 if(diff(previous,pixels)>.008||!isReady||changed<.1){stable=now;ready=-1;}else if(ready<0)ready=stable;memcpy(previous,pixels,sizeof previous);if(ready>=0&&now-ready>=500){printf("{\"round\":%d,\"kind\":\"%s\",\"releaseToReadyMs\":%.3f,\"releaseToFirstVisibleMs\":%.3f,\"observedMs\":%.3f,\"instructions\":%llu}\n",round,kind?"home":"settings",ready-release,first-release,now-start,retired-initial);fflush(stdout);shot(argv[4],round,kind);break;}}
 wait_ms(1000);
 }
 checked(emu_host_clock(0));return 0;
}
