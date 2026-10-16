/*
 * S5LBox -- atomic, firmware-neutral guest-RAM patch manifests.
 *
 * The caller supplies version-specific addresses and bytes.  This module only
 * validates a small fixed manifest, maps 32-bit guest virtual addresses into
 * one explicit RAM aperture, and applies the manifest all-or-nothing.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_GUEST_PATCH_H
#define S5LBOX_GUEST_PATCH_H

#include <stddef.h>
#include <stdint.h>

#define GUEST_PATCH_MAX_ENTRIES UINT32_C(64)
#define GUEST_PATCH_MAX_BYTES UINT32_C(8)
#define GUEST_PATCH_NO_INDEX UINT32_MAX
#define GUEST_PATCH_NO_ADDRESS UINT64_MAX

typedef struct {
    uint32_t virtual_address;
    uint32_t length;
    uint8_t expected[GUEST_PATCH_MAX_BYTES];
    uint8_t replacement[GUEST_PATCH_MAX_BYTES];
} guest_patch_entry_t;

/*
 * `ram` points at the byte whose physical address is `ram_base`.  The physical
 * aperture must be nonempty and fit wholly in the 32-bit guest address space;
 * an exclusive end of 0x100000000 is valid.  A patch virtual address maps as:
 *
 *     physical = ram_base + (virtual - virt_base)
 *
 * The entries and their byte arrays are copied into a bounded local plan
 * before the first RAM store, so the manifest object and entry storage may
 * themselves alias `ram`. `report` is output storage and must not overlap the
 * manifest, entries, or RAM aperture. Calls must still be serialized with
 * guest execution.
 */
typedef struct {
    uint8_t *ram;
    size_t ram_size;
    uint64_t ram_base;
    uint32_t virt_base;
    const guest_patch_entry_t *entries;
    size_t entry_count;
} guest_patch_manifest_t;

typedef enum {
    GUEST_PATCH_STATUS_OK = 0,
    GUEST_PATCH_STATUS_INVALID_ARGUMENT,
    GUEST_PATCH_STATUS_INVALID_GEOMETRY,
    GUEST_PATCH_STATUS_INVALID_COUNT,
    GUEST_PATCH_STATUS_INVALID_LENGTH,
    GUEST_PATCH_STATUS_NO_CHANGE,
    GUEST_PATCH_STATUS_VA_BELOW_BASE,
    GUEST_PATCH_STATUS_VA_OVERFLOW,
    GUEST_PATCH_STATUS_PA_OVERFLOW,
    GUEST_PATCH_STATUS_OUT_OF_RAM,
    GUEST_PATCH_STATUS_OVERLAP,
    GUEST_PATCH_STATUS_EXPECTED_MISMATCH,
    GUEST_PATCH_STATUS_VERIFY_MISMATCH
} guest_patch_status_t;

/*
 * A report is fully reset on every call for which `report` is non-NULL.
 * entry_index/byte_index and the addresses identify the first deterministic
 * failure.  They use the *_NO_* sentinels when a failure is not entry- or
 * byte-specific.  virtual_address is 64-bit so VA-overflow reports can name
 * the first unrepresentable byte (0x100000000) exactly.  expected/actual are
 * meaningful for byte-comparison failures and zero otherwise.
 */
typedef struct {
    guest_patch_status_t status;
    uint32_t entry_index;
    uint32_t byte_index;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint8_t expected;
    uint8_t actual;
} guest_patch_report_t;

/*
 * Validate the complete manifest and every expected byte before writing any
 * replacement byte.  On the raw-memory interface below, stores cannot report
 * failure; replacements are nevertheless read back through volatile accesses.
 * A verification failure restores every entry's original expected bytes before
 * returning.  Both manifest and report are required.
 */
guest_patch_status_t guest_patch_apply(const guest_patch_manifest_t *manifest,
                                       guest_patch_report_t *report);

const char *guest_patch_status_string(guest_patch_status_t status);

#endif /* S5LBOX_GUEST_PATCH_H */
