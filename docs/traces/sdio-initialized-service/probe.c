#define _POSIX_C_SOURCE 200809L
#include "checkpoint.h"
#include "runtime.h"
#include "gates.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
static volatile sig_atomic_t timeout_flag;
static FILE*trace_file,*milestones;
static bool trace_ok=true;
static uint64_t transactions,status_reads,good_status,request_reads,marker_low,marker_high,read_errors;
static bool main_boundary,main_return,verify_entered;
static gate_pair_t marker_pair;
static uint64_t start_cycles,start_ticks;
static void timeout_handler(int sig){(void)sig;timeout_flag=1;}
static double monotonic(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return t.tv_sec+t.tv_nsec/1e9;}
static uint32_t le(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void observe(checkpoint_phone_t*p,bool write,uint32_t address,uint32_t value,unsigned width,bool accepted){
 uint8_t bytes[32];uint64_t fields[]={p->machine->cpu.cycles,p->machine->timer.ticks,p->machine->cpu.r[15],address,value,(write?1u:0u)|(width<<1)|(accepted?256u:0u)};
 unsigned sizes[]={8,8,4,4,4,4},n=0;for(unsigned i=0;i<6;i++)for(unsigned j=0;j<sizes[i];j++)bytes[n++]=(uint8_t)(fields[i]>>(j*8));
 if(fwrite(bytes,1,32,trace_file)!=32)trace_ok=false;transactions++;
 if(write||address!=SDIO_BASE+SDIO_RESP0)return;
 sdio_t*s=&p->device;if((s->command&63u)!=52u||(s->argument&0x80000000u)||((s->argument>>28)&7u)!=1u){if(verify_entered)gate_pair_begin(&marker_pair);return;}
 unsigned reg=(s->argument>>9)&0x1ffffu;bool successful=accepted&&(value&0x0b00u)==0&&(value&0x1000u);
 if(verify_entered)gate_pair_read(&marker_pair,reg,value,successful,s->completions);
 if(!successful)read_errors++;
 if(reg==0x20){status_reads++;if(successful&&(value&9)==9)good_status++;}
 else if(reg==0x10||reg==0x11){if(successful&&(value&255)==0)request_reads++;}
 else if(reg==0x34){if(successful&&(value&255)==0xdc)marker_low++;}
 else if(reg==0x35){if(successful&&(value&255)==0xfe)marker_high++;}
 if(reg==0x10||reg==0x11||reg==0x34||reg==0x35||reg==6||(reg==0x20&&(status_reads<4||status_reads>=999)))
  fprintf(milestones,"completed R5 read cycles=%"PRIu64" ticks=%"PRIu64" pc=%08x reg=%02x value=%04x success=%u\n",p->machine->cpu.cycles,p->machine->timer.ticks,p->machine->cpu.r[15],reg,value,successful);
}
static void witness(checkpoint_phone_t*p){arm_cpu_t*c=&p->machine->cpu;if(!gate_arm_svc(c->cpsr))return;
 if(c->r[15]==0xc03801c8u)gate_pair_begin(&marker_pair);
 if(c->r[15]==0xc038a968u){main_boundary=c->r[11]==122608&&c->r[10]==0;fprintf(milestones,"original loader success boundary pc=c038a968 sourceCursor=%u nextRequested=%u exact=%u\n",c->r[11],c->r[10],main_boundary);}
 if(c->r[15]==0xc038aaa4u){main_return=c->r[0]==1;fprintf(milestones,"original loader returned pc=c038aaa4 r0=%u\n",c->r[0]);}
 if(c->r[15]==0xc03801b4u){verify_entered=true;gate_pair_begin(&marker_pair);fprintf(milestones,"original verification entered pc=c03801b4\n");}
}
static bool dump(const char*dir,const char*name,const void*p,size_t n){char path[512];int z=snprintf(path,sizeof path,"%s/%s",dir,name);if(z<0||(size_t)z>=sizeof path)return false;FILE*f=fopen(path,"wbx");if(!f)return false;bool ok=fwrite(p,1,n,f)==n;if(fclose(f))ok=false;return ok;}
int main(int argc,char**argv){
 /* A Python adapter must verify frozen manifest/hash/identity before streaming
  * immutable input bytes. This C loader independently canonical-validates them. */
 if(argc!=3)return 2;const char*mode=argv[1];bool control=!strcmp(mode,"control"),positive=!strcmp(mode,"positive"),no_marker=!strcmp(mode,"no-marker");if(!control&&!positive&&!no_marker)return 2;
 signal(SIGALRM,timeout_handler);alarm(120);double load_start=monotonic();
 uint8_t header[8];if(fread(header,1,8,stdin)!=8)return 3;size_t mn=le(header),sn=le(header+4);if(!mn||mn>300u*1024*1024||!sn||sn>300000)return 3;
 uint8_t*m=malloc(mn),*side=malloc(sn);if(!m||!side){free(m);free(side);return 4;}if(fread(m,1,mn,stdin)!=mn||fread(side,1,sn,stdin)!=sn||fgetc(stdin)!=EOF)return 5;
 checkpoint_phone_t*p=checkpoint_pair_load(m,mn,side,sn);free(m);free(side);double load_seconds=monotonic()-load_start;alarm(0);if(!p||timeout_flag||load_seconds<0||load_seconds>120)return 6;
 if(!gate_arm_svc(p->machine->cpu.cpsr)||p->machine->cpu.r[15]!=0xc0379684u||p->machine->cpu.cycles!=17517070017ull||p->machine->timer.ticks!=256764208ull)return 7;
 start_cycles=p->machine->cpu.cycles;start_ticks=p->machine->timer.ticks;
 runtime_t runtime={0};sdio_t*before=malloc(sizeof *before);if(!before)return 8;*before=p->device;
 bool admitted=control||runtime_prepare(&runtime,&p->device,start_ticks,NULL,NULL,NULL,positive);
 bool admission_unchanged=!memcmp(before,&p->device,sizeof *before);free(before);if(!admitted||!admission_unchanged)return 8;
 char name[512];snprintf(name,sizeof name,"%s/transactions.bin",argv[2]);trace_file=fopen(name,"wbx");snprintf(name,sizeof name,"%s/milestones.log",argv[2]);milestones=fopen(name,"wbx");if(!trace_file||!milestones)return 9;
 fprintf(milestones,"normal fresh073 start pc=c0379684 cycles=%"PRIu64" ticks=%"PRIu64" mode=%s runtimeAdmitted=%u; ARM SVC required; semantic model only, no firmware CPU\n",start_cycles,start_ticks,mode,!control);
 p->observe=observe;uint64_t instructions=0;bool normal=true,reached=false;const uint64_t cap=100000000;
 timeout_flag=0;alarm(90);double run_start=monotonic();
 while(instructions<cap&&!timeout_flag&&trace_ok&&transactions<=8388608u){
  arm_cpu_t*c=&p->machine->cpu;witness(p);
  if(control&&gate_arm_svc(c->cpsr)&&c->r[15]==0xc038a8ecu){reached=c->r[4]==1000;break;}
  if(positive&&gate_arm_svc(c->cpsr)&&c->r[15]==0xc0380208u){reached=main_boundary&&main_return&&verify_entered&&c->r[2]==0xfedc&&c->r[3]==0xfedc&&good_status&&request_reads==2&&marker_low&&marker_high&&marker_pair.complete&&!read_errors;break;}
  if(no_marker&&gate_arm_svc(c->cpsr)&&c->r[15]==0xc0380224u&&c->r[0]==0){reached=main_boundary&&main_return&&verify_entered&&good_status&&request_reads==2&&!marker_low&&!marker_high&&!read_errors;break;}
  if(gate_arm_svc(c->cpsr)&&(c->r[15]==0xc038ab14u||(!control&&c->r[15]==0xc038a8ecu))){normal=false;break;}
  arm_status_t status=ARM_OK;unsigned count=s5l8900_run(p->machine,1,&status);checkpoint_sync(p);instructions++;
  if(count!=1||status!=ARM_OK){normal=false;break;}
 }
 alarm(0);double run_seconds=monotonic()-run_start;
 bool image=runtime_image_supported(&p->device);
 bool passed=reached&&normal&&trace_ok&&!timeout_flag&&run_seconds>=0&&run_seconds<=90&&instructions<=cap&&transactions<=8388608u&&image&&p->device.data_completions==504&&p->device.main_model.phase==MAIN_HANDOFF;
 if(control)passed=passed&&instructions==41947204&&transactions==4581748&&status_reads==1000&&!good_status&&!request_reads&&!marker_low&&!marker_high;
 fprintf(milestones,"normal outside-run stop pc=%08x r0=%u r4=%u cycles=%"PRIu64" ticks=%"PRIu64" instructions=%"PRIu64" passed=%u no register6 completion or firmwareCPU claim\n",p->machine->cpu.r[15],p->machine->cpu.r[0],p->machine->cpu.r[4],p->machine->cpu.cycles,p->machine->timer.ticks,instructions,passed);
 if(fclose(trace_file))passed=false;if(fclose(milestones))passed=false;
 sdio_main_t*image_state=&p->device.main_model;
 if(!dump(argv[2],"low.bin",image_state->low,sizeof image_state->low)||!dump(argv[2],"high.bin",image_state->high,sizeof image_state->high)||
    !dump(argv[2],"low-mask.bin",image_state->low_coverage,sizeof image_state->low_coverage)||!dump(argv[2],"high-mask.bin",image_state->high_coverage,sizeof image_state->high_coverage)||
    !dump(argv[2],"handoff.bin",image_state->pending,32)||!dump(argv[2],"helper.bin",p->device.helper.memory,p->device.helper.used))passed=false;
 snprintf(name,sizeof name,"%s/report.json",argv[2]);FILE*f=fopen(name,"wbx");if(!f)return 10;
 fprintf(f,"{\"passed\":%s,\"mode\":\"%s\",\"normalOutsideRunStop\":%s,\"pc\":\"%08x\",\"r0\":%u,\"r4\":%u,\"instructions\":%"PRIu64",\"instructionLimit\":%"PRIu64",\"totalInstructions\":%"PRIu64",\"ticks\":%"PRIu64",\"loadSeconds\":%.6f,\"loadLimitSeconds\":120,\"runSeconds\":%.6f,\"runLimitSeconds\":90,\"timeout\":%s,\"transactions\":%"PRIu64",\"statusReads\":%"PRIu64",\"goodStatusReads\":%"PRIu64",\"zeroLengthReads\":%"PRIu64",\"fedcLowReads\":%"PRIu64",\"fedcHighReads\":%"PRIu64",\"readErrors\":%"PRIu64",\"mainBoundary\":%s,\"mainReturn\":%s,\"verifyEntered\":%s,\"imageUnchanged\":%s,\"admissionDeviceUnchanged\":%s,\"dataCompletions\":%"PRIu64",\"runtimePhase\":%u,\"semanticDelayTicks\":600,\"scope\":\"private initialized storage and original verification only; no guest runtimeCMD53, register6, firmwareCPU or networkinterface\"}\n",passed?"true":"false",mode,normal?"true":"false",p->machine->cpu.r[15],p->machine->cpu.r[0],p->machine->cpu.r[4],instructions,cap,p->machine->cpu.cycles,p->machine->timer.ticks,load_seconds,run_seconds,timeout_flag?"true":"false",transactions,status_reads,good_status,request_reads,marker_low,marker_high,read_errors,main_boundary?"true":"false",main_return?"true":"false",verify_entered?"true":"false",image?"true":"false",admission_unchanged?"true":"false",p->device.data_completions,runtime.phase);
 if(fclose(f))passed=false;
 snprintf(name,sizeof name,"%s/gate-mode-pair.json",argv[2]);f=fopen(name,"wbx");if(!f)passed=false;else{fprintf(f,"{\"armSVC\":%s,\"cpsr\":\"%08x\",\"orderedFEDCPair\":%s,\"lowCompletion\":%"PRIu64"}\n",gate_arm_svc(p->machine->cpu.cpsr)?"true":"false",p->machine->cpu.cpsr,marker_pair.complete?"true":"false",marker_pair.low_completion);if(fclose(f))passed=false;}
 runtime_destroy(&runtime);checkpoint_phone_free(p);free(p);return passed?0:1;
}
