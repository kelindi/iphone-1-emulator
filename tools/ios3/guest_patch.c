/*
 * S5LBox -- atomic, firmware-neutral guest-RAM patch manifests.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "guest_patch.h"

#include <stdbool.h>

#define GUEST_ADDRESS_SPACE_SIZE UINT64_C(0x100000000)

typedef struct {
    uint64_t virtual_address;
    uint64_t physical_address;
    size_t ram_offset;
    uint32_t length;
    uint8_t expected[GUEST_PATCH_MAX_BYTES];
    uint8_t replacement[GUEST_PATCH_MAX_BYTES];
} guest_patch_plan_entry_t;

static void report_reset(guest_patch_report_t *report) {
    *report = (guest_patch_report_t){
        .status = GUEST_PATCH_STATUS_OK,
        .entry_index = GUEST_PATCH_NO_INDEX,
        .byte_index = GUEST_PATCH_NO_INDEX,
        .virtual_address = GUEST_PATCH_NO_ADDRESS,
        .physical_address = GUEST_PATCH_NO_ADDRESS,
        .expected = 0u,
        .actual = 0u
    };
}

static guest_patch_status_t report_failure(guest_patch_report_t *report,
                                           guest_patch_status_t status,
                                           uint32_t entry_index,
                                           uint32_t byte_index,
                                           uint64_t virtual_address,
                                           uint64_t physical_address,
                                           uint8_t expected,
                                           uint8_t actual) {
    *report = (guest_patch_report_t){
        .status = status,
        .entry_index = entry_index,
        .byte_index = byte_index,
        .virtual_address = virtual_address,
        .physical_address = physical_address,
        .expected = expected,
        .actual = actual
    };
    return status;
}

static bool entry_changes_bytes(const guest_patch_entry_t *entry) {
    uint32_t byte_index;

    for (byte_index = 0u; byte_index < entry->length; byte_index++) {
        if (entry->expected[byte_index] != entry->replacement[byte_index])
            return true;
    }
    return false;
}

guest_patch_status_t guest_patch_apply(const guest_patch_manifest_t *manifest,
                                       guest_patch_report_t *report) {
    guest_patch_manifest_t saved_manifest;
    guest_patch_plan_entry_t plan[GUEST_PATCH_MAX_ENTRIES];
    uint64_t ram_end;
    size_t entry_index;

    if (report == NULL)
        return GUEST_PATCH_STATUS_INVALID_ARGUMENT;

    if (manifest == NULL) {
        report_reset(report);
        return report_failure(report, GUEST_PATCH_STATUS_INVALID_ARGUMENT,
                              GUEST_PATCH_NO_INDEX, GUEST_PATCH_NO_INDEX,
                              GUEST_PATCH_NO_ADDRESS, GUEST_PATCH_NO_ADDRESS,
                              0u, 0u);
    }

    /*
     * The manifest object is explicitly allowed to live inside the RAM being
     * patched.  Snapshot every scalar and pointer before resetting the report
     * or writing a destination; later loops must never observe a self-modified
     * entry_count, RAM pointer, aperture, or entry-list pointer.
     */
    saved_manifest = *manifest;
    manifest = &saved_manifest;
    report_reset(report);

    if (manifest->ram == NULL || manifest->entries == NULL)
        return report_failure(report, GUEST_PATCH_STATUS_INVALID_ARGUMENT,
                              GUEST_PATCH_NO_INDEX, GUEST_PATCH_NO_INDEX,
                              GUEST_PATCH_NO_ADDRESS, GUEST_PATCH_NO_ADDRESS,
                              0u, 0u);

    if (manifest->ram_size == 0u ||
        manifest->ram_base >= GUEST_ADDRESS_SPACE_SIZE ||
        (uint64_t)manifest->ram_size >
            GUEST_ADDRESS_SPACE_SIZE - manifest->ram_base) {
        return report_failure(report, GUEST_PATCH_STATUS_INVALID_GEOMETRY,
                              GUEST_PATCH_NO_INDEX, GUEST_PATCH_NO_INDEX,
                              GUEST_PATCH_NO_ADDRESS, manifest->ram_base,
                              0u, 0u);
    }
    ram_end = manifest->ram_base + (uint64_t)manifest->ram_size;

    if (manifest->entry_count == 0u ||
        manifest->entry_count > (size_t)GUEST_PATCH_MAX_ENTRIES) {
        return report_failure(report, GUEST_PATCH_STATUS_INVALID_COUNT,
                              GUEST_PATCH_NO_INDEX, GUEST_PATCH_NO_INDEX,
                              GUEST_PATCH_NO_ADDRESS, GUEST_PATCH_NO_ADDRESS,
                              0u, 0u);
    }

    /*
     * Build a complete private plan before touching RAM.  Besides keeping the
     * operation atomic on validation failures, this prevents a valid patch
     * whose destination aliases its manifest from changing later entries.
     */
    for (entry_index = 0u; entry_index < manifest->entry_count; entry_index++) {
        const guest_patch_entry_t *entry = &manifest->entries[entry_index];
        uint64_t va = entry->virtual_address;
        uint64_t va_end;
        uint64_t pa;
        uint64_t pa_end;
        uint32_t byte_index;

        if (entry->length == 0u || entry->length > GUEST_PATCH_MAX_BYTES) {
            return report_failure(report, GUEST_PATCH_STATUS_INVALID_LENGTH,
                                  (uint32_t)entry_index, GUEST_PATCH_NO_INDEX,
                                  va, GUEST_PATCH_NO_ADDRESS, 0u, 0u);
        }

        if (va < manifest->virt_base) {
            return report_failure(report, GUEST_PATCH_STATUS_VA_BELOW_BASE,
                                  (uint32_t)entry_index, 0u, va,
                                  GUEST_PATCH_NO_ADDRESS,
                                  entry->expected[0], 0u);
        }

        va_end = va + entry->length;
        if (va_end > GUEST_ADDRESS_SPACE_SIZE) {
            uint32_t failed_byte = (uint32_t)(GUEST_ADDRESS_SPACE_SIZE - va);
            uint64_t failed_va = va + failed_byte;
            uint64_t failed_pa = manifest->ram_base +
                                 (failed_va - manifest->virt_base);
            return report_failure(report, GUEST_PATCH_STATUS_VA_OVERFLOW,
                                  (uint32_t)entry_index, failed_byte,
                                  failed_va, failed_pa,
                                  entry->expected[failed_byte], 0u);
        }

        pa = manifest->ram_base + (va - manifest->virt_base);
        pa_end = pa + entry->length;
        if (pa >= GUEST_ADDRESS_SPACE_SIZE ||
            pa_end > GUEST_ADDRESS_SPACE_SIZE) {
            uint32_t failed_byte = pa >= GUEST_ADDRESS_SPACE_SIZE
                ? 0u : (uint32_t)(GUEST_ADDRESS_SPACE_SIZE - pa);
            return report_failure(report, GUEST_PATCH_STATUS_PA_OVERFLOW,
                                  (uint32_t)entry_index, failed_byte,
                                  va + failed_byte, pa + failed_byte,
                                  entry->expected[failed_byte], 0u);
        }

        if (pa < manifest->ram_base || pa_end > ram_end) {
            uint32_t failed_byte = pa >= ram_end
                ? 0u : (uint32_t)(ram_end - pa);
            return report_failure(report, GUEST_PATCH_STATUS_OUT_OF_RAM,
                                  (uint32_t)entry_index, failed_byte,
                                  va + failed_byte, pa + failed_byte,
                                  entry->expected[failed_byte], 0u);
        }

        if (!entry_changes_bytes(entry)) {
            return report_failure(report, GUEST_PATCH_STATUS_NO_CHANGE,
                                  (uint32_t)entry_index, 0u, va, pa,
                                  entry->expected[0], entry->replacement[0]);
        }

        plan[entry_index].virtual_address = va;
        plan[entry_index].physical_address = pa;
        plan[entry_index].ram_offset = (size_t)(pa - manifest->ram_base);
        plan[entry_index].length = entry->length;
        for (byte_index = 0u; byte_index < entry->length; byte_index++) {
            plan[entry_index].expected[byte_index] =
                entry->expected[byte_index];
            plan[entry_index].replacement[byte_index] =
                entry->replacement[byte_index];
        }
    }

    /* Pairwise validation is bounded at 64 * 63 / 2 comparisons. */
    for (entry_index = 0u; entry_index < manifest->entry_count; entry_index++) {
        size_t previous_index;
        uint64_t start = plan[entry_index].virtual_address;
        uint64_t end = start + plan[entry_index].length;

        for (previous_index = 0u; previous_index < entry_index;
             previous_index++) {
            uint64_t previous_start = plan[previous_index].virtual_address;
            uint64_t previous_end = previous_start +
                                    plan[previous_index].length;

            if (start < previous_end && previous_start < end) {
                uint64_t overlap = start > previous_start
                    ? start : previous_start;
                uint32_t byte_index = (uint32_t)(overlap - start);
                return report_failure(report, GUEST_PATCH_STATUS_OVERLAP,
                                      (uint32_t)entry_index, byte_index,
                                      overlap,
                                      plan[entry_index].physical_address +
                                          byte_index,
                                      0u, 0u);
            }
        }
    }

    /* Every expected byte in every entry is checked before the first write. */
    for (entry_index = 0u; entry_index < manifest->entry_count; entry_index++) {
        uint32_t byte_index;

        for (byte_index = 0u; byte_index < plan[entry_index].length;
             byte_index++) {
            uint8_t actual = manifest->ram[plan[entry_index].ram_offset +
                                           byte_index];
            uint8_t expected = plan[entry_index].expected[byte_index];
            if (actual != expected) {
                return report_failure(
                    report, GUEST_PATCH_STATUS_EXPECTED_MISMATCH,
                    (uint32_t)entry_index, byte_index,
                    plan[entry_index].virtual_address + byte_index,
                    plan[entry_index].physical_address + byte_index,
                    expected, actual);
            }
        }
    }

    for (entry_index = 0u; entry_index < manifest->entry_count; entry_index++) {
        uint32_t byte_index;
        for (byte_index = 0u; byte_index < plan[entry_index].length;
             byte_index++) {
            manifest->ram[plan[entry_index].ram_offset + byte_index] =
                plan[entry_index].replacement[byte_index];
        }
    }

    for (entry_index = 0u; entry_index < manifest->entry_count; entry_index++) {
        volatile const uint8_t *verify_ram = manifest->ram;
        uint32_t byte_index;

        for (byte_index = 0u; byte_index < plan[entry_index].length;
             byte_index++) {
            uint8_t actual = verify_ram[plan[entry_index].ram_offset +
                                        byte_index];
            uint8_t expected = plan[entry_index].replacement[byte_index];
            if (actual != expected) {
                size_t rollback_entry;
                for (rollback_entry = 0u;
                     rollback_entry < manifest->entry_count;
                     rollback_entry++) {
                    uint32_t rollback_byte;
                    for (rollback_byte = 0u;
                         rollback_byte < plan[rollback_entry].length;
                         rollback_byte++) {
                        manifest->ram[plan[rollback_entry].ram_offset +
                                      rollback_byte] =
                            plan[rollback_entry].expected[rollback_byte];
                    }
                }
                return report_failure(
                    report, GUEST_PATCH_STATUS_VERIFY_MISMATCH,
                    (uint32_t)entry_index, byte_index,
                    plan[entry_index].virtual_address + byte_index,
                    plan[entry_index].physical_address + byte_index,
                    expected, actual);
            }
        }
    }

    return GUEST_PATCH_STATUS_OK;
}

const char *guest_patch_status_string(guest_patch_status_t status) {
    switch (status) {
    case GUEST_PATCH_STATUS_OK: return "ok";
    case GUEST_PATCH_STATUS_INVALID_ARGUMENT: return "invalid argument";
    case GUEST_PATCH_STATUS_INVALID_GEOMETRY: return "invalid geometry";
    case GUEST_PATCH_STATUS_INVALID_COUNT: return "invalid entry count";
    case GUEST_PATCH_STATUS_INVALID_LENGTH: return "invalid entry length";
    case GUEST_PATCH_STATUS_NO_CHANGE: return "expected and replacement match";
    case GUEST_PATCH_STATUS_VA_BELOW_BASE: return "virtual address below base";
    case GUEST_PATCH_STATUS_VA_OVERFLOW: return "virtual address overflow";
    case GUEST_PATCH_STATUS_PA_OVERFLOW: return "physical address overflow";
    case GUEST_PATCH_STATUS_OUT_OF_RAM: return "patch outside RAM";
    case GUEST_PATCH_STATUS_OVERLAP: return "patch entries overlap";
    case GUEST_PATCH_STATUS_EXPECTED_MISMATCH: return "expected byte mismatch";
    case GUEST_PATCH_STATUS_VERIFY_MISMATCH: return "replacement verify mismatch";
    default: return "unknown guest patch status";
    }
}
