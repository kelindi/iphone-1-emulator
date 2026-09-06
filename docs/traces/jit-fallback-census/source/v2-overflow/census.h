#ifndef FALLBACK_CENSUS_H
#define FALLBACK_CENSUS_H
#include <stdint.h>
/* Diagnostic only. No CPU pointer, MMU callback, guest memory, or allocator. */
enum { FC_NONE, FC_DISABLED, FC_NO_REGION, FC_NO_BIT, FC_RUNTIME_GUARD,
       FC_GENERATED_ZERO, FC_GENERATED_PREFIX, FC_STALE_ZERO, FC_MISMATCH };
enum { FC_OUTSIDE_BASE, FC_OTHER_MODE, FC_NO_ADMISSION, FC_INACTIVE, FC_ADMITTED };
enum { FC_FETCH=1, FC_FIQ=2, FC_IRQ=4, FC_PREFETCH=8, FC_DATA_ABORT=16 };
typedef struct { uint32_t pc, opcode, flags, provenance, guard, admission, status, used; uint64_t count; } fc_row;
typedef struct { uint64_t calls, ok, fetched, nofetch, nonok, overflow, map_overflow, epoch, installs_during_capture; uint32_t rows, enabled; } fc_totals;
void fc_reset(void);
void fc_start(void);
void fc_stop(void);
void fc_install(uint32_t pc, unsigned thumb, const uint32_t *mask);
void fc_stale(uint32_t pc, unsigned thumb);
void fc_attempt(unsigned reason, uint32_t next_pc, unsigned thumb, unsigned guard);
void fc_begin(uint32_t pc, unsigned thumb);
void fc_event(unsigned flag);
void fc_fetch(uint32_t opcode);
void fc_end(unsigned status, unsigned ok);
const fc_totals *fc_summary(void);
const fc_row *fc_rows(void);
unsigned fc_capacity(void);
#endif
