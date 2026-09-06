#include "soc.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}} while(0)
static bool put(s5l_pcf50635_t*p,unsigned first,const uint8_t*bytes,unsigned n){
 s5l_i2c_slave_t s;s5l_pcf50635_bind(p,&s);
 if(!s.start(s.ctx,false)||!s.write(s.ctx,first))return false;
 for(unsigned i=0;i<n;i++)if(!s.write(s.ctx,bytes[i]))return false;
 s.stop(s.ctx);return true;
}
static uint8_t get(s5l_pcf50635_t*p,unsigned reg){
 s5l_i2c_slave_t s;s5l_pcf50635_bind(p,&s);s.start(s.ctx,false);s.write(s.ctx,reg);
 s.start(s.ctx,true);uint8_t v=s.read(s.ctx);s.stop(s.ctx);return v;
}
static bool clean(const s5l_pcf50635_t*p){for(unsigned i=0;i<7;i++)if(p->written[0x59+i])return false;return true;}
int main(void){
 s5l_pcf50635_t p,q;
 const uint8_t actual[]={0x25,0x13,0x00,0x05,0x01,0x01,0x10};
 const uint64_t desired=1262304805ull; /* Independent UTC conversion of079. */
 s5l_pcf50635_reset(&p,10);s5l_pcf50635_tick(&p,3);
 for(unsigned i=0;i<7;i++){
  CHECK(put(&p,0x59+i,actual+i,1));CHECK(p.seconds==PCF50635_DEFAULT_TIME);
  CHECK(p.tick_accum==3);CHECK(p.unknown_writes==0);
 }
 CHECK(!clean(&p));CHECK(get(&p,0x59)==0);CHECK(get(&p,0x5a)==0);
 s5l_pcf50635_tick(&p,6);CHECK(p.seconds==PCF50635_DEFAULT_TIME);CHECK(p.tick_accum==9);
 s5l_pcf50635_tick(&p,1);CHECK(p.seconds==desired);CHECK(p.tick_accum==0);CHECK(clean(&p));
 for(unsigned i=0;i<7;i++)CHECK(get(&p,0x59+i)==actual[i]);
 CHECK(p.unknown_writes==0);CHECK(get(&p,PCF50635_INT1)==0x80);
 s5l_pcf50635_tick(&p,15);CHECK(p.seconds==desired+1);CHECK(p.tick_accum==5);

 /* Same requested value still produces the real oscillator event. */
 s5l_pcf50635_reset(&p,10);s5l_pcf50635_set_time(&p,desired);
 CHECK(put(&p,0x59,actual,7));s5l_pcf50635_tick(&p,10);
 CHECK(p.seconds==desired);CHECK(get(&p,PCF50635_INT1)==0x80);
 /* Lumped and split edges agree, including a backward date change. */
 s5l_pcf50635_reset(&p,10);s5l_pcf50635_set_time(&p,desired+500);
 CHECK(put(&p,0x59,actual,7));memcpy(&q,&p,sizeof p);
 s5l_pcf50635_tick(&p,37);for(unsigned i=0;i<37;i++)s5l_pcf50635_tick(&q,1);
 CHECK(!memcmp(&p,&q,sizeof p));CHECK(p.seconds==desired+2);CHECK(p.tick_accum==7);

 /* The deliberately unsupported partial operation is visible and finite. */
 for(unsigned mask=1;mask<127;mask++){
  s5l_pcf50635_reset(&p,10);unsigned n=0;
  for(unsigned i=0;i<7;i++)if(mask&(1u<<i)){CHECK(put(&p,0x59+i,actual+i,1));n++;}
  s5l_pcf50635_tick(&p,10);CHECK(p.seconds==PCF50635_DEFAULT_TIME+1);
  CHECK(p.unknown_writes==n);CHECK(clean(&p));
  s5l_pcf50635_tick(&p,10);CHECK(p.unknown_writes==n);
 }
 /* Invalid digits, range, reserved bits and weekday are never normalized. */
 const uint8_t bad[][2]={{0,0x60},{0,0x1a},{0,0x80},{1,0x60},{2,0x24},{2,0x80},
  {3,7},{3,6},{4,0},{4,0x32},{5,0},{5,0x13},{6,0xfa}};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){
  uint8_t date[7];memcpy(date,actual,7);date[bad[i][0]]=bad[i][1];
  s5l_pcf50635_reset(&p,10);CHECK(put(&p,0x59,date,7));
  s5l_pcf50635_tick(&p,10);CHECK(p.seconds==PCF50635_DEFAULT_TIME+1);
  CHECK(p.unknown_writes==7);CHECK(clean(&p));
 }
 /* Independent Python UTC fixtures cover month, year and leap boundaries. */
 const struct {uint8_t date[7];uint64_t unix_seconds;} dates[]={
  {{0x00,0x00,0x00,0x06,0x01,0x01,0x00},946684800ull},
  {{0x59,0x59,0x23,0x02,0x29,0x02,0x00},951868799ull},
  {{0x59,0x59,0x23,0x03,0x28,0x02,0x01},983404799ull},
  {{0x59,0x59,0x23,0x00,0x29,0x02,0x04},1078099199ull},
  {{0x25,0x14,0x00,0x05,0x01,0x01,0x10},1262304865ull},
  {{0x59,0x59,0x23,0x02,0x31,0x12,0x24},1735689599ull},
  {{0x59,0x59,0x23,0x03,0x30,0x04,0x25},1746057599ull},
  {{0x58,0x59,0x23,0x04,0x31,0x12,0x99},4102444798ull}
 };
 for(unsigned i=0;i<sizeof dates/sizeof dates[0];i++){
  s5l_pcf50635_reset(&p,10);CHECK(put(&p,0x59,dates[i].date,7));
  s5l_pcf50635_tick(&p,10);CHECK(p.seconds==dates[i].unix_seconds);CHECK(p.unknown_writes==0);
  s5l_pcf50635_tick(&p,10);CHECK(p.seconds==dates[i].unix_seconds+1);
 }
 const uint8_t invalid_day[7]={0,0,0,4,0x29,0x02,0x01};
 s5l_pcf50635_reset(&p,10);CHECK(put(&p,0x59,invalid_day,7));
 s5l_pcf50635_tick(&p,10);CHECK(p.unknown_writes==7);CHECK(p.seconds==PCF50635_DEFAULT_TIME+1);
 /* Explicit host clock setup cancels pending bytes; guest STOP did not. */
 s5l_pcf50635_reset(&p,10);CHECK(put(&p,0x59,actual,7));
 s5l_pcf50635_set_time(&p,PCF50635_DEFAULT_TIME);CHECK(clean(&p));
 s5l_pcf50635_tick(&p,10);CHECK(p.seconds==PCF50635_DEFAULT_TIME+1);
 printf("PASS %u complete-update/phase/unsupported-input contracts\n",checks);return 0;
}
