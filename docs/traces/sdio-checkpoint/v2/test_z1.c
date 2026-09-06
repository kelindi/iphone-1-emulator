#include "checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stddef.h>
static unsigned checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %s:%d %s\n",__func__,__LINE__,#x);}}while(0)
static void init(checkpoint_phone_t*p){sdio_config_t c={.present=true,.enumeration=true,.boot_endpoint=true,.data_transfer=true,.endpoint_capacity=512,.io_port=0x10000,.function_init_ticks=1800,.io_functions=1,.assigned_rca=1,.voltage_mask=0x60000,.readable_cccr=0x101,.command_ticks=600,.data_ticks=600};CHECK(checkpoint_phone_init(p,0x08000000u,1u<<20,&c));if(!p->machine)exit(2);s5l_mtz1_power_pin(&p->machine->mtz1,true);p->machine->mtz1.firmware=true;p->machine->mtz1.uptime_ms=1234;p->machine->mtz1.uptime_tick_acc=3500;checkpoint_sync(p);}
static uint8_t transfer(checkpoint_phone_t*p,uint8_t b){s5l_spi_slave_t*s=&p->machine->spi[2].slaves[0];return s->transfer(s->ctx,b);}
static void select(checkpoint_phone_t*p,bool selected){s5l_mtz1_select_pin(&p->machine->mtz1,!selected);}
static void typed_equal_z(const s5l_mtz1_t*a,const s5l_mtz1_t*b){
#include "z1-independent-equal.inc"
}
static void equal_z(const s5l_mtz1_t*a,const s5l_mtz1_t*b){typed_equal_z(a,b);CHECK(!memcmp(a->last_contacts,b->last_contacts,sizeof a->last_contacts));CHECK(!memcmp(a->held,b->held,sizeof a->held));}
static void equal(checkpoint_phone_t*a,checkpoint_phone_t*b){equal_z(&a->machine->mtz1,&b->machine->mtz1);uint8_t*am,*as,*bm,*bs;size_t an,al,bn,bl;CHECK(checkpoint_pair_save(a,&am,&an,&as,&al));CHECK(checkpoint_pair_save(b,&bm,&bn,&bs,&bl));CHECK(an==bn&&!memcmp(am,bm,an));CHECK(al==bl&&!memcmp(as,bs,al));free(am);free(as);free(bm);free(bs);}
static checkpoint_phone_t*clone(checkpoint_phone_t*p){
 /* Standard v39 stores these arrays as raw bytes. The portable extension must
  * preserve padding already supplied by that standard snapshot. */
 size_t first=offsetof(s5l_mt_contact_t,minor)+sizeof(((s5l_mt_contact_t*)0)->minor);
 CHECK(first<sizeof(s5l_mt_contact_t));
 for(unsigned j=0;j<MTZ2_CONTACT_MAX;j++)for(size_t k=first;k<sizeof(s5l_mt_contact_t);k++){((uint8_t*)&p->machine->mtz1.last_contacts[j])[k]=(uint8_t)(0xa0+j);((uint8_t*)&p->machine->mtz1.held[j])[k]=(uint8_t)(0xb0+j);}
 uint8_t*m,*s;size_t mn,sn;CHECK(checkpoint_pair_save(p,&m,&mn,&s,&sn));checkpoint_phone_t*b=checkpoint_pair_load(m,mn,s,sn);CHECK(b!=NULL);free(m);free(s);if(!b)exit(3);equal(p,b);return b;}
static void release(checkpoint_phone_t*p){checkpoint_phone_free(p);free(p);}
static void read_pending(checkpoint_phone_t*a,checkpoint_phone_t*b){unsigned count=a->machine->mtz1.pending_len+3;select(a,true);select(b,true);for(unsigned i=0;i<count;i++)CHECK(transfer(a,i?0:0x47)==transfer(b,i?0:0x47));select(a,false);select(b,false);CHECK(!a->machine->mtz1.pending_len&&!b->machine->mtz1.pending_len);equal(a,b);}
static void scenario(unsigned mode){checkpoint_phone_t a={0};init(&a);s5l_mtz1_t*z=&a.machine->mtz1;
 if(mode<2){select(&a,true);transfer(&a,0xd0);for(unsigned i=1;i<(mode?2u:8u);i++)transfer(&a,0);if(!mode)select(&a,false);}
 if(mode==2){z->firmware=false;for(unsigned i=0;i<8;i++)transfer(&a,i?i:0xc2);CHECK(!z->selected&&z->rx_len==8);}
 if(mode==3){z->firmware=false;z->stage_b=true;select(&a,true);for(unsigned i=0;i<1031;i++)transfer(&a,0x44);CHECK(z->transaction_overflow&&z->rx_len==1031);}
 if(mode==4){s5l_mt_contact_t c={.id=1,.phase=MTZ2_PHASE_TOUCHING,.x=70,.y=120,.pressure=88,.major=7,.minor=5};CHECK(s5l_mtz1_set_contacts(z,&c,1));CHECK(z->pending_len&&z->held_n==1);}
 checkpoint_phone_t*b=clone(&a);
 if(mode==0){CHECK(!z->selected&&z->pkt_len==0&&z->rx_len==8);select(&a,true);select(b,true);CHECK(z->rx_len==0&&b->machine->mtz1.rx_len==0);for(unsigned i=0;i<8;i++)CHECK(transfer(&a,i?0:0xd0)==transfer(b,i?0:0xd0));select(&a,false);select(b,false);}
 if(mode==1){CHECK(z->selected&&z->rx_len==2);for(unsigned i=0;i<6;i++)CHECK(transfer(&a,0)==transfer(b,0));select(&a,false);select(b,false);}
 if(mode==2){select(&a,true);select(b,true);CHECK(z->rx_len==8&&b->machine->mtz1.rx_len==8&&z->aspeed_stream);for(unsigned i=8;i<1024;i++)CHECK(transfer(&a,(uint8_t)i)==transfer(b,(uint8_t)i));select(&a,false);select(b,false);CHECK(z->rx_len==1024);}
 if(mode==3){for(unsigned i=0;i<19;i++)CHECK(transfer(&a,(uint8_t)i)==transfer(b,(uint8_t)i));select(&a,false);select(b,false);}
 if(mode==4){read_pending(&a,b);s5l_mtz1_tick(z,6000);s5l_mtz1_tick(&b->machine->mtz1,6000);s5l_mt_contact_t c=z->held[0];c.x+=10;CHECK(s5l_mtz1_set_contacts(z,&c,1));CHECK(s5l_mtz1_set_contacts(&b->machine->mtz1,&c,1));CHECK(z->frame_ms==1235&&z->uptime_tick_acc==3500);CHECK(((unsigned)z->pending[24]|(unsigned)z->pending[25]<<8)==11280);CHECK(z->pending[26]==0&&z->pending[27]==0);read_pending(&a,b);CHECK(s5l_mtz1_set_contacts(z,NULL,0));CHECK(s5l_mtz1_set_contacts(&b->machine->mtz1,NULL,0));s5l_mtz1_tick(z,60000);s5l_mtz1_tick(&b->machine->mtz1,60000);CHECK(z->pending_len&&z->held_n==0);read_pending(&a,b);}
 equal(&a,b);s5l_mtz1_power_pin(z,false);s5l_mtz1_power_pin(&b->machine->mtz1,false);equal(&a,b);release(b);checkpoint_phone_free(&a);
}
static void omission_negatives(void){checkpoint_phone_t a={0};init(&a);select(&a,true);transfer(&a,0xd0);transfer(&a,0);checkpoint_phone_t*b=clone(&a);
 /* Deliberately erase only omitted legacy receive storage, retaining length.
  * Real firmware byte response changes at the next byte (2 versus0). */
 memset(b->machine->mtz1.rx,0,sizeof b->machine->mtz1.rx);uint8_t av=transfer(&a,0),bv=transfer(b,0);CHECK(av==2&&bv==0&&av!=bv);release(b);checkpoint_phone_free(&a);
 init(&a);b=clone(&a);b->machine->mtz1.uptime_ms=0;b->machine->mtz1.uptime_tick_acc=0;s5l_mt_contact_t c={.id=1,.x=70,.y=120,.phase=4,.pressure=80};CHECK(s5l_mtz1_set_contacts(&a.machine->mtz1,&c,1));CHECK(s5l_mtz1_set_contacts(&b->machine->mtz1,&c,1));CHECK(a.machine->mtz1.frame_ms==1234&&b->machine->mtz1.frame_ms==1);CHECK(memcmp(a.machine->mtz1.pending,b->machine->mtz1.pending,sizeof a.machine->mtz1.pending));release(b);checkpoint_phone_free(&a);
}
static uint32_t le32(const uint8_t*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void rehash(uint8_t*b,size_t n){uint64_t h=1469598103934665603ull;for(size_t i=0;i<n-8;i++){h^=b[i];h*=1099511628211ull;}for(unsigned i=0;i<8;i++)b[n-8+i]=(uint8_t)(h>>(8*i));}
static checkpoint_instant_t instant(checkpoint_phone_t*p){s5l8900_t*m=p->machine;return(checkpoint_instant_t){m->timer.ticks,m->cpu.cycles,m->cpu.r[15],m->cpu_hz,m->ram_base,m->ram_size};}
static void negatives(void){checkpoint_phone_t a={0};init(&a);select(&a,true);transfer(&a,0xd0);for(unsigned i=1;i<8;i++)transfer(&a,0);select(&a,false);s5l_mt_contact_t contacts[2]={{.id=1,.phase=4,.x=70,.y=120,.pressure=80},{.id=2,.phase=4,.x=100,.y=160,.pressure=80}};CHECK(s5l_mtz1_set_contacts(&a.machine->mtz1,contacts,2));uint8_t*m,*s;size_t mn,sn;CHECK(checkpoint_pair_save(&a,&m,&mn,&s,&sn));
 /* Known v1 flaw remains explicit: ordinary loader loses rx_len and uptime. */
 checkpoint_phone_t old={0};init(&old);old.machine->mtz1.uptime_ms=0;old.machine->mtz1.uptime_tick_acc=0;CHECK(snapshot_load_mem(old.machine,m,mn)==SNAP_OK);CHECK(a.machine->mtz1.rx_len==8&&old.machine->mtz1.rx_len==0);CHECK(old.machine->mtz1.uptime_ms!=a.machine->mtz1.uptime_ms);checkpoint_phone_free(&old);
 checkpoint_instant_t at=instant(&a);uint8_t*v1;size_t vn;CHECK(checkpoint_side_save(&a.device,&at,&v1,&vn));CHECK(!checkpoint_pair_load(m,mn,v1,vn));free(v1);
 size_t zb=24+le32(s+16);uint8_t*bad=malloc(sn);CHECK(bad!=NULL);if(!bad)exit(2);
 /* Envelope corruption without hash correction. */
 for(unsigned i=0;i<32;i++){memcpy(bad,s,sn);bad[i]^=1;CHECK(!checkpoint_pair_load(m,mn,bad,sn));}
 memcpy(bad,s,sn);bad[8]=1;rehash(bad,sn);CHECK(!checkpoint_pair_load(m,mn,bad,sn));
 /* Source-derived offsets supplied from the independent inventory. */
#include "z1-negative-offsets.inc"
 for(unsigned i=0;i<sizeof mutations/sizeof mutations[0];i++){memcpy(bad,s,sn);for(unsigned j=0;j<mutations[i].size;j++)bad[zb+mutations[i].offset+j]=(uint8_t)(mutations[i].value>>(8*j));rehash(bad,sn);sdio_t rejected=a.device;s5l_mtz1_t rz=a.machine->mtz1;checkpoint_instant_t ri=at;CHECK(!checkpoint_bundle_load(&rejected,&rz,&ri,bad,sn));CHECK(!checkpoint_pair_load(m,mn,bad,sn));}
 memcpy(bad,s,sn);bad[zb+2]=1;rehash(bad,sn);CHECK(!checkpoint_pair_load(m,mn,bad,sn)); /* valid bool conflicting with standard selected=0 */
 for(size_t n=0;n<32;n++)CHECK(!checkpoint_pair_load(m,mn,s,n));CHECK(!checkpoint_pair_load(m,mn,s,sn-1));
 /* Failed direct decode preserves all output bytes and host trace/callbacks. */
 sdio_t sd=a.device;s5l_mtz1_t z=a.machine->mtz1;z.trace=true;for(unsigned j=0;j<MTZ2_CONTACT_MAX;j++){((uint8_t*)&z.last_contacts[j])[sizeof(s5l_mt_contact_t)-1]=0xa5;((uint8_t*)&z.held[j])[sizeof(s5l_mt_contact_t)-1]=0xb5;}sdio_t before=sd;s5l_mtz1_t bz=z;memcpy(bad,s,sn);bad[zb]=2;rehash(bad,sn);checkpoint_instant_t stamp=at;
 CHECK(!checkpoint_bundle_load(&sd,&z,&stamp,bad,sn));CHECK(!memcmp(&before,&sd,sizeof sd)&&!memcmp(&bz,&z,sizeof z));CHECK(!memcmp(&stamp,&at,sizeof at));
 CHECK(checkpoint_bundle_load(&sd,&z,&stamp,s,sn));CHECK(z.trace&&sd.dma_reader==a.device.dma_reader&&sd.dma_ctx==a.device.dma_ctx);for(unsigned j=0;j<MTZ2_CONTACT_MAX;j++){CHECK(((uint8_t*)&z.last_contacts[j])[sizeof(s5l_mt_contact_t)-1]==0xa5);CHECK(((uint8_t*)&z.held[j])[sizeof(s5l_mt_contact_t)-1]==0xb5);}
 typed_equal_z(&z,&a.machine->mtz1);
 free(bad);free(m);free(s);checkpoint_phone_free(&a);
}
int main(void){for(unsigned i=0;i<5;i++)scenario(i);omission_negatives();negatives();printf("Private checkpoint v2 Z1: %u checks, %u failures\n",checks,failures);return failures?1:0;}
