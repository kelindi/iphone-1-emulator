#include "board.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
int main(int argc,char **argv){
 for(int i=1;i<argc;i++){
  s5l8900_t *m=calloc(1,sizeof *m);if(!m||!s5l_board_init(&s5l_board_m68,m,256u<<20)||snapshot_load(m,argv[i])!=SNAP_OK)return 2;
  printf("{\"input\":\"%s\",\"retired\":%"PRIu64",\"ticks\":%"PRIu64",\"rtcSeconds\":%"PRIu64",\"rtcFraction\":%"PRIu64",\"rtcTickHz\":%u,\"registerWrites\":%"PRIu64",\"unknownWrites\":%"PRIu64",\"unknownReads\":%"PRIu64",\"pending\":[",argv[i],m->cpu.cycles,m->timer.ticks,m->pmu.seconds,m->pmu.tick_accum,m->pmu.tick_hz,m->pmu.reg_writes,m->pmu.unknown_writes,m->pmu.unknown_reads);
  for(unsigned j=0;j<7;j++)printf("%s%u",j?",":"",m->pmu.written[0x59+j]);printf("],\"dateRegisters\":[");for(unsigned j=0;j<7;j++)printf("%s%u",j?",":"",m->pmu.regs[0x59+j]);printf("],\"guestInstructionsExecuted\":0}\n");
  s5l8900_free(m);free(m);
 }return 0;
}
