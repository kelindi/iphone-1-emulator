/*
 * S5LBox -- firmware-parameterized host-backed memory-disk bridge.
 *
 * A platform may replace two audited Thumb SVC sites inside a guest memory-
 * disk strategy routine and install md_bridge_handle_svc() on arm_bus_t.  The
 * portable core contains no firmware addresses or patch bytes: all such
 * values, along with the synthetic media aperture and physical RAM geometry,
 * are explicit configuration supplied by the boot frontend.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_MD_BRIDGE_H
#define S5LBOX_MD_BRIDGE_H

#include "arm.h"
#include "vm_block.h"

#include <stdbool.h>
#include <stdint.h>

#define MD_BRIDGE_PAGE_SIZE UINT32_C(4096)
#define MD_BRIDGE_MAX_TRANSFER MD_BRIDGE_PAGE_SIZE

typedef enum {
    MD_BRIDGE_DIRECTION_NONE = 0,
    MD_BRIDGE_DIRECTION_READ,
    MD_BRIDGE_DIRECTION_WRITE
} md_bridge_direction_t;

typedef struct {
    /*
     * Address of the first halfword of the replaced 32-bit Thumb BL.  The
     * frontend must prove LR is dead after this exact call site: the bridge
     * deliberately preserves every CPU register and cannot reproduce the
     * private LR clobbers inside the bypassed callee.  Do not generalize a
     * configured site to other callers merely because they call bcopy_phys.
     */
    uint32_t pc;
    /* Exact zero-extended Thumb SVC halfword installed at pc. */
    uint32_t encoding;
} md_bridge_site_t;

/*
 * Every address range is half-open.  token_base identifies the synthetic
 * physical address corresponding to block offset zero; media_size must equal
 * block->size exactly.  Both ranges must be non-empty and disjoint. token_base
 * itself must be a 32-bit published address, but the token range may cross
 * 4 GiB: the audited mdevstrategy path supplies bcopy_phys with split 64-bit
 * arguments. The RAM range and every guest-RAM operand remain wholly within
 * the guest's 32-bit physical address space (an exclusive end of 0x100000000
 * is valid). All four range values are 4 KiB aligned because the guest
 * publishes this geometry in pages.
 * `ram` points at an array of at least ram_size bytes whose first byte
 * corresponds to ram_base; it must not overlap the bridge object or backend
 * transfer storage.
 */
typedef struct {
    md_bridge_site_t read_site;
    md_bridge_site_t write_site;
    uint64_t token_base;
    uint64_t media_size;
    uint64_t ram_base;
    uint64_t ram_size;
    uint8_t *ram;
    const vm_block_t *block;
    vm_block_cancel_fn cancelled;
    void *cancel_context;
} md_bridge_config_t;

typedef struct {
    uint64_t successful_reads;
    uint64_t successful_writes;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t failures;
} md_bridge_stats_t;

/* Stable, machine-readable reasons for a recognized service failure. */
typedef enum {
    MD_BRIDGE_ERROR_NONE = 0,
    MD_BRIDGE_ERROR_NULL_CPU,
    MD_BRIDGE_ERROR_INVALID_MODE,
    MD_BRIDGE_ERROR_INVALID_CONFIG,
    MD_BRIDGE_ERROR_MISSING_BUS_ACCESS,
    MD_BRIDGE_ERROR_STACK_ALIGNMENT,
    MD_BRIDGE_ERROR_STACK_PAGE,
    MD_BRIDGE_ERROR_STACK_TRANSLATION,
    MD_BRIDGE_ERROR_STACK_RANGE,
    MD_BRIDGE_ERROR_ADDRESS_HIGH, /* guest-RAM operand has a nonzero high word */
    MD_BRIDGE_ERROR_LENGTH,
    MD_BRIDGE_ERROR_TOKEN_RANGE,
    MD_BRIDGE_ERROR_GUEST_RANGE,
    MD_BRIDGE_ERROR_TOKEN_PAGE,
    MD_BRIDGE_ERROR_GUEST_PAGE,
    MD_BRIDGE_ERROR_BLOCK_IO
} md_bridge_error_code_t;

/*
 * A failure snapshot contains only integer facts and a vm_block status; it is
 * safe to log after the backend returns and does not borrow guest pointers.
 * Fields that were not reached remain zero.  `transferred` is especially
 * important for a failed write: the backend may already contain that prefix
 * and the bridge cannot roll external storage back.
 */
typedef struct {
    md_bridge_error_code_t code;
    md_bridge_direction_t direction;
    uint32_t pc;
    uint32_t encoding;
    uint32_t stack_va;
    uint32_t stack_pa;
    uint32_t mmu_status;
    uint32_t length;
    uint64_t source;
    uint64_t destination;
    uint64_t media_offset;
    uint64_t transferred;
    vm_block_status_t block_status;
} md_bridge_error_t;

/*
 * This object is intentionally self-contained: no allocation, FILE, errno,
 * locks, or platform types enter emucore.  Initialize it before installing the
 * handler.  Configuration is copied so a caller cannot accidentally retarget
 * a live VM by modifying its stack-local config structure; the referenced
 * RAM, block, and callback contexts must remain alive and stable.  Calls for
 * one bridge must be serialized because its staging buffer and diagnostics
 * are mutable; the normal single-CPU machine loop provides that property.
 */
typedef struct {
    md_bridge_config_t config;
    md_bridge_stats_t stats;
    /* Most recent recognized failure; success and UNHANDLED do not erase it. */
    md_bridge_error_t last_error;
    uint8_t scratch[MD_BRIDGE_MAX_TRANSFER];
} md_bridge_t;

/*
 * Validate the complete static geometry before beginning a long guest run.
 * The handler repeats this check at every recognized service so later host
 * mutation of borrowed configuration cannot turn into unchecked I/O.
 */
bool md_bridge_config_valid(const md_bridge_config_t *config);

void md_bridge_init(md_bridge_t *bridge, const md_bridge_config_t *config);

/*
 * Suitable directly as arm_privileged_svc_handler_t.  Success, ERROR, and
 * UNHANDLED preserve every CPU register, including r14.  This is valid only
 * for individually audited sites satisfying the LR-dead invariant above.
 */
arm_svc_result_t md_bridge_handle_svc(void *context, arm_cpu_t *cpu,
                                      uint32_t pc, uint32_t encoding);

const char *md_bridge_error_string(md_bridge_error_code_t code);

#endif /* S5LBOX_MD_BRIDGE_H */
