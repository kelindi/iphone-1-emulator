#include "checkpoint.h"
#include "board.h"
#include <stdlib.h>
#include <string.h>
/* Private tools can compare a few independent phones. No machine fields added. */
static checkpoint_phone_t *phones[8];
static checkpoint_phone_t *lookup(void*ctx){for(unsigned i=0;i<8;i++)if(phones[i]&&phones[i]->machine==ctx)return phones[i];return NULL;}
static bool in_sdio(uint32_t a){return a>=SDIO_BASE&&a-SDIO_BASE<SDIO_SIZE;}
void checkpoint_sync(checkpoint_phone_t*p){sdio_step(&p->device,p->machine->timer.ticks);s5l_vic_set_line(&p->machine->vic[1],10,sdio_irq_level(&p->device));}
#define READ(bits,type) static type read##bits(void*ctx,uint32_t a){checkpoint_phone_t*p=lookup(ctx);if(!p)return 0;if(!in_sdio(a))return p->underlying.read##bits(ctx,a);type v=(type)sdio_read(&p->device,a-SDIO_BASE,bits/8,p->machine->timer.ticks);if(p->observe)p->observe(p,false,a,v,bits/8,true);checkpoint_sync(p);return v;}
#define WRITE(bits,type) static void write##bits(void*ctx,uint32_t a,type v){checkpoint_phone_t*p=lookup(ctx);if(!p)return;if(!in_sdio(a)){p->underlying.write##bits(ctx,a,v);return;}bool accepted=sdio_write(&p->device,a-SDIO_BASE,v,bits/8,p->machine->timer.ticks);if(p->observe)p->observe(p,true,a,v,bits/8,accepted);checkpoint_sync(p);}
READ(8,uint8_t) READ(16,uint16_t) READ(32,uint32_t)
WRITE(8,uint8_t) WRITE(16,uint16_t) WRITE(32,uint32_t)
static const uint8_t*dma(void*ctx,uint32_t a,size_t n){checkpoint_phone_t*p=ctx;if(n>UINT32_MAX||!p->underlying.host_ram)return NULL;return p->underlying.host_ram(p->underlying.ctx,a,(uint32_t)n);}
bool checkpoint_phone_init(checkpoint_phone_t*p,uint32_t base,uint32_t n,const sdio_config_t*c){
 if(!p||!c||p->machine||base!=s5l_board_m68.ram_base)return false;
 memset(p,0,sizeof *p);unsigned slot=0;while(slot<8&&phones[slot])slot++;if(slot==8)return false;
 p->machine=calloc(1,sizeof *p->machine);if(!p->machine)return false;
 if(!s5l_board_init(&s5l_board_m68,p->machine,n)){free(p->machine);p->machine=NULL;return false;}
 if(!sdio_init(&p->device,c)||!s5l8900_set_direct_ram_writes(p->machine,false)){checkpoint_phone_free(p);return false;}
 p->underlying=p->machine->bus;phones[slot]=p;sdio_bind_dma_reader(&p->device,dma,p);
 p->machine->bus.read8=read8;p->machine->bus.read16=read16;p->machine->bus.read32=read32;
 p->machine->bus.write8=write8;p->machine->bus.write16=write16;p->machine->bus.write32=write32;
 return true;
}
void checkpoint_phone_free(checkpoint_phone_t*p){if(!p)return;for(unsigned i=0;i<8;i++)if(phones[i]==p)phones[i]=NULL;if(p->machine){s5l8900_free(p->machine);free(p->machine);}memset(p,0,sizeof *p);}
static checkpoint_instant_t at(const checkpoint_phone_t*p){s5l8900_t*m=p->machine;return(checkpoint_instant_t){m->timer.ticks,m->cpu.cycles,m->cpu.r[15],m->cpu_hz,m->ram_base,m->ram_size};}
static bool m68_wired(const s5l8900_t*m){return m&&m->nor_base==S5L8900_M68_NOR_BASE&&m->ram_base==s5l_board_m68.ram_base&&m->i2c[0].slave_count==2&&m->i2c[0].slaves[0].addr==PCF50635_I2C_ADDR&&m->i2c[0].slaves[0].ctx==&m->pmu&&m->i2c[0].slaves[1].addr==WM8758_I2C_ADDR&&m->i2c[0].slaves[1].ctx==&m->codec&&m->spi[0].slaves[0].ctx==&m->spi[0]&&m->spi[1].slaves[0].ctx==&m->spi[1]&&m->spi[2].slaves[0].ctx==&m->mtz1;}
bool checkpoint_pair_save(const checkpoint_phone_t*p,uint8_t**machine,size_t*mn,uint8_t**side,size_t*sn){
 if(!p||!m68_wired(p->machine)||!machine||!mn||!side||!sn)return false;
 if(((p->machine->vic[1].raw>>10)&1u)!=(unsigned)sdio_irq_level(&p->device))return false;
 uint8_t*s=NULL,*m=NULL;size_t sl=0,ml=0;checkpoint_instant_t i=at(p);
 if(!checkpoint_side_save(&p->device,&i,&s,&sl))return false;
 if(snapshot_save_mem(p->machine,&m,&ml)!=SNAP_OK){free(s);return false;}
 *machine=m;*mn=ml;*side=s;*sn=sl;return true;
}
checkpoint_phone_t*checkpoint_pair_load(const uint8_t*m,size_t mn,const uint8_t*s,size_t sn){
 if(!m||!s||!mn||mn>300u*1024*1024)return NULL;
 sdio_t*d=calloc(1,sizeof *d);if(!d)return NULL;checkpoint_instant_t i={0};
 if(!checkpoint_side_load(d,&i,s,sn)){free(d);return NULL;}
 checkpoint_phone_t*p=calloc(1,sizeof *p);if(!p){free(d);return NULL;}
 bool ok=checkpoint_phone_init(p,i.ram_base,i.ram_size,&d->config);free(d);
 if(ok)ok=snapshot_load_mem(p->machine,m,mn)==SNAP_OK;
 if(ok)ok=checkpoint_side_load(&p->device,&i,s,sn);
 if(ok){checkpoint_instant_t j=at(p);ok=i.ticks==j.ticks&&i.cycles==j.cycles&&i.pc==j.pc&&i.cpu_hz==j.cpu_hz&&i.ram_base==j.ram_base&&i.ram_size==j.ram_size;}
 if(ok)ok=m68_wired(p->machine);
 if(ok)ok=((p->machine->vic[1].raw>>10)&1u)==(unsigned)sdio_irq_level(&p->device);
 if(!ok){checkpoint_phone_free(p);free(p);return NULL;}return p;
}
