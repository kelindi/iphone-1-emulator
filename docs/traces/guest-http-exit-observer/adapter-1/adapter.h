#ifndef EXIT_ADAPTER_H
#define EXIT_ADAPTER_H
#include "soc.h"
#include <stdio.h>
typedef struct {
 s5l8900_t*m;FILE*log;bool stop,bound,owner_executed;char reason[96];uint32_t proc,pid,task,thread;arm_cp15_t owner;
 unsigned phase,call_index,events;uint32_t target,return_pc,args[4],sys_errno,err_pointer,err_value,err_target;
 uint64_t call_cycle,entry_cycle,sys_cycle,return_cycle,read_cycle;
 bool sys_failed,status_pending,exit_entered;uint32_t exit_status,status_before;uint64_t status_cycle;
} exit_adapter;
bool exit_adapter_init(exit_adapter*,s5l8900_t*,FILE*);
bool exit_adapter_hook(void*);
/* Read-only sampling after a normal run boundary; no hook claims fake yield. */
void exit_adapter_sample(exit_adapter*);
#endif
