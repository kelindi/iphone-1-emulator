#ifndef ISOLATED_AMC_SUSPEND_H
#define ISOLATED_AMC_SUSPEND_H
#include "bsu_save.h"
typedef struct {
 unsigned phase;bool owned;
 uint32_t clock,power;
 amc_engine retained,vacant;
 amc_idle_t retained_idle,vacant_idle;
} amc_suspend;
typedef void (*amc_gate_writer)(void*,uint32_t,uint32_t);
bool amc_suspend_valid(const amc_suspend*,const bsu_save*,const amc_engine*);
bool amc_suspend_quiet(const amc_suspend*,const amc_engine*);
bool amc_suspend_write(amc_suspend*,bsu_save*,amc_engine*,uint32_t,uint32_t,uint32_t,unsigned,uint32_t,amc_gate_writer,void*);
void amc_suspend_destroy(amc_suspend*);
#endif
