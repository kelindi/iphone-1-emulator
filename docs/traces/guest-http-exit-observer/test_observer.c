#include "observer.h"
#include "bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,live_reads;static uint32_t forbidden(void*x,uint32_t p){(void)x;(void)p;live_reads++;return 0;}
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);return 1;}}while(0)
static void word(uint8_t*r,size_t o,uint32_t x){memcpy(r+o,&x,4);}
int main(void){
 const uint32_t n=0x200000,b=0x10000000;uint8_t*r=calloc(1,n),*old=malloc(n);arm_cpu_t*c=calloc(1,sizeof*c),*saved=malloc(sizeof*c);C(r&&old&&c&&saved);arm_bus_t bus={0};bus.read32=forbidden;c->bus=&bus;
 c->cp15.sctlr=ARM_SCTLR_M|ARM_SCTLR_XP;c->cp15.ttbr0=b+0x4000;c->cp15.dacr=1;
 word(r,0x7000,b|0x402);word(r,0x4000,(b+0x100000)|0xc02);
 qualified_process p={0xc0018000,21,0xc001a000,"iphone1-local-ht"};word(r,0x18024,p.pid);word(r,0x18138,p.task);memcpy(r+0x180eb,p.name,16);
 const qualified_site*sites[]={&site_exit1,&site_psignal,&site_sigexit_locked};
 entry_observation out,unchanged;memset(&unchanged,0xa5,sizeof unchanged);
 for(unsigned h=0;h<3;h++){
  const qualified_site*s=sites[h];size_t off=s->pc-0xc0000000u;memcpy(r+off,s->code,32);c->cpsr=s->mode;c->r[15]=s->pc;c->r[0]=p.proc;c->r[1]=9;c->cycles=123;
  memcpy(saved,c,sizeof*c);memcpy(old,r,n);C(observe_kernel_target(c,r,b,n,s,&p,&out));C(out.args[1]==9&&out.pid==21&&out.cycles==123);C(!memcmp(saved,c,sizeof*c)&&!memcmp(old,r,n)&&!live_reads);
  for(unsigned i=0;i<32;i++){r[off+i]^=1;out=unchanged;C(!observe_kernel_target(c,r,b,n,s,&p,&out));C(!memcmp(&out,&unchanged,sizeof out));r[off+i]^=1;}
  c->cpsr=ARM_MODE_USR|ARM_CPSR_T;C(!observe_kernel_target(c,r,b,n,s,&p,&out));c->cpsr=ARM_MODE_SVC;C(!observe_kernel_target(c,r,b,n,s,&p,&out));c->cpsr=s->mode;
  c->r[0]+=4;C(!observe_kernel_target(c,r,b,n,s,&p,&out));c->r[0]-=4;
  word(r,0x18024,22);C(!observe_kernel_target(c,r,b,n,s,&p,&out));word(r,0x18024,21);
  word(r,0x18138,p.task+4);C(!observe_kernel_target(c,r,b,n,s,&p,&out));word(r,0x18138,p.task);
  r[0x180eb]^=1;C(!observe_kernel_target(c,r,b,n,s,&p,&out));r[0x180eb]^=1;
  word(r,0x7000,0);out=unchanged;C(!observe_kernel_target(c,r,b,n,s,&p,&out));C(!memcmp(&out,&unchanged,sizeof out));word(r,0x7000,b|0x402);
 }
 const qualified_site*own[]={&site_number,&site_own_exit,&site_listener_poll_return,&site_accept_return,&site_client_close_return,&site_setsockopt_return,&site_accept_errno,&site_poll_errno};
 qualified_space space={c->cp15.sctlr,c->cp15.ttbr0,c->cp15.ttbr1,c->cp15.ttbcr,c->cp15.dacr,c->cp15.context_id};
 for(unsigned h=0;h<sizeof own/sizeof*own;h++){
  const qualified_site*s=own[h];memcpy(r+0x100000+s->pc,s->code,32);c->cpsr=ARM_MODE_USR;c->r[15]=s->pc;
  memcpy(saved,c,sizeof*c);memcpy(old,r,n);C(observe_own_site(c,r,b,n,s,&space,&out));C(!memcmp(saved,c,sizeof*c)&&!memcmp(old,r,n)&&!live_reads);
  uint32_t *fields[]={&c->cp15.sctlr,&c->cp15.ttbr0,&c->cp15.ttbr1,&c->cp15.ttbcr,&c->cp15.dacr,&c->cp15.context_id};
  for(unsigned i=0;i<6;i++){uint32_t before=*fields[i];*fields[i]^=1;out=unchanged;C(!observe_own_site(c,r,b,n,s,&space,&out));C(!memcmp(&out,&unchanged,sizeof out));*fields[i]=before;}
  c->cpsr|=ARM_CPSR_T;C(!observe_own_site(c,r,b,n,s,&space,&out));c->cpsr=ARM_MODE_USR;
  word(r,0x4000,(b+0x100000)|0x402);C(!observe_own_site(c,r,b,n,s,&space,&out));word(r,0x4000,(b+0x100000)|0xc02);
  for(unsigned i=0;i<32;i++){r[0x100000+s->pc+i]^=1;C(!observe_own_site(c,r,b,n,s,&space,&out));r[0x100000+s->pc+i]^=1;}
 }
 qualified_site bad=site_own_exit;bad.pc=0xfffffff0;out=unchanged;C(!observe_own_site(c,r,b,n,&bad,&space,&out));C(!memcmp(&out,&unchanged,sizeof out));C(!live_reads);
 printf("PASS %u focused binding/immutability/permission/output-atomicity checks; synthetic only, no installed hook or guest continuation\n",checks);free(saved);free(c);free(old);free(r);return 0;
}
