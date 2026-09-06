#include "board.h"
#include "snapshot.h"
#include "peek.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
int main(int argc,char **argv){
 for(int i=1;i<argc;i++){
  s5l8900_t*m=calloc(1,sizeof *m);if(!m||!s5l_board_init(&s5l_board_m68,m,256u<<20)||snapshot_load(m,argv[i])!=SNAP_OK)return 2;
  arm_cpu_t*copy=malloc(sizeof *copy);if(!copy)return 3;*copy=m->cpu;copy->cpsr=(copy->cpsr&~31u)|ARM_MODE_SVC;
  uint32_t clock[12],platform=0;uint64_t boot=0;
  if(!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,0xc01c85dc,clock,sizeof clock)||!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,0xc01c865c,&boot,8)||!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,0xc01d52d0,&platform,4))return 4;
  printf("{\"input\":\"%s\",\"retired\":%"PRIu64",\"calendarMicroOffset\":%u,\"calendarSecondOffsetLow\":%u,\"calendarSecondOffsetHigh\":%u,\"bootTime\":%"PRIu64",\"platformExpert\":\"%08x\",\"rtcSeconds\":%"PRIu64",\"guestInstructionsExecuted\":0}\n",argv[i],m->cpu.cycles,clock[1],clock[2],clock[3],boot,platform,m->pmu.seconds);
  uint32_t vtable=0;unsigned char rtc[24],event[80];
  if(!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,0xcbb6f800,&vtable,4)||vtable!=0xc0402348||!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,0xcbb6f888,rtc,sizeof rtc))return 5;
  uint32_t source;memcpy(&source,rtc+8,4);
  if(!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,source,event,sizeof event))return 6;
  printf("RTC_OBJECT input%s vtable%08x fields88=",argv[i],vtable);for(unsigned j=0;j<sizeof rtc;j++)printf("%02x",rtc[j]);printf(" secondSource%08x bytes=",source);for(unsigned j=0;j<sizeof event;j++)printf("%02x",event[j]);printf(" INT1%02x MASK1%02x\n",m->pmu.regs[PCF50635_INT1],m->pmu.regs[PCF50635_INT1MASK]);
  free(copy);s5l8900_free(m);free(m);
 }
 return 0;
}
