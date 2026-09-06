#include "board.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 s5l8900_t *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);CHECK(a&&b);
 CHECK(s5l_board_init(s5l_board_find("m68"),a,1u<<20));
 CHECK(s5l_board_init(s5l_board_find("m68"),b,1u<<20));
 const uint8_t date[]={0x25,0x13,0,5,1,1,0x10};
 unsigned cases=0;
 for(unsigned count=1;count<=7;count++){
  s5l_pcf50635_reset(&a->pmu,a->tb_hz);
  s5l_i2c_slave_t *wire=&a->i2c[0].slaves[0];
  CHECK(wire->ctx==&a->pmu&&wire->addr==PCF50635_I2C_ADDR);
  CHECK(wire->start(wire->ctx,false));CHECK(wire->write(wire->ctx,0x59));
  for(unsigned i=0;i<count;i++)CHECK(wire->write(wire->ctx,date[i]));
  wire->stop(wire->ctx);
  s5l_pcf50635_tick(&a->pmu,a->tb_hz-1);
  CHECK(a->pmu.seconds==PCF50635_DEFAULT_TIME);
  uint8_t *saved=NULL,*resaved=NULL;size_t length=0,resaved_length=0;
  CHECK(snapshot_save_mem(a,&saved,&length)==SNAP_OK);
  CHECK(snapshot_load_mem(b,saved,length)==SNAP_OK);
  CHECK(!memcmp(&a->pmu,&b->pmu,sizeof a->pmu));
  CHECK(b->i2c[0].slaves[0].ctx==&b->pmu&&b->i2c[0].slaves[0].write==wire->write);
  CHECK(snapshot_save_mem(b,&resaved,&resaved_length)==SNAP_OK);
  CHECK(length==resaved_length&&!memcmp(saved,resaved,length));
  free(saved);free(resaved);
  s5l_pcf50635_tick(&a->pmu,1);s5l_pcf50635_tick(&b->pmu,1);
  CHECK(!memcmp(&a->pmu,&b->pmu,sizeof a->pmu));
  CHECK(b->pmu.seconds==(count==7?1262304805ull:PCF50635_DEFAULT_TIME+1));
  CHECK(b->pmu.unknown_writes==(count==7?0:count));
  for(unsigned i=0;i<7;i++)CHECK(!b->pmu.written[0x59+i]);
  cases++;
 }
 s5l8900_free(a);s5l8900_free(b);free(a);free(b);
 printf("PASS %u pending full/partial snapshot39 exact-byte roundtrips and subsequent pulse\n",cases);
 return 0;
}
