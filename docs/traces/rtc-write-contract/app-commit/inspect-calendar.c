#include "board.h"
#include "snapshot.h"
#include "peek.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
int main(int argc,char **argv){
 for(int i=1;i<argc;i++){
  s5l8900_t*m=calloc(1,sizeof *m);if(!m||!s5l_board_init(&s5l_board_m68,m,256u<<20)||snapshot_load(m,argv[i])!=SNAP_OK)return 2;
  arm_cpu_t*copy=malloc(sizeof *copy);if(!copy)return 3;*copy=m->cpu;copy->cpsr=(copy->cpsr&~31u)|ARM_MODE_SVC;
  uint32_t clock[12],platform=0;uint64_t boot=0;
  if(!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,0xc01c85dc,clock,sizeof clock)||!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,0xc01c865c,&boot,8)||!amc_peek_ram(copy,m->ram,m->ram_base,m->ram_size,0xc01d52d0,&platform,4))return 4;
  printf("{\"input\":\"%s\",\"retired\":%"PRIu64",\"calendarMicroOffset\":%u,\"calendarSecondOffsetLow\":%u,\"calendarSecondOffsetHigh\":%u,\"bootTime\":%"PRIu64",\"platformExpert\":\"%08x\",\"rtcSeconds\":%"PRIu64",\"guestInstructionsExecuted\":0}\n",argv[i],m->cpu.cycles,clock[1],clock[2],clock[3],boot,platform,m->pmu.seconds);
  free(copy);s5l8900_free(m);free(m);
 }
 return 0;
}
