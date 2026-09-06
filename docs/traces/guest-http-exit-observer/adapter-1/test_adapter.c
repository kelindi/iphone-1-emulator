/* Synthetic event sequences plus actual ARM BLX / Thumb store execution.
 * No retained guest snapshot or operating-system continuation is run. */
#include "adapter.c"
#include <stdlib.h>
static unsigned checks,external_reads;static uint8_t*ram;static const uint32_t BASE=0x10000000,SIZE=0x500000;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d %s reason=%s phase=%u\n",__LINE__,#x,a.reason,a.phase);return 1;}}while(0)
static uint32_t rd(void*ctx,uint32_t p){(void)ctx;uint32_t v=0;if(p>=BASE&&p<=BASE+SIZE-4)memcpy(&v,ram+p-BASE,4);else external_reads++;return v;}
static uint8_t rd8(void*ctx,uint32_t p){(void)ctx;if(p>=BASE&&p<BASE+SIZE)return ram[p-BASE];external_reads++;return 0;}
static uint16_t rd16(void*ctx,uint32_t p){return (uint16_t)rd8(ctx,p)|((uint16_t)rd8(ctx,p+1)<<8);}
static void wr(void*ctx,uint32_t p,uint32_t v){(void)ctx;if(p>=BASE&&p<=BASE+SIZE-4)memcpy(ram+p-BASE,&v,4);else external_reads++;}
static uint32_t physical(uint32_t v){if(v<0x100000)return 0x10300000+v;if(v>=0xc0000000&&v<0xc0200000)return BASE+v-0xc0000000;if(v>=0xcc200000&&v<0xcc300000)return 0x10200000+v-0xcc200000;if(v>=0x30000000&&v<0x30100000)return 0x10400000+v-0x30000000;abort();}
static void wv(uint32_t v,uint32_t w){wr(NULL,physical(v),w);}static void cv(uint32_t v,const void*p,size_t n){memcpy(ram+physical(v)-BASE,p,n);}
static void map(uint32_t va,uint32_t pa,bool usr){wr(NULL,BASE+0x4000+(va>>20)*4,pa|(usr?0xc02:0x402));}
static void fixture(s5l8900_t*m){memset(ram,0,SIZE);memset(m,0,sizeof*m);m->ram=ram;m->ram_base=BASE;m->ram_size=SIZE;m->cpu.cp15.sctlr=ARM_SCTLR_M|ARM_SCTLR_XP;m->cpu.cp15.ttbr0=BASE+0x4000;m->cpu.cp15.dacr=1;m->bus.ctx=m;m->bus.read32=rd;m->bus.read16=rd16;m->bus.read8=rd8;m->bus.write32=wr;m->cpu.bus=&m->bus;
 map(0xc0000000,BASE,false);map(0xc0100000,BASE+0x100000,false);map(0xcc200000,BASE+0x200000,false);map(0,BASE+0x300000,true);map(0x30000000,BASE+0x400000,true);
 cv(0xc0060028,kernel_setthread,12);cv(0xc0026040,kernel_currenttask,10);cv(0xc002b5ac,kernel_bsdtask,8);cv(0xc002b5bc,kernel_bsdthread,8);cv(0xc00f7094,kernel_exit1,32);cv(0xc00fd790,kernel_psignal,10);cv(0xc00fdf6c,kernel_sigexit,32);cv(0xc00f710e,kernel_statuspre,2);cv(0xc00f7110,kernel_statuspost,4);
 wv(0xcc2e5d98+0x24,21);wv(0xcc2e5d98+0x138,0xcc285620);cv(0xcc2e5d98+0xeb,"iphone1-local-ht",16);wv(0xc00606ac,0xc01d7d34);wv(0xc01d7d34,0xc0008000);wv(0xc0008000+0x2fc,0xcc285620);wv(0xcc285620+0x178,0xcc2e5d98);wv(0xc0008000+0x3b4,0xc000a000);wv(0xc000a000+0xa4,0);
 for(unsigned i=0;i<6;i++){const callsite*q=&calls[i];cv(q->pc,q->own,32);cv(q->pc+4,q->ret,32);cv(q->fileva,q->lib,32);wv(q->slot,q->fileva);if(q->errcall){cv(q->errcall,q->ec,32);cv(q->errread,q->er,32);cv(q->errread+4,q->ea,32);}}
 cv(0x30001bb0,lib_error,32);wv(0x4028,0x30001bb1);m->cpu.cpsr=ARM_MODE_USR;m->cpu.r[13]=0x80000;m->cpu.cycles=100;
}
static void at(s5l8900_t*m,uint32_t pc,uint32_t mode){m->cpu.r[15]=pc;m->cpu.cpsr=mode;m->cpu.cycles++;}
int main(void){ram=malloc(SIZE);uint8_t*before=malloc(SIZE);s5l8900_t*m=calloc(1,sizeof*m);arm_cpu_t*cpu_copy=malloc(sizeof*cpu_copy);exit_adapter a;FILE*f=tmpfile();C(ram&&before&&m&&f&&cpu_copy);
 for(unsigned i=1;i<6;i++)for(unsigned failed=0;failed<2;failed++){
  fixture(m);C(exit_adapter_init(&a,m,f));const callsite*q=&calls[i];at(m,q->pc,ARM_MODE_USR);m->cpu.r[0]=i==5?1:3;m->cpu.r[1]=0x9000;m->cpu.r[2]=4;m->cpu.r[q->reg]=q->fileva;cv(0x9000,"test",4);memcpy(before,ram,SIZE);memcpy(cpu_copy,&m->cpu,sizeof m->cpu);C(!exit_adapter_hook(&a));C(!a.stop&&a.phase==CALL&&!a.owner_executed);C(!memcmp(before,ram,SIZE)&&!memcmp(cpu_copy,&m->cpu,sizeof m->cpu));
  C(arm_step(&m->cpu)==ARM_OK);C(m->cpu.r[15]==q->fileva);C(!exit_adapter_hook(&a));C(!a.stop&&a.phase==ENTERED&&a.owner_executed);
  at(m,q->fileva+q->svc,ARM_MODE_USR);m->cpu.r[12]=230;C(!exit_adapter_hook(&a));C(a.phase==SYSTEM_ENTERED&&!a.stop);
  /* A different process may visit every shared/own numeric PC while the
   * owned syscall sleeps. Its events neither advance nor fail our pairing. */
  uint32_t foreign_pcs[]={q->fileva,q->fileva+q->svc,q->fileva+q->svc+4,q->pc+4,q->pc};
  for(unsigned j=0;j<5;j++){wv(0xcc285620+0x178,0xcc2e0000);at(m,foreign_pcs[j],ARM_MODE_USR);exit_adapter old=a;memcpy(cpu_copy,&m->cpu,sizeof m->cpu);memcpy(before,ram,SIZE);C(!exit_adapter_hook(&a));C(!memcmp(&old,&a,sizeof a));C(!memcmp(cpu_copy,&m->cpu,sizeof m->cpu)&&!memcmp(before,ram,SIZE));wv(0xcc285620+0x178,a.proc);}
  at(m,q->fileva+q->svc+4,ARM_MODE_USR|(failed?ARM_CPSR_C:0));m->cpu.r[0]=failed?35:0;C(!exit_adapter_hook(&a));C(!a.stop&&a.phase==SYSTEM_RETURNED);
  at(m,q->pc+4,ARM_MODE_USR);m->cpu.r[0]=failed?UINT32_MAX:0;C(!exit_adapter_hook(&a));C(!a.stop);
  if(failed&&q->errcall){C(a.phase==ERROR_WAIT);at(m,q->errcall,ARM_MODE_USR);m->cpu.r[0]=0x30001bb1;C(!exit_adapter_hook(&a));C(!a.stop&&a.phase==ERROR_CALL);C(arm_step(&m->cpu)==ARM_OK);C(!exit_adapter_hook(&a));C(!a.stop&&a.phase==ERROR_ENTERED);at(m,q->errread,ARM_MODE_USR);m->cpu.r[0]=0x9000;wv(0x9000,35);C(!exit_adapter_hook(&a));C(a.phase==ERROR_READ&&!a.stop);C(arm_step(&m->cpu)==ARM_OK);C(!exit_adapter_hook(&a));C(!a.stop&&a.phase==IDLE);}else C(a.phase==IDLE);
 }
 /* Kernel entry is not exit completion; actual Thumb STR must commit. */
 fixture(m);C(exit_adapter_init(&a,m,f));at(m,0xc00f7094,ARM_MODE_SVC|ARM_CPSR_T);m->cpu.r[0]=a.proc;m->cpu.r[1]=0x4600;C(!exit_adapter_hook(&a));C(a.exit_entered&&!a.stop&&!a.status_pending);at(m,0xc00f710e,ARM_MODE_SVC|ARM_CPSR_T);m->cpu.r[4]=a.proc;m->cpu.r[3]=0x128;m->cpu.r[2]=0x4600;C(!exit_adapter_hook(&a));C(a.status_pending&&!a.stop);C(arm_step(&m->cpu)==ARM_OK);C(!exit_adapter_hook(&a));C(a.stop&&!strcmp(a.reason,"observed-exit-status-store"));
 /* Independent ordering/binding negatives. */
 for(unsigned neg=0;neg<13;neg++){
  fixture(m);C(exit_adapter_init(&a,m,f));const callsite*q=&calls[1];at(m,q->pc,ARM_MODE_USR);m->cpu.r[3]=q->fileva;
  if(neg==0)wv(q->slot,q->fileva+4);if(neg==1)ram[physical(q->pc)-BASE]^=1;if(neg==2)wv(a.proc+0x24,22);if(neg==3)wv(0xc000a000+0xa4,1u<<25);
  C(!exit_adapter_hook(&a));if(neg<4){C(a.stop);continue;}
  C(!a.stop);if(neg==4){at(m,q->pc+4,ARM_MODE_USR);C(!exit_adapter_hook(&a));C(a.stop);continue;}
  C(arm_step(&m->cpu)==ARM_OK);if(neg==5)m->cpu.r[14]+=4;if(neg==6)m->cpu.cp15.context_id++;if(neg==7)m->cpu.cpsr|=ARM_CPSR_T;if(neg==8)m->cpu.cycles++;
  C(!exit_adapter_hook(&a));if(neg<9){C(a.stop);continue;}
  at(m,q->fileva+q->svc+(neg==9?4:0),ARM_MODE_USR);C(!exit_adapter_hook(&a));if(neg==9){C(a.stop);continue;}
  at(m,q->fileva+q->svc+4,ARM_MODE_USR|ARM_CPSR_C);m->cpu.r[0]=35;C(!exit_adapter_hook(&a));at(m,q->pc+4,ARM_MODE_USR);m->cpu.r[0]=neg==10?0:UINT32_MAX;C(!exit_adapter_hook(&a));if(neg==10){C(a.stop);continue;}
  at(m,q->errcall,ARM_MODE_USR);m->cpu.r[0]=0x30001bb1;C(!exit_adapter_hook(&a));C(arm_step(&m->cpu)==ARM_OK);C(!exit_adapter_hook(&a));at(m,q->errread,ARM_MODE_USR);m->cpu.r[0]=0x9000;wv(0x9000,neg==11?53:35);C(!exit_adapter_hook(&a));C(arm_step(&m->cpu)==ARM_OK);if(neg==12)m->cpu.cycles++;C(!exit_adapter_hook(&a));C(a.stop);
 }
 for(unsigned neg=0;neg<4;neg++){
  fixture(m);C(exit_adapter_init(&a,m,f));at(m,0xc00f7094,ARM_MODE_SVC|ARM_CPSR_T);m->cpu.r[0]=a.proc;m->cpu.r[1]=0x4600;C(!exit_adapter_hook(&a));at(m,0xc00f710e,ARM_MODE_SVC|ARM_CPSR_T);m->cpu.r[4]=a.proc;m->cpu.r[3]=0x128;m->cpu.r[2]=0x4600;if(neg==0)a.exit_entered=false;C(!exit_adapter_hook(&a));if(neg==0){C(a.stop);continue;}
  if(neg==1){at(m,0xc00f7110,ARM_MODE_SVC|ARM_CPSR_T);}else{C(arm_step(&m->cpu)==ARM_OK);if(neg==2)m->cpu.cycles++;if(neg==3)wv(a.proc+0x128,0x4700);}
  C(!exit_adapter_hook(&a));C(a.stop&&strcmp(a.reason,"observed-exit-status-store"));
 }
 fixture(m);C(exit_adapter_init(&a,m,f));a.events=8192;at(m,0xc00fd790,ARM_MODE_SVC|ARM_CPSR_T);m->cpu.r[0]=a.proc;C(!exit_adapter_hook(&a));C(a.stop&&!strcmp(a.reason,"event-cap"));C(!external_reads);
 printf("PASS %u adapter ordering/identity/error/real-BLX/real-errno-LDR/real-status-STR checks; synthetic only\n",checks);fclose(f);free(cpu_copy);free(m);free(before);free(ram);return 0;}
