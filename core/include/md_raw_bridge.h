/*
 * S5LBox -- bounded host-backed XNU32 raw memory-disk bridge.
 *
 * A frontend may replace one audited Thumb memory-disk character entry with
 * `svc #0xe3; svc #0xe4` and install md_raw_bridge_handle_svc() on the ARM
 * bus. The first SVC performs a bounded direct transfer when every user page
 * is resident. Otherwise it redirects through the guest's native uiomove so
 * XNU can resolve demand-zero and copy-on-write faults, then consumes the
 * second SVC as the exact completion continuation.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_MD_RAW_BRIDGE_H
#define S5LBOX_MD_RAW_BRIDGE_H

#include "arm.h"
#include "vm_block.h"

#include <stdbool.h>
#include <stdint.h>

#define MD_RAW_BRIDGE_PAGE_SIZE UINT32_C(4096)
#define MD_RAW_BRIDGE_PERMISSION_GRANULE UINT32_C(1024)
#define MD_RAW_BRIDGE_MAX_TRANSFER UINT32_C(131072)
#define MD_RAW_BRIDGE_MAX_IOVECS UINT32_C(1024)
#define MD_RAW_BRIDGE_MAX_BOUNCE_SLOTS UINT32_C(4)
#define MD_RAW_BRIDGE_MAX_METADATA_SPANS UINT32_C(16)
#define MD_RAW_BRIDGE_MAX_DATA_SPANS \
    (MD_RAW_BRIDGE_MAX_IOVECS * UINT32_C(2) + \
     MD_RAW_BRIDGE_MAX_TRANSFER / MD_RAW_BRIDGE_PERMISSION_GRANULE)

/* Exact 32-bit XNU uio layout implemented by this bridge. */
#define MD_RAW_BRIDGE_UIO_SIZE UINT32_C(0x28)
#define MD_RAW_BRIDGE_USER_IOV_SIZE UINT32_C(8)

typedef enum {
    MD_RAW_BRIDGE_DIRECTION_NONE = 0,
    MD_RAW_BRIDGE_DIRECTION_READ,
    MD_RAW_BRIDGE_DIRECTION_WRITE
} md_raw_bridge_direction_t;

typedef struct {
    /* First halfword of the replaced four-byte Thumb function prologue. */
    uint32_t pc;
    /* Exact zero-extended Thumb SVC halfword installed at pc. */
    uint32_t encoding;
} md_raw_bridge_site_t;

typedef struct {
    md_raw_bridge_site_t site;
    /* Must be site.pc + 2 and encode the exact Thumb `svc #0xe4`. */
    md_raw_bridge_site_t completion_site;
    /* Even fetch PC of the exact firmware's Thumb uiomove entry. */
    uint32_t uiomove_thumb_pc;
    /* Complete Darwin dev_t accepted by this backend (for 7E18 md0: 09000000). */
    uint32_t expected_device;
    /* Exclusive user VA ceiling enforced before unprivileged translation. */
    uint32_t user_address_limit;
    uint64_t media_size;
    uint64_t ram_base;
    uint64_t ram_size;
    uint8_t *ram;
    /*
     * Reserved, page-aligned physical guest-RAM slots used as native-uiomove
     * bounce storage. There must be 1..4 slots, bounce_stride must be at least
     * MD_RAW_BRIDGE_MAX_TRANSFER, and the complete reservation must be inside
     * the configured RAM aperture. The frontend owns this reservation and
     * must keep it unavailable to the guest allocator.
     */
    uint64_t bounce_base_pa;
    uint32_t bounce_stride;
    uint32_t bounce_slot_count;
    const vm_block_t *block;
    vm_block_cancel_fn cancelled;
    void *cancel_context;
} md_raw_bridge_config_t;

typedef struct {
    uint64_t successful_reads;
    uint64_t successful_writes;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t media_bytes_read;
    uint64_t media_bytes_written;
    uint64_t guard_bytes_read;
    uint64_t guard_bytes_written;
    uint64_t zero_length_requests;
    uint64_t redirected_requests;
    uint64_t redirected_completions;
    uint64_t partial_uiomove_errors;
    uint64_t guest_errors;
    uint64_t failures;
} md_raw_bridge_stats_t;

/* Stable reasons for either a returned guest errno or a fail-closed halt. */
typedef enum {
    MD_RAW_BRIDGE_ERROR_NONE = 0,
    MD_RAW_BRIDGE_ERROR_NULL_CPU,
    MD_RAW_BRIDGE_ERROR_INVALID_MODE,
    MD_RAW_BRIDGE_ERROR_INVALID_CONFIG,
    MD_RAW_BRIDGE_ERROR_MMU_DISABLED,
    MD_RAW_BRIDGE_ERROR_MISSING_BUS_ACCESS,
    MD_RAW_BRIDGE_ERROR_DEVICE,
    MD_RAW_BRIDGE_ERROR_UIO_ALIGNMENT,
    MD_RAW_BRIDGE_ERROR_UIO_TRANSLATION,
    MD_RAW_BRIDGE_ERROR_UIO_RANGE,
    MD_RAW_BRIDGE_ERROR_UIO_STATE,
    MD_RAW_BRIDGE_ERROR_SEGMENT,
    MD_RAW_BRIDGE_ERROR_DIRECTION,
    MD_RAW_BRIDGE_ERROR_RESIDUAL,
    MD_RAW_BRIDGE_ERROR_OFFSET,
    MD_RAW_BRIDGE_ERROR_MEDIA_RANGE,
    MD_RAW_BRIDGE_ERROR_IOV_COUNT,
    MD_RAW_BRIDGE_ERROR_IOV_ALIGNMENT,
    MD_RAW_BRIDGE_ERROR_IOV_TRANSLATION,
    MD_RAW_BRIDGE_ERROR_IOV_RANGE,
    MD_RAW_BRIDGE_ERROR_IOV_SUM,
    MD_RAW_BRIDGE_ERROR_USER_ADDRESS,
    MD_RAW_BRIDGE_ERROR_USER_TRANSLATION,
    MD_RAW_BRIDGE_ERROR_USER_RANGE,
    MD_RAW_BRIDGE_ERROR_METADATA_ALIAS,
    MD_RAW_BRIDGE_ERROR_BOUNCE_ALIAS,
    MD_RAW_BRIDGE_ERROR_PLAN_CAPACITY,
    MD_RAW_BRIDGE_ERROR_PENDING_COLLISION,
    MD_RAW_BRIDGE_ERROR_PENDING_EXHAUSTED,
    MD_RAW_BRIDGE_ERROR_STALE_COMPLETION,
    MD_RAW_BRIDGE_ERROR_MALFORMED_COMPLETION,
    MD_RAW_BRIDGE_ERROR_UIOMOVE,
    MD_RAW_BRIDGE_ERROR_BLOCK_IO
} md_raw_bridge_error_code_t;

typedef struct {
    md_raw_bridge_error_code_t code;
    md_raw_bridge_direction_t direction;
    uint32_t pc;
    uint32_t encoding;
    uint32_t device;
    uint32_t uio_va;
    uint32_t iov_va;
    uint32_t fault_va;
    uint32_t fault_pa;
    uint32_t mmu_status;
    uint32_t segment;
    uint32_t rw;
    int32_t iov_count;
    int32_t residual;
    int guest_errno;
    uint64_t media_offset;
    uint64_t transferred;
    vm_block_status_t block_status;
} md_raw_bridge_error_t;

typedef struct {
    uint32_t base_pa;
    uint32_t length_pa;
    uint32_t base;
    uint32_t length;
    /* Prefix consumed by this request; may be shorter than length. */
    uint32_t consumed;
} md_raw_bridge_iov_plan_t;

typedef struct {
    uint32_t pa;
    uint32_t length;
} md_raw_bridge_data_span_t;

typedef struct {
    uint32_t pa;
    uint32_t length;
} md_raw_bridge_pending_span_t;

typedef struct {
    bool active;
    uint32_t key_sp;
    uint32_t key_mode;
    uint32_t return_lr;
    uint32_t uio_va;
    uint32_t uio_iovs_pa;
    uint32_t uio_iovcnt_pa;
    uint32_t uio_offset_lo_pa;
    uint32_t uio_offset_hi_pa;
    uint32_t uio_segment_pa;
    uint32_t uio_rw_pa;
    uint32_t uio_resid_pa;
    uint32_t uio_span_count;
    uint32_t metadata_span_count;
    uint32_t original_segment;
    uint32_t physical_segment;
    uint32_t rw;
    int32_t original_residual;
    uint64_t original_offset;
    uint64_t bounce_pa;
    uint32_t media_length;
    md_raw_bridge_pending_span_t
        metadata_spans[MD_RAW_BRIDGE_MAX_METADATA_SPANS];
} md_raw_bridge_pending_t;

/*
 * No allocation or host FILE state enters emucore. The copied config borrows
 * RAM, vm_block, callback, and callback-context lifetimes from its owner. One
 * bridge is single-CPU. Its direct-path staging buffer and plans are reused per
 * call, while up to four native-uiomove continuations may remain pending in
 * disjoint guest bounce slots. A zero-initialized, host-resident 128 KiB
 * overlay preserves coherent reads and writes in the bounded allocation tail
 * without extending the disk image. The object is roughly 300 KiB; place it in
 * static or heap storage, never on a small iOS thread stack.
 *
 * A direct backend failure commits no uio metadata. A redirected WRITE first
 * lets native uiomove update the guest uio, then persists the completed media
 * prefix. A subsequent backend failure deliberately halts: guest-memory side
 * effects cannot be rolled back and continuing would silently corrupt media.
 */
typedef struct {
    md_raw_bridge_config_t config;
    md_raw_bridge_stats_t stats;
    /* Success does not erase the most recent diagnostic. */
    md_raw_bridge_error_t last_error;
    /* A later fatal bridge error does not erase guest-errno attribution. */
    md_raw_bridge_error_t last_guest_error;
    uint32_t iov_plan_count;
    uint32_t data_span_count;
    md_raw_bridge_iov_plan_t iov_plan[MD_RAW_BRIDGE_MAX_IOVECS];
    md_raw_bridge_data_span_t data_spans[MD_RAW_BRIDGE_MAX_DATA_SPANS];
    md_raw_bridge_pending_t pending[MD_RAW_BRIDGE_MAX_BOUNCE_SLOTS];
    uint8_t scratch[MD_RAW_BRIDGE_MAX_TRANSFER];
    uint8_t guard_tail[MD_RAW_BRIDGE_MAX_TRANSFER];
} md_raw_bridge_t;

bool md_raw_bridge_config_valid(const md_raw_bridge_config_t *config);
void md_raw_bridge_init(md_raw_bridge_t *bridge,
                        const md_raw_bridge_config_t *config);

/*
 * Suitable as an arm_privileged_svc_handler_t. Every recognized entry result
 * returns ARM_SVC_REDIRECTED: direct transfers and guest errnos redirect to the
 * saved LR because the next patched halfword is the completion SVC. Faultable
 * transfers redirect to native uiomove and later consume completion_site.
 */
arm_svc_result_t md_raw_bridge_handle_svc(void *context, arm_cpu_t *cpu,
                                          uint32_t pc, uint32_t encoding);

const char *md_raw_bridge_error_string(md_raw_bridge_error_code_t code);

#endif /* S5LBOX_MD_RAW_BRIDGE_H */
