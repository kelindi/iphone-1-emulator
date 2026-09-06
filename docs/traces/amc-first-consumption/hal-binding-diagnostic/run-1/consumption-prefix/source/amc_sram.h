#ifndef ISOLATED_AMC_SRAM_H
#define ISOLATED_AMC_SRAM_H
#include <stdbool.h>
#include <stdint.h>
#define AMC_SRAM_BASE UINT32_C(0x22000000)
#define AMC_SRAM_SIZE UINT32_C(0x0002c000)
/* Isolated byte storage only. No reset/status, engine, or IRQ side effects. */
typedef struct { uint8_t bytes[AMC_SRAM_SIZE]; } amc_sram_t;
/* Deterministic zero initialization is an explicit unmeasured power-on policy.
 * AMC reset and power-control writes do NOT call this function. */
void amc_sram_init(amc_sram_t *s);
bool amc_sram_ram_conflict(uint32_t base, uint32_t size);
bool amc_sram_read(const amc_sram_t *s, uint32_t offset,
                   unsigned width, uint32_t *value);
bool amc_sram_write(amc_sram_t *s, uint32_t offset,
                    unsigned width, uint32_t value);
#endif
