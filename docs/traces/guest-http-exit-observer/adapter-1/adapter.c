#include "adapter.h"
#include "peek.h"
#include <string.h>
#include <stdarg.h>
typedef struct {const char*name;uint32_t pc,reg,slot,fileva,svc,errcall,errread,errreg;const uint8_t*own,*lib,*ret,*ec,*er,*ea;} callsite;
#include "code.h"
enum{IDLE,CALL,ENTERED,SYSTEM_ENTERED,SYSTEM_RETURNED,ERROR_WAIT,ERROR_CALL,ERROR_ENTERED,ERROR_POINTER,ERROR_READ};
static void fail(exit_adapter*a,const char*s){if(!a->stop){a->stop=true;snprintf(a->reason,sizeof a->reason,"%s",s);}}
static void event(exit_adapter*a,const char*kind,const char*fmt,...){if(a->stop)return;if(a->events>=8192){fail(a,"event-cap");return;}fprintf(a->log,"{\"sequence\":%u,\"kind\":\"%s\",\"cycles\":%llu,\"ticks\":%llu,\"pc\":%u",a->events++,kind,(unsigned long long)a->m->cpu.cycles,(unsigned long long)a->m->timer.ticks,a->m->cpu.r[15]);if(fmt&&*fmt){fputc(',',a->log);va_list v;va_start(v,fmt);vfprintf(a->log,fmt,v);va_end(v);}fputs("}\n",a->log);if(fflush(a->log)||ferror(a->log))fail(a,"trace-write-failure");}
static bool read_at(exit_adapter*a,bool kernel,uint32_t va,void*p,size_t n){arm_cpu_t view=a->m->cpu;if(kernel)view.cpsr=(view.cpsr&~31u)|ARM_MODE_SVC;return amc_peek_ram(&view,a->m->ram,a->m->ram_base,a->m->ram_size,va,p,n);}
static bool word(exit_adapter*a,bool k,uint32_t va,uint32_t*p){return !(va&3)&&read_at(a,k,va,p,4);}
static bool code(exit_adapter*a,bool k,uint32_t pc,const uint8_t*p,size_t n){uint8_t b[32];return n&&n<=sizeof b&&pc<=UINT32_MAX-n&&read_at(a,k,pc,b,n)&&!memcmp(b,p,n);}
static bool pointer(uint32_t p){return p>=0xc0000000u&&p<=0xfffff000u&&!(p&3);}
static bool process(exit_adapter*a,uint32_t p,uint32_t task){uint32_t pid,t;char name[16];return pointer(p)&&pointer(task)&&word(a,true,p+0x24,&pid)&&word(a,true,p+0x138,&t)&&read_at(a,true,p+0xeb,name,16)&&pid==a->pid&&t==task&&!memcmp(name,"iphone1-local-ht",16);}
static bool current(exit_adapter*a,uint32_t*thread,uint32_t*task,uint32_t*proc){uint32_t u,flags;return word(a,true,0xc01d7d34,thread)&&pointer(*thread)&&word(a,true,*thread+0x2fc,task)&&pointer(*task)&&word(a,true,*task+0x178,proc)&&pointer(*proc)&&word(a,true,*thread+0x3b4,&u)&&pointer(u)&&word(a,true,u+0xa4,&flags)&&(!(flags&(1u<<25))||*proc!=a->proc);}
static bool space(exit_adapter*a){arm_cp15_t*p=&a->m->cpu.cp15,*q=&a->owner;return p->sctlr==q->sctlr&&p->ttbr0==q->ttbr0&&p->ttbr1==q->ttbr1&&p->ttbcr==q->ttbcr&&p->dacr==q->dacr&&p->fcse_pid==q->fcse_pid&&p->context_id==q->context_id;}
static bool owner(exit_adapter*a,bool allow_bind){uint32_t th,ta,pr;if(!current(a,&th,&ta,&pr)){fail(a,"current-owner-unreadable-or-vfork");return false;}if(pr!=a->proc)return false;if(!process(a,pr,ta)||ta!=a->task){fail(a,"owner-pid-task-name-mismatch");return false;}if(!a->bound){if(!allow_bind)return false;a->thread=th;a->owner=a->m->cpu.cp15;a->bound=true;event(a,"owner-candidate", "\"proc\":%u,\"pid\":%u,\"task\":%u,\"thread\":%u",pr,a->pid,ta,th);for(unsigned i=0;i<sizeof a->owner/4;i++){uint32_t value;memcpy(&value,(const uint8_t*)&a->owner+i*4,4);event(a,"owner-cp15-word","\"offset\":%u,\"value\":%u",i*4,value);}}if(th!=a->thread||!space(a)){fail(a,"owner-address-space-changed");return false;}return true;}
static void refresh(exit_adapter*a){uint32_t list[16];unsigned n=0;const uint32_t fixed[]={0xc00f7094,0xc00fd790,0xc00fdf6c,0xc00f710e,0xc00f7110};for(unsigned i=0;i<5;i++)list[n++]=fixed[i];for(unsigned i=0;i<6;i++)list[n++]=calls[i].pc;
#define ADD(x) do{uint32_t v=(x)&~1u;bool seen=false;for(unsigned j=0;j<n;j++)if(list[j]==v)seen=true;if(!seen){if(n==16){fail(a,"hook-target-cap");return;}list[n++]=v;}}while(0)
 if(a->phase>=CALL&&a->phase<=SYSTEM_RETURNED){ADD(a->target);ADD(a->target+calls[a->call_index].svc);ADD(a->target+calls[a->call_index].svc+4);ADD(a->return_pc);}
 if(a->phase>=ERROR_WAIT){const callsite*c=&calls[a->call_index];ADD(c->errcall);ADD(c->errread);ADD(c->errread+4);if(a->phase>=ERROR_CALL)ADD(a->err_target);}
 if(!s5l8900_set_pre_step_hook(a->m,exit_adapter_hook,a,list,n))fail(a,"hook-admission-failed");
#undef ADD
}
static bool own_code(exit_adapter*a,const uint8_t*p){return (a->m->cpu.cpsr&63u)==ARM_MODE_USR&&code(a,false,a->m->cpu.r[15],p,32);}
static void kernel_event(exit_adapter*a){arm_cpu_t*c=&a->m->cpu;uint32_t pc=c->r[15];if((c->cpsr&63u)!=(ARM_MODE_SVC|ARM_CPSR_T))return;
 if(pc==0xc00f7094||pc==0xc00fd790||pc==0xc00fdf6c){if(c->r[0]!=a->proc)return;const uint8_t*p=pc==0xc00f7094?kernel_exit1:pc==0xc00fd790?kernel_psignal:kernel_sigexit;size_t n=pc==0xc00fd790?10:32;if(!code(a,true,pc,p,n)||!process(a,a->proc,a->task)){fail(a,"kernel-target-binding");return;}event(a,pc==0xc00f7094?"exit1-entry":pc==0xc00fd790?"signal-request-entry":"fatal-signal-entry","\"proc\":%u,\"argument\":%u,\"lr\":%u",c->r[0],c->r[1],c->r[14]);if(pc==0xc00f7094){a->exit_entered=true;a->exit_status=c->r[1];}}
 if(pc==0xc00f710e&&c->r[4]==a->proc){if(!a->exit_entered||c->r[3]!=0x128||c->r[2]!=a->exit_status||!code(a,true,pc,kernel_statuspre,2)||!process(a,a->proc,a->task)||!word(a,true,a->proc+0x128,&a->status_before)){fail(a,"status-store-before-binding");return;}a->status_pending=true;a->status_cycle=c->cycles;event(a,"status-store-before","\"old\":%u,\"new\":%u",a->status_before,a->exit_status);}
 if(pc==0xc00f7110&&a->status_pending){uint32_t actual;if(c->cycles!=a->status_cycle+1||c->r[4]!=a->proc||!code(a,true,pc,kernel_statuspost,4)||!word(a,true,a->proc+0x128,&actual)||actual!=a->exit_status){fail(a,"status-store-after-binding");return;}event(a,"status-store-committed","\"status\":%u,\"beforeCycle\":%llu",actual,(unsigned long long)a->status_cycle);a->status_pending=false;fail(a,"observed-exit-status-store");}
}
bool exit_adapter_hook(void*ctx){exit_adapter*a=ctx;if(a->stop)return false;arm_cpu_t*c=&a->m->cpu;uint32_t pc=c->r[15];if(pc>=0xc0000000u){kernel_event(a);return false;}
 if((c->cpsr&31u)!=ARM_MODE_USR)return false;
 /* Shared library PCs also execute in other processes while our syscall
  * sleeps. Ignore those events before applying the owned call state machine. */
 if(a->phase!=IDLE){uint32_t th,ta,pr;if(!current(a,&th,&ta,&pr)){fail(a,"pending-current-owner-unreadable");return false;}if(pr!=a->proc)return false;}
 const callsite*q=&calls[a->call_index];
 if(a->phase>=ERROR_WAIT){
  if(pc==q->errcall){if(!own_code(a,q->ec)||!owner(a,false)||a->phase!=ERROR_WAIT||!word(a,false,0x4028,&a->err_target)||(a->err_target&1u)!=1||c->r[0]!=a->err_target||!code(a,false,a->err_target&~1u,lib_error,32)){fail(a,"errno-call-binding");return false;}a->phase=ERROR_CALL;a->read_cycle=c->cycles;event(a,"errno-call","\"target\":%u",a->err_target);refresh(a);return false;}
  if(pc==(a->err_target&~1u)&&a->phase==ERROR_CALL){if((c->cpsr&63u)!=(ARM_MODE_USR|ARM_CPSR_T)||!owner(a,false)||c->r[14]!=q->errcall+4||c->cycles!=a->read_cycle+1||!code(a,false,pc,lib_error,32)){fail(a,"errno-target-binding");return false;}a->phase=ERROR_ENTERED;event(a,"errno-target",NULL);return false;}
  if(pc==q->errread){if(a->phase!=ERROR_ENTERED||!own_code(a,q->er)||!owner(a,false)||!word(a,false,c->r[0],&a->err_value)){fail(a,"errno-read-before-binding");return false;}a->err_pointer=c->r[0];a->read_cycle=c->cycles;a->phase=ERROR_READ;event(a,"errno-read-before","\"pointer\":%u,\"value\":%u",a->err_pointer,a->err_value);return false;}
  if(pc==q->errread+4&&a->phase==ERROR_READ){if(!own_code(a,q->ea)||!owner(a,false)||c->cycles!=a->read_cycle+1||c->r[q->errreg]!=a->err_value||a->err_value!=a->sys_errno){fail(a,"errno-read-after-binding");return false;}event(a,"errno-read-committed","\"errno\":%u,\"pointer\":%u",a->err_value,a->err_pointer);a->phase=IDLE;refresh(a);return false;}
 }
 if(a->phase>=CALL&&a->phase<=SYSTEM_RETURNED){
  if(pc==(a->target&~1u)&&a->phase==CALL){if((c->cpsr&63u)!=ARM_MODE_USR||!owner(a,false)||c->r[14]!=a->return_pc||c->cycles!=a->call_cycle+1||!code(a,false,pc,q->lib,32)){fail(a,"relocated-target-binding");return false;}a->phase=ENTERED;a->entry_cycle=c->cycles;if(!a->owner_executed){a->owner_executed=true;event(a,"owner-call-retired", "\"proc\":%u,\"thread\":%u",a->proc,a->thread);}event(a,"callee-entered","\"target\":%u,\"return\":%u",a->target,a->return_pc);return false;}
  if(pc==a->target+q->svc){uint32_t instruction;if(a->phase!=ENTERED||!owner(a,false)||(c->cpsr&63u)!=ARM_MODE_USR||!word(a,false,pc,&instruction)||instruction!=0xef000080u){fail(a,"svc-before-binding");return false;}a->phase=SYSTEM_ENTERED;a->sys_cycle=c->cycles;event(a,"syscall-before", "\"ip\":%u",c->r[12]);return false;}
  if(pc==a->target+q->svc+4){if(a->phase!=SYSTEM_ENTERED||!owner(a,false)||(c->cpsr&63u)!=ARM_MODE_USR||!code(a,false,a->target,q->lib,32)||c->cycles<=a->entry_cycle){fail(a,"syscall-return-binding");return false;}a->sys_failed=(c->cpsr&ARM_CPSR_C)!=0;a->sys_errno=a->sys_failed?c->r[0]:0;a->sys_cycle=c->cycles;a->phase=SYSTEM_RETURNED;event(a,"syscall-return","\"carry\":%u,\"r0\":%u,\"errnoOnlyIfCarry\":%u",a->sys_failed,c->r[0],a->sys_errno);return false;}
  if(pc==a->return_pc){if(a->phase!=SYSTEM_RETURNED||!own_code(a,q->ret)||!owner(a,false)||c->cycles<=a->sys_cycle||(a->sys_failed&&c->r[0]!=UINT32_MAX)){fail(a,"own-return-binding");return false;}event(a,"call-returned","\"call\":\"%s\",\"r0\":%u,\"syscallErrno\":%u",q->name,c->r[0],a->sys_errno);a->return_cycle=c->cycles;a->phase=a->sys_failed&&q->errcall?ERROR_WAIT:IDLE;refresh(a);return false;}
 }
 for(unsigned i=0;i<6;i++)if(pc==calls[i].pc){const callsite*s=&calls[i];if((c->cpsr&63u)!=ARM_MODE_USR)return false;
  uint32_t th,ta,pr;if(!current(a,&th,&ta,&pr)){fail(a,"call-current-owner-unreadable");return false;}if(pr!=a->proc)return false;
  if(!own_code(a,s->own)||!owner(a,true)||a->phase!=IDLE){fail(a,"own-call-order-or-binding");return false;}uint32_t target;if(!word(a,false,s->slot,&target)||target!=c->r[s->reg]||(target&3u)||!code(a,false,target,s->lib,32)){fail(a,"eager-import-target-binding");return false;}
  a->call_index=i;a->target=target;a->return_pc=pc+4;a->call_cycle=c->cycles;memcpy(a->args,c->r,16);a->phase=CALL;a->sys_failed=false;event(a,"call","\"name\":\"%s\",\"target\":%u,\"args\":[%u,%u,%u,%u]",s->name,target,c->r[0],c->r[1],c->r[2],c->r[3]);
  if(i==5){uint8_t bytes[1024];if(c->r[0]!=1||!c->r[2]||c->r[2]>1024||!read_at(a,false,c->r[1],bytes,c->r[2])){fail(a,"console-byte-bound");return false;}fprintf(a->log,"{\"kind\":\"console-attempt-bytes\",\"callCycle\":%llu,\"hex\":\"",(unsigned long long)a->call_cycle);for(unsigned k=0;k<c->r[2];k++)fprintf(a->log,"%02x",bytes[k]);fputs("\"}\n",a->log);}
  refresh(a);return false;
 }
 return false;
}
bool exit_adapter_init(exit_adapter*a,s5l8900_t*m,FILE*f){if(!a||!m||!f)return false;memset(a,0,sizeof*a);a->m=m;a->log=f;a->proc=0xcc2e5d98;a->pid=21;a->task=0xcc285620;
 uint32_t current_literal=0;if(!word(a,true,0xc00606ac,&current_literal)||current_literal!=0xc01d7d34||!process(a,a->proc,a->task)||!code(a,true,0xc0060028,kernel_setthread,12)||!code(a,true,0xc0026040,kernel_currenttask,10)||!code(a,true,0xc002b5ac,kernel_bsdtask,8)||!code(a,true,0xc002b5bc,kernel_bsdthread,8)){fail(a,"snapshot-owner-source-binding");return false;}refresh(a);return !a->stop;}
void exit_adapter_sample(exit_adapter*a){if(a->stop)return;if(a->bound&&a->phase!=IDLE&&a->m->cpu.cycles-a->call_cycle>500000000ull)fail(a,"pending-call-instruction-cap");}
