#include "transport.h"
#include <string.h>
#include <stdlib.h>
static const uint8_t expected_caller[32]={82,138,2,235,0,48,149,229,12,32,149,229,8,16,157,229,2,32,99,224,2,48,138,224,1,48,67,226,34,230,160,225};
bool transport_manager(const destination_observer*d,const uint8_t*ram,uint32_t base,uint32_t size,uint8_t out[64]){
 if(!d||!d->captured||!d->context||!d->caller_available||d->caller_address!=0x3122b2b4||d->regs[14]!=0x3122b2b8||d->regs[5]!=0x39254d38||(d->cpsr&63)!=16||(d->context->cpsr&63)!=16||memcmp(d->caller,expected_caller,32))return false;
 uint8_t bytes[64],code[32];
 if(!amc_peek_ram(d->context,ram,base,size,d->caller_address,code,32)||memcmp(code,expected_caller,32)||!amc_peek_ram(d->context,ram,base,size,d->regs[5],bytes,64))return false;
 memcpy(out,bytes,64);return true;
}
bool transport_lli(const s5l8900_t*m,unsigned d,unsigned c,uint8_t out[16]){
 if(!m||!m->ram||d>=2||c>=8)return false;
 const s5l_pl080_t*p=&m->dmac[d];const s5l_pl080_chan_t*x=&p->ch[c];uint32_t a=x->lli&~3u;
 /* Existing runnable enable, not the always-zero model Active bit. No chain walk. */
 if(!(p->config&1)||!(x->cfg&1)||(x->cfg&PL080_CFG_HALT)||!a||a<m->ram_base||(uint64_t)a+16>(uint64_t)m->ram_base+m->ram_size)return false;
 memcpy(out,m->ram+(a-m->ram_base),16);return true;
}
static bool add(transport_values*v,const char*n,uint64_t x){if(v->count>=TRANSPORT_VALUES)return false;v->names[v->count]=n;v->values[v->count++]=x;return true;}
#define V(n,x) do{if(!add(v,n,(x)))return false;}while(0)
bool transport_collect(const s5l8900_t*m,transport_values*v){
 if(!m||!v||m->stub_count>S5L_STUB_MAX)return false;memset(v,0,sizeof *v);
 V("cycles",m->cpu.cycles);V("timerTicks",m->timer.ticks);
 for(unsigned i=0;i<2;i++){const s5l_i2s_t*x=&m->i2s[i];V("i2s.index",i);for(unsigned j=0;j<7;j++)V("i2s.reg",x->regs[j]);V("i2s.reads",x->reads);V("i2s.writes",x->writes);V("i2s.unknownReads",x->unknown_reads);V("i2s.unknownWrites",x->unknown_writes);V("i2s.unknownCount",x->unknown_off_count);for(unsigned j=0;j<S5L_I2S_UNKNOWN_OFF;j++)V("i2s.unknownOffset",x->unknown_off[j]);}
 for(unsigned i=0;i<2;i++){const s5l_pl080_t*x=&m->dmac[i];V("dmac.index",i);V("dmac.config",x->config);V("dmac.sync",x->sync);V("dmac.rawTc",x->raw_tc);V("dmac.rawErr",x->raw_err);
 V("dmac.reads",x->reads);V("dmac.writes",x->writes);V("dmac.unknownReads",x->unknown_reads);V("dmac.unknownWrites",x->unknown_writes);V("dmac.items",x->items);V("dmac.transfers",x->transfers);V("dmac.bytes",x->bytes_moved);V("dmac.completions",x->completions);V("dmac.configWrites",x->config_writes);V("dmac.configFirst",x->config_first);
 for(unsigned j=0;j<8;j++){const s5l_pl080_chan_t*c=&x->ch[j];V("channel.index",j);V("channel.src",c->src);V("channel.dst",c->dst);V("channel.lli",c->lli);V("channel.ctrl",c->ctrl);V("channel.cfg",c->cfg);V("channel.runs",c->runs);V("channel.bytes",c->bytes);}}
 V("power.state",m->power.state);V("power.cfg0",m->power.cfg0);V("power.cfg1",m->power.cfg1);V("power.sram",m->power.sram);V("power.cfg24",m->power.cfg24);V("power.cfg28",m->power.cfg28);
 for(unsigned j=0;j<WM8991_NREG;j++){V("wm8758.reg",m->codec.regs[j]);V("wm8758.written",m->codec.written[j]);}V("wm8758.reads",m->codec.reg_reads);V("wm8758.writes",m->codec.reg_writes);
 unsigned found=0;for(unsigned i=0;i<m->stub_count;i++){const s5l_stub_t*x=&m->stubs[i];if(x->base!=S5L8900_CLOCK_BASE)continue;if(found++||!x->regs||x->nregs>1024)return false;V("clock.base",x->base);V("clock.size",x->size);V("clock.count",x->nregs);V("clock.reads",x->reads);V("clock.writes",x->writes);V("clock.oob",x->oob);for(unsigned j=0;j<x->nregs;j++)V("clock.reg",x->regs[j]);}V("clock.available",found);
 return true;
}
static bool filebytes(const char*dir,const char*stage,const char*suffix,const void*b,size_t n){char p[1024];if(snprintf(p,sizeof p,"%s/transport-%s-%s",dir,stage,suffix)>=(int)sizeof p)return false;FILE*f=fopen(p,"wb");if(!f)return false;bool ok=fwrite(b,1,n,f)==n;return fclose(f)==0&&ok;}
bool transport_export(const s5l8900_t*m,const destination_observer*d,const char*dir,const char*stage,bool final){
 transport_values*v=malloc(sizeof*v);if(!v)return false;if(!transport_collect(m,v)){free(v);return false;}uint8_t manager[64]={0},llis[256]={0};bool available=transport_manager(d,m->ram,m->ram_base,m->ram_size,manager);unsigned mask=0;
 if(final)for(unsigned i=0;i<16;i++)if(transport_lli(m,i/8,i%8,llis+i*16))mask|=1u<<i;
 char p[1024];if(snprintf(p,sizeof p,"%s/transport-%s.json",dir,stage)>=(int)sizeof p){free(v);return false;}FILE*f=fopen(p,"w");if(!f){free(v);return false;}
 fprintf(f,"{\"stage\":\"%s\",\"managerAvailable\":%s,\"managerAddress\":%u,\"lliAvailableMask\":%u,\"values\":[",stage,available?"true":"false",d&&d->captured?d->regs[5]:0,mask);
 for(unsigned i=0;i<v->count;i++)fprintf(f,"%s[\"%s\",%llu]",i?",":"",v->names[i],(unsigned long long)v->values[i]);fputs("]}\n",f);bool ok=fclose(f)==0;
 ok=filebytes(dir,stage,"values.bin",v->values,v->count*8)&&ok;ok=filebytes(dir,stage,"manager.bin",manager,64)&&ok;ok=filebytes(dir,stage,"lli.bin",llis,256)&&ok;free(v);return ok;
}
