/*
 * iPhone OS 3.1.3 (7E18) kernel gate for s5l_bringup().
 *
 * Adapted from S5LBox tools/ios3_bringup_gate.h. This tree has no packet or
 * extended-geometry bridge yet, so the gate applies only the five base sites:
 * the RTC wait, the BSD root choice, the memory-disk read/write handoffs and
 * the raw-device watcher. The iPhone1,1 (M68) and iPhone1,2 (N82) 7E18
 * kernels are both accepted.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef IPHONE1_IOS3_BRINGUP_GATE_H
#define IPHONE1_IOS3_BRINGUP_GATE_H

#include "bringup.h"
#include "ios3_kernel_patch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ios3_kernel_patch_status_t status;
    ios3_kernel_patch_report_t report;
    bool ran;
} ios3_bringup_gate_report_t;

/* An s5l_bringup_kernel_gate_fn. `context` may be NULL or point at an
 * ios3_bringup_gate_report_t. Returns false without changing the kernel file
 * or guest RAM on any rejection. */
bool ios3_bringup_gate(void *context,
                       const uint8_t *kernel_file,
                       size_t kernel_file_size,
                       uint8_t *ram,
                       size_t ram_size,
                       uint64_t ram_base,
                       uint32_t virt_base,
                       char *detail,
                       size_t detail_capacity);

/* Set `request`'s gate hook and the 7E18 SVC site addresses together, so the
 * bridges are never armed against a kernel patched somewhere else. */
void ios3_bringup_gate_configure(s5l_bringup_request_t *request,
                                 ios3_bringup_gate_report_t *gate_report);

#ifdef __cplusplus
}
#endif

#endif /* IPHONE1_IOS3_BRINGUP_GATE_H */
