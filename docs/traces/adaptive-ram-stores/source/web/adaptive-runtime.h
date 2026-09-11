#ifndef IPHONE1_WEB_ADAPTIVE_RUNTIME_H
#define IPHONE1_WEB_ADAPTIVE_RUNTIME_H
#include "arm_adaptive_dispatch.h"
#include "arm_adaptive_guard.h"

#define ADAPTIVE_UNIT_CAP 2048u
#define ADAPTIVE_WITNESS_CAP 8u
#define ADAPTIVE_BINARY_CAP (32u * 1024u * 1024u)
#define ADAPTIVE_RETIRED_CAP ADAPTIVE_UNIT_CAP

/* One synchronous CPU owner. Bind BEFORE reset/restore/free/replacement of RAM.
 * Function handles remain runtime-owned until returned by adaptive_retired;
 * callers must never remove a live or active handle themselves. A failed bind
 * disables execution but retains every handle; drain and bind again to recover.
 * Epoch exhaustion is permanent and must not be recovered by epoch reuse.
 */
void adaptive_bind(arm_cpu_t *cpu);
uint32_t adaptive_epoch(void);
/* Optional version tracking, configured once immediately after bind and before
 * capture/install. Host must attach adaptive_ram_written to every RAM writer.
 * Literal guards remain mandatory until the complete writer audit qualifies.
 * This table is derived state, freed by bind and never saved in snapshots. */
unsigned adaptive_track_ram(uint8_t *ram, uint32_t bytes);
void adaptive_ram_written(void *ctx, const uint8_t *host, uint32_t bytes);
uint32_t adaptive_code_version(const uint8_t *host);
unsigned adaptive_capture(arm_adaptive_fetch_witness_t *out);
unsigned adaptive_install(uint32_t entry_pc, unsigned thumb, arm_adaptive_fn fn,
    const arm_adaptive_fetch_witness_t *witnesses, unsigned count,
    uint32_t epoch, uint32_t binary_bytes);
unsigned adaptive_retired(arm_adaptive_fn *out, unsigned capacity);
void adaptive_enable(unsigned enabled);
unsigned adaptive_guard(uint32_t pc, uint32_t raw, unsigned width);
/* Active-unit load helper. Zero is a BEFORE refusal, not a guest fault. */
uintptr_t adaptive_read(uint32_t va, unsigned width, unsigned privileged);
/* 0 BEFORE refusal; 1 committed; 2 committed, mandatory BOUNDARY exit. */
unsigned adaptive_write(uint32_t va, unsigned width, unsigned privileged,
                         uint32_t value);
unsigned adaptive_failed(void);
unsigned adaptive_units(void);
#endif
