#include "board.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
int main(int argc,char **argv) {
    for(int i=1;i<argc;i++) {
        FILE *f=fopen(argv[i],"rb");if(!f)return 2;
        if(fseek(f,0,SEEK_END))return 3;
        long n=ftell(f);if(n<=0||n>300*1024*1024)return 4;rewind(f);
        unsigned char *b=malloc((size_t)n);if(!b)return 5;
        if(fread(b,1,(size_t)n,f)!=(size_t)n||fclose(f))return 6;
        s5l8900_t *m=calloc(1,sizeof *m);if(!m)return 7;
        if(!s5l_board_init(&s5l_board_m68,m,256u*1024*1024))return 8;
        snapshot_status_t status=snapshot_load_mem(m,b,(size_t)n);free(b);
        if(status!=SNAP_OK){fprintf(stderr,"load status%d\n",status);return 9;}
        printf("{\"input\":\"%s\",\"retired\":%"PRIu64",\"ticks\":%"PRIu64",\"rtcSeconds\":%"PRIu64",\"rtcFraction\":%"PRIu64",\"rtcTickHz\":%u,\"registerWrites\":%"PRIu64",\"unknownWrites\":%"PRIu64"}\n",argv[i],m->cpu.cycles,m->timer.ticks,m->pmu.seconds,m->pmu.tick_accum,m->pmu.tick_hz,m->pmu.reg_writes,m->pmu.unknown_writes);
        s5l8900_free(m);free(m);
    }
    return 0;
}
