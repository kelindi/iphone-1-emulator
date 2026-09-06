#ifndef AMC_REAL_PREFETCH_OBSERVER_H
#define AMC_REAL_PREFETCH_OBSERVER_H
#include "binding.h"
#include "retirement.h"
enum {FETCH_REFUSE=0,FETCH_ALLOW=1,FETCH_MAPPED=2};
typedef struct {uint64_t start,end;uint32_t pc,cpsr,ifsr,ifar,irq,fiq,sctlr,fetch_present,fetch_block,fetch_gen,tlb_gen,fetch_priv,after_pc,after_cpsr,after_ifsr,after_ifar,after_lr,after_spsr,kind,returned;} fetch_receipt;
typedef struct {unsigned count,faults;bool pending,mapped;binding_diagnostic initial;fetch_receipt records[8];} fetch_observer;
bool fetch_cache_hit(const arm_cpu_t*);
unsigned fetch_prepare(fetch_observer*,const arm_cpu_t*,const binding_diagnostic*);
bool fetch_finish(fetch_observer*,const arm_cpu_t*,const guest_step*,bool);
#endif
