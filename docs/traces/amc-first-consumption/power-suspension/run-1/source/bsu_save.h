#ifndef ISOLATED_BSU_SAVE_H
#define ISOLATED_BSU_SAVE_H
#include "amc_engine.h"
/* One current-resource save only. Not a restore registry or portable snapshot. */
typedef struct {
 bool owned;unsigned phase;
 uint32_t words[7];
 const amc_engine *identity;
 amc_engine engine;
 amc_idle_t idle;
 amc_sram_t sram;
 uint8_t queue[AMC_QUEUE_BYTES];
} bsu_save;
bool bsu_save_valid(const bsu_save *,const amc_engine *);
bool bsu_save_read(bsu_save *,const amc_engine *,uint32_t,unsigned,uint32_t *);
#endif
