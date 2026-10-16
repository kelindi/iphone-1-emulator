/*
 * S5LBox -- fail-closed iPhone OS 3.1.3 (7E18) kernel compatibility gate.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "ios3_kernel_patch.h"
#include "sha256.h"

#include <string.h>

#define GUEST_ADDRESS_SPACE_SIZE UINT64_C(0x100000000)

const uint8_t ios3_kernel_patch_expected_sha256[
    IOS3_KERNEL_PATCH_SHA256_LENGTH] = {
        0x0du, 0x8cu, 0xdbu, 0x33u, 0x9du, 0x37u, 0xcfu, 0x37u,
        0xa1u, 0xdbu, 0x26u, 0x38u, 0xffu, 0xf7u, 0x92u, 0x72u,
        0xecu, 0xd6u, 0x3au, 0x17u, 0x76u, 0x4bu, 0xf7u, 0x66u,
        0x6eu, 0xfau, 0x16u, 0x18u, 0x72u, 0x5du, 0xf7u, 0x0cu
    };

/*
 * iPhone1,1 (M68) 7E18 kernelcache. It shares the UUID, segment layout, entry
 * point and every patch-site byte with the iPhone1,2 build above, but its
 * bytes differ elsewhere, so it is accepted as a second exact identity.
 */
const uint8_t ios3_kernel_patch_expected_sha256_m68[
    IOS3_KERNEL_PATCH_SHA256_LENGTH] = {
        0xf3u, 0x6au, 0x88u, 0xd6u, 0x11u, 0xd3u, 0xb9u, 0x06u,
        0xaeu, 0x85u, 0x8fu, 0x37u, 0x7eu, 0x21u, 0x85u, 0x3bu,
        0x40u, 0xb2u, 0x14u, 0xb2u, 0xbeu, 0xa9u, 0x9cu, 0xb2u,
        0xf9u, 0x88u, 0xe3u, 0x80u, 0x69u, 0x8eu, 0x6cu, 0xe9u
    };

const uint8_t ios3_kernel_patch_expected_uuid[
    IOS3_KERNEL_PATCH_UUID_LENGTH] = {
        0x7fu, 0x87u, 0xddu, 0x4bu, 0xdcu, 0x3du, 0xf5u, 0x22u,
        0xa8u, 0x43u, 0x07u, 0x57u, 0x08u, 0x59u, 0x35u, 0x7eu
    };

/* Keep this order synchronized with ios3_kernel_patch_site_t. */
static const guest_patch_entry_t kernel_patches[] = {
    {
        .virtual_address = IOS3_KERNEL_PATCH_IORTC_VA,
        .length = 2u,
        .expected = {0x1eu, 0x23u},
        .replacement = {0x00u, 0x23u}
    },
    {
        .virtual_address = IOS3_KERNEL_PATCH_BSD_ROOT_VA,
        .length = 2u,
        .expected = {0x00u, 0x23u},
        .replacement = {0x01u, 0x23u}
    },
    {
        .virtual_address = IOS3_KERNEL_PATCH_MD_READ_VA,
        .length = 4u,
        .expected = {0xefu, 0xf7u, 0x12u, 0xf9u},
        .replacement = {0xe1u, 0xdfu, 0xc0u, 0x46u}
    },
    {
        .virtual_address = IOS3_KERNEL_PATCH_MD_WRITE_VA,
        .length = 4u,
        .expected = {0xefu, 0xf7u, 0xbfu, 0xf8u},
        .replacement = {0xe2u, 0xdfu, 0xc0u, 0x46u}
    },
    {
        .virtual_address = IOS3_KERNEL_PATCH_RAW_WATCHER_VA,
        .length = 4u,
        .expected = {0xf0u, 0xb5u, 0x46u, 0x46u},
        /*
         * SVC #0xe3 starts the host-backed raw transfer. SVC #0xe4 is the
         * exact in-text completion trampoline used after native _uiomove64
         * has resolved any user-page fault and returned.
         */
        .replacement = {0xe3u, 0xdfu, 0xe4u, 0xdfu}
    },
    {
        /* After FCS removal and chain detachment, before pkthdr_setlen.
         * Unoffloaded frames execute the original sub r1,r6,#2 in the bridge. */
        .virtual_address = IOS3_KERNEL_PATCH_PACKET_RX_VA,
        .length = 4u,
        .expected = {0x02u, 0x10u, 0x46u, 0xe2u},
        .replacement = {0xf0u, 0x00u, 0x00u, 0xefu}
    },
    {
        /* pppserial_lk_output's first mbuf_type call. The original blx r6 is
         * retained for every control packet and whenever offload is detached. */
        .virtual_address = IOS3_KERNEL_PATCH_PACKET_TX_VA,
        .length = 4u,
        .expected = {0x36u, 0xffu, 0x2fu, 0xe1u},
        .replacement = {0xf1u, 0x00u, 0x00u, 0xefu}
    },
    {
        /* Native mbuf_getpacket has replenished the receive buffer. Bounded
         * batches reuse native allocation and enqueue without serial bytes. */
        .virtual_address = IOS3_KERNEL_PATCH_PACKET_BATCH_VA,
        .length = 4u,
        .expected = {0x5eu, 0x01u, 0x00u, 0xeau},
        .replacement = {0xf2u, 0x00u, 0x00u, 0xefu}
    },
    {
        .virtual_address = IOS3_KERNEL_PATCH_MD_REGISTER_VA,
        .length = 2u, .expected = {0x12u, 0x0bu},
        .replacement = {0xe5u, 0xdfu}
    },
    {
        .virtual_address = IOS3_KERNEL_PATCH_MD_BOUNDS_VA,
        .length = 2u, .expected = {0xabu, 0x68u},
        .replacement = {0xe6u, 0xdfu}
    },
    {
        /* Preserve the high offset word already calculated by __muldi3. */
        .virtual_address = IOS3_KERNEL_PATCH_MD_HIGH_VA,
        .length = 2u, .expected = {0x00u, 0x23u},
        .replacement = {0x05u, 0x9bu} /* ldr r3,[sp,#0x14] */
    },
    {
        .virtual_address = IOS3_KERNEL_PATCH_MD_COUNT64_VA,
        .length = 2u, .expected = {0x00u, 0x03u},
        .replacement = {0xe7u, 0xdfu}
    },
    {
        .virtual_address = IOS3_KERNEL_PATCH_MD_COUNT32_VA,
        .length = 2u, .expected = {0x00u, 0x03u},
        .replacement = {0xe8u, 0xdfu}
    }
};

/*
 * The raw bridge redirects guest control to this unmodified helper. Pinning
 * its first two Thumb instructions makes a mistyped target constant fail
 * during the same exact-build preflight as the patched sites.
 */
static const uint8_t uiomove_prologue[] = {
    0xf0u, 0xb5u, 0x5eu, 0x46u
};

/*
 * Sites are ordered base, packet, extended geometry. Without a packet bridge
 * the three pppserial handoffs stay unpatched: an SVC there with no host
 * handler would fault the kernel the first time PPP carried a frame.
 */
static size_t patch_site_count(const ios3_kernel_patch_request_t *request) {
    if (!request->packet_bridge)
        return IOS3_KERNEL_PATCH_SITE_PACKET_RX;
    return request->extended_md_geometry
        ? sizeof kernel_patches / sizeof kernel_patches[0]
        : IOS3_KERNEL_PATCH_SITE_MD_REGISTER;
}

typedef struct {
    char name[17];
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
} expected_segment_t;

static const expected_segment_t expected_segments[] = {
    {"__TEXT",          0xc0008000u, 0x00205000u, 0x00000000u, 0x00205000u},
    {"__DATA",          0xc020d000u, 0x00053000u, 0x00205000u, 0x00018000u},
    {"__HIB",           0xc0000000u, 0x00005000u, 0x0021d000u, 0x00005000u},
    {"__KLD",           0xc0260000u, 0x00001000u, 0x00222000u, 0x00001000u},
    {"__LAST",          0xc0261000u, 0x00000000u, 0x00223000u, 0x00000000u},
    {"__PRELINK_TEXT",  0xc02cd000u, 0x004c8000u, 0x0028f000u, 0x004c8000u},
    {"__PRELINK_STATE", 0xc0261000u, 0x00000000u, 0x00223000u, 0x00000000u},
    {"__PRELINK_INFO",  0xc0795000u, 0x0003c000u, 0x00757000u, 0x0003c000u},
    {"__LINKEDIT",      0xc0261000u, 0x0006b5a4u, 0x00223000u, 0x0006b5a4u}
};

bool ios3_kernel_patch_sha256(
        const uint8_t *data,
        size_t length,
        uint8_t digest[IOS3_KERNEL_PATCH_SHA256_LENGTH]) {
    return ios3_sha256(data, length, digest);
}

typedef struct {
    uintptr_t begin;
    uintptr_t end;
    bool empty;
} host_range_t;

static bool make_host_range(const void *pointer,
                            size_t size,
                            host_range_t *range) {
    uintptr_t begin;

    if (pointer == NULL || size == 0u) {
        *range = (host_range_t){.empty = true};
        return true;
    }
    begin = (uintptr_t)pointer;
    if (size > UINTPTR_MAX - begin)
        return false;
    *range = (host_range_t){
        .begin = begin,
        .end = begin + size,
        .empty = false
    };
    return true;
}

static bool host_ranges_overlap(const host_range_t *left,
                                const host_range_t *right) {
    return !left->empty && !right->empty &&
           left->begin < right->end && right->begin < left->end;
}

static void report_reset(ios3_kernel_patch_report_t *report) {
    *report = (ios3_kernel_patch_report_t){
        .status = IOS3_KERNEL_PATCH_STATUS_OK,
        .site = IOS3_KERNEL_PATCH_NO_SITE,
        .segment_index = IOS3_KERNEL_PATCH_NO_SEGMENT,
        .byte_index = IOS3_KERNEL_PATCH_NO_BYTE,
        .virtual_address = IOS3_KERNEL_PATCH_NO_ADDRESS,
        .physical_address = IOS3_KERNEL_PATCH_NO_ADDRESS,
        .expected_value = 0u,
        .actual_value = 0u,
        .macho_status = MACHO_OK,
        .guest_patch_status = GUEST_PATCH_STATUS_OK
    };
}

static ios3_kernel_patch_status_t report_failure(
        ios3_kernel_patch_report_t *report,
        ios3_kernel_patch_status_t status,
        uint32_t site,
        uint32_t segment_index,
        uint32_t byte_index,
        uint64_t virtual_address,
        uint64_t physical_address,
        uint64_t expected_value,
        uint64_t actual_value,
        macho_status_t macho_status,
        guest_patch_status_t guest_patch_status) {
    *report = (ios3_kernel_patch_report_t){
        .status = status,
        .site = site,
        .segment_index = segment_index,
        .byte_index = byte_index,
        .virtual_address = virtual_address,
        .physical_address = physical_address,
        .expected_value = expected_value,
        .actual_value = actual_value,
        .macho_status = macho_status,
        .guest_patch_status = guest_patch_status
    };
    return status;
}

static ios3_kernel_patch_status_t simple_failure(
        ios3_kernel_patch_report_t *report,
        ios3_kernel_patch_status_t status,
        uint64_t expected_value,
        uint64_t actual_value) {
    return report_failure(report, status, IOS3_KERNEL_PATCH_NO_SITE,
                          IOS3_KERNEL_PATCH_NO_SEGMENT,
                          IOS3_KERNEL_PATCH_NO_BYTE,
                          IOS3_KERNEL_PATCH_NO_ADDRESS,
                          IOS3_KERNEL_PATCH_NO_ADDRESS,
                          expected_value, actual_value, MACHO_OK,
                          GUEST_PATCH_STATUS_OK);
}

static ios3_kernel_patch_status_t segment_failure(
        ios3_kernel_patch_report_t *report,
        ios3_kernel_patch_status_t status,
        uint32_t segment_index,
        uint32_t byte_index,
        uint64_t expected_value,
        uint64_t actual_value) {
    return report_failure(report, status, IOS3_KERNEL_PATCH_NO_SITE,
                          segment_index, byte_index,
                          IOS3_KERNEL_PATCH_NO_ADDRESS,
                          IOS3_KERNEL_PATCH_NO_ADDRESS,
                          expected_value, actual_value, MACHO_OK,
                          GUEST_PATCH_STATUS_OK);
}

static size_t ram_offset_for_va(uint32_t virtual_address) {
    return (size_t)(virtual_address - IOS3_KERNEL_PATCH_VIRT_BASE);
}

static ios3_kernel_patch_status_t validate_topology(
        const macho_t *macho,
        ios3_kernel_patch_report_t *report) {
    size_t segment_index;

    if (macho->segment_count !=
        sizeof expected_segments / sizeof expected_segments[0]) {
        return simple_failure(
            report, IOS3_KERNEL_PATCH_STATUS_SEGMENT_COUNT_MISMATCH,
            sizeof expected_segments / sizeof expected_segments[0],
            macho->segment_count);
    }

    for (segment_index = 0u;
         segment_index < sizeof expected_segments / sizeof expected_segments[0];
         segment_index++) {
        const expected_segment_t *expected = &expected_segments[segment_index];
        const macho_segment_t *actual = &macho->segments[segment_index];
        size_t name_index;

        if (strcmp(actual->name, expected->name) != 0) {
            for (name_index = 0u; name_index < sizeof actual->name;
                 name_index++) {
                uint8_t expected_byte = (uint8_t)expected->name[name_index];
                uint8_t actual_byte = (uint8_t)actual->name[name_index];
                if (expected_byte != actual_byte) {
                    return segment_failure(
                        report, IOS3_KERNEL_PATCH_STATUS_SEGMENT_NAME_MISMATCH,
                        (uint32_t)segment_index, (uint32_t)name_index,
                        expected_byte, actual_byte);
                }
                if (expected_byte == 0u)
                    break;
            }
            return segment_failure(
                report, IOS3_KERNEL_PATCH_STATUS_SEGMENT_NAME_MISMATCH,
                (uint32_t)segment_index, IOS3_KERNEL_PATCH_NO_BYTE, 0u, 0u);
        }
        if (actual->vmaddr != expected->vmaddr) {
            return segment_failure(
                report, IOS3_KERNEL_PATCH_STATUS_SEGMENT_VMADDR_MISMATCH,
                (uint32_t)segment_index, IOS3_KERNEL_PATCH_NO_BYTE,
                expected->vmaddr, actual->vmaddr);
        }
        if (actual->vmsize != expected->vmsize) {
            return segment_failure(
                report, IOS3_KERNEL_PATCH_STATUS_SEGMENT_VMSIZE_MISMATCH,
                (uint32_t)segment_index, IOS3_KERNEL_PATCH_NO_BYTE,
                expected->vmsize, actual->vmsize);
        }
        if (actual->fileoff != expected->fileoff) {
            return segment_failure(
                report, IOS3_KERNEL_PATCH_STATUS_SEGMENT_FILEOFF_MISMATCH,
                (uint32_t)segment_index, IOS3_KERNEL_PATCH_NO_BYTE,
                expected->fileoff, actual->fileoff);
        }
        if (actual->filesize != expected->filesize) {
            return segment_failure(
                report, IOS3_KERNEL_PATCH_STATUS_SEGMENT_FILESIZE_MISMATCH,
                (uint32_t)segment_index, IOS3_KERNEL_PATCH_NO_BYTE,
                expected->filesize, actual->filesize);
        }
    }

    if (macho->vm_low != IOS3_KERNEL_PATCH_VIRT_BASE) {
        return segment_failure(
            report, IOS3_KERNEL_PATCH_STATUS_VM_RANGE_MISMATCH,
            IOS3_KERNEL_PATCH_NO_SEGMENT, 0u,
            IOS3_KERNEL_PATCH_VIRT_BASE, macho->vm_low);
    }
    if (macho->vm_high != UINT32_C(0xc07d1000)) {
        return segment_failure(
            report, IOS3_KERNEL_PATCH_STATUS_VM_RANGE_MISMATCH,
            IOS3_KERNEL_PATCH_NO_SEGMENT, 1u,
            UINT32_C(0xc07d1000), macho->vm_high);
    }
    if (!macho->has_entry) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_ENTRYPOINT_MISSING,
                              1u, 0u);
    }
    if (macho->entry != UINT32_C(0xc0069040)) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_ENTRYPOINT_MISMATCH,
                              UINT32_C(0xc0069040), macho->entry);
    }
    if (macho->entry_sp != 0u) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_ENTRY_STACK_MISMATCH,
                              0u, macho->entry_sp);
    }
    if (!macho->has_symtab) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISSING,
                              1u, 0u);
    }
    if (macho->symoff != UINT32_C(0x00223000)) {
        return segment_failure(
            report, IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISMATCH,
            IOS3_KERNEL_PATCH_NO_SEGMENT, 0u,
            UINT32_C(0x00223000), macho->symoff);
    }
    if (macho->nsyms != UINT32_C(11431)) {
        return segment_failure(
            report, IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISMATCH,
            IOS3_KERNEL_PATCH_NO_SEGMENT, 1u,
            UINT32_C(11431), macho->nsyms);
    }
    if (macho->stroff != UINT32_C(0x002447d4)) {
        return segment_failure(
            report, IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISMATCH,
            IOS3_KERNEL_PATCH_NO_SEGMENT, 2u,
            UINT32_C(0x002447d4), macho->stroff);
    }
    if (macho->strsize != UINT32_C(0x00049dd0)) {
        return segment_failure(
            report, IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISMATCH,
            IOS3_KERNEL_PATCH_NO_SEGMENT, 3u,
            UINT32_C(0x00049dd0), macho->strsize);
    }
    return IOS3_KERNEL_PATCH_STATUS_OK;
}

ios3_kernel_patch_status_t ios3_kernel_patch_apply(
        const ios3_kernel_patch_request_t *request,
        ios3_kernel_patch_report_t *report) {
    ios3_kernel_patch_request_t saved_request;
    host_range_t report_range;
    host_range_t request_range;
    host_range_t kernel_range;
    host_range_t ram_range;
    macho_t macho;
    macho_status_t macho_status;
    uint8_t digest[IOS3_KERNEL_PATCH_SHA256_LENGTH];
    guest_patch_manifest_t manifest;
    guest_patch_report_t patch_report;
    guest_patch_status_t patch_status;
    ios3_kernel_patch_status_t status;
    size_t byte_index;

    if (report == NULL)
        return IOS3_KERNEL_PATCH_STATUS_INVALID_ARGUMENT;
    if (request == NULL) {
        report_reset(report);
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_INVALID_ARGUMENT,
                              0u, 0u);
    }

    /* Copy before overlap detection: this is a read only. If report aliases the
     * request, the copy lets us reject without performing the first write. */
    saved_request = *request;
    if (!make_host_range(report, sizeof *report, &report_range) ||
        !make_host_range(request, sizeof *request, &request_range) ||
        !make_host_range(saved_request.kernel_file,
                         saved_request.kernel_file_size, &kernel_range) ||
        !make_host_range(saved_request.ram,
                         saved_request.ram_size, &ram_range)) {
        return IOS3_KERNEL_PATCH_STATUS_HOST_RANGE_OVERFLOW;
    }
    if (host_ranges_overlap(&report_range, &request_range) ||
        host_ranges_overlap(&report_range, &kernel_range) ||
        host_ranges_overlap(&report_range, &ram_range)) {
        return IOS3_KERNEL_PATCH_STATUS_REPORT_OVERLAP;
    }
    if (saved_request.extended_md_geometry && !saved_request.packet_bridge) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_INVALID_ARGUMENT,
                              0u, 0u);
    }

    /* From here on, report output is proven disjoint from every input. */
    report_reset(report);
    if (saved_request.kernel_file == NULL || saved_request.ram == NULL) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_INVALID_ARGUMENT,
                              0u, 0u);
    }
    if (host_ranges_overlap(&request_range, &kernel_range)) {
        return simple_failure(
            report, IOS3_KERNEL_PATCH_STATUS_REQUEST_KERNEL_OVERLAP, 0u, 0u);
    }
    if (host_ranges_overlap(&request_range, &ram_range)) {
        return simple_failure(
            report, IOS3_KERNEL_PATCH_STATUS_REQUEST_RAM_OVERLAP, 0u, 0u);
    }
    if (host_ranges_overlap(&kernel_range, &ram_range)) {
        return simple_failure(
            report, IOS3_KERNEL_PATCH_STATUS_KERNEL_RAM_OVERLAP, 0u, 0u);
    }

    if (saved_request.kernel_file_size !=
        (size_t)IOS3_KERNEL_PATCH_FILE_SIZE) {
        return simple_failure(
            report, IOS3_KERNEL_PATCH_STATUS_KERNEL_SIZE_MISMATCH,
            IOS3_KERNEL_PATCH_FILE_SIZE,
            (uint64_t)saved_request.kernel_file_size);
    }

    macho_status = macho_parse(saved_request.kernel_file,
                               saved_request.kernel_file_size, &macho);
    if (macho_status != MACHO_OK) {
        if (macho_status == MACHO_ERR_NOT_ARM) {
            return report_failure(
                report, IOS3_KERNEL_PATCH_STATUS_CPU_TYPE_MISMATCH,
                IOS3_KERNEL_PATCH_NO_SITE, IOS3_KERNEL_PATCH_NO_SEGMENT,
                IOS3_KERNEL_PATCH_NO_BYTE, IOS3_KERNEL_PATCH_NO_ADDRESS,
                IOS3_KERNEL_PATCH_NO_ADDRESS, MH_CPU_TYPE_ARM, macho.cputype,
                macho_status, GUEST_PATCH_STATUS_OK);
        }
        return report_failure(
            report, IOS3_KERNEL_PATCH_STATUS_MACHO_PARSE_FAILED,
            IOS3_KERNEL_PATCH_NO_SITE, IOS3_KERNEL_PATCH_NO_SEGMENT,
            IOS3_KERNEL_PATCH_NO_BYTE, IOS3_KERNEL_PATCH_NO_ADDRESS,
            IOS3_KERNEL_PATCH_NO_ADDRESS, MACHO_OK, macho_status,
            macho_status, GUEST_PATCH_STATUS_OK);
    }
    if (macho.cputype != MH_CPU_TYPE_ARM) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_CPU_TYPE_MISMATCH,
                              MH_CPU_TYPE_ARM, macho.cputype);
    }
    if (macho.cpusubtype != MH_CPU_SUBTYPE_V6) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_CPU_SUBTYPE_MISMATCH,
                              MH_CPU_SUBTYPE_V6, macho.cpusubtype);
    }
    if (macho.filetype != MH_EXECUTE) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_FILE_TYPE_MISMATCH,
                              MH_EXECUTE, macho.filetype);
    }
    if (!macho.has_uuid) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_UUID_MISSING,
                              1u, 0u);
    }
    for (byte_index = 0u; byte_index < IOS3_KERNEL_PATCH_UUID_LENGTH;
         byte_index++) {
        if (macho.uuid[byte_index] !=
            ios3_kernel_patch_expected_uuid[byte_index]) {
            return report_failure(
                report, IOS3_KERNEL_PATCH_STATUS_UUID_MISMATCH,
                IOS3_KERNEL_PATCH_NO_SITE, IOS3_KERNEL_PATCH_NO_SEGMENT,
                (uint32_t)byte_index, IOS3_KERNEL_PATCH_NO_ADDRESS,
                IOS3_KERNEL_PATCH_NO_ADDRESS,
                ios3_kernel_patch_expected_uuid[byte_index],
                macho.uuid[byte_index], MACHO_OK, GUEST_PATCH_STATUS_OK);
        }
    }
    status = validate_topology(&macho, report);
    if (status != IOS3_KERNEL_PATCH_STATUS_OK)
        return status;

    if (saved_request.virt_base != IOS3_KERNEL_PATCH_VIRT_BASE) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_VIRT_BASE_MISMATCH,
                              IOS3_KERNEL_PATCH_VIRT_BASE,
                              saved_request.virt_base);
    }
    if (saved_request.ram_base != IOS3_KERNEL_PATCH_RAM_BASE) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_RAM_BASE_MISMATCH,
                              IOS3_KERNEL_PATCH_RAM_BASE,
                              saved_request.ram_base);
    }
    if (saved_request.ram_size == 0u ||
        saved_request.ram_size > (size_t)UINT32_MAX ||
        (uint64_t)saved_request.ram_size >
            GUEST_ADDRESS_SPACE_SIZE - saved_request.ram_base) {
        return simple_failure(
            report, IOS3_KERNEL_PATCH_STATUS_INVALID_RAM_GEOMETRY,
            GUEST_ADDRESS_SPACE_SIZE - saved_request.ram_base,
            (uint64_t)saved_request.ram_size);
    }
    if (saved_request.ram_size < IOS3_KERNEL_PATCH_MIN_RAM_SIZE) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_RAM_TOO_SMALL,
                              IOS3_KERNEL_PATCH_MIN_RAM_SIZE,
                              (uint64_t)saved_request.ram_size);
    }

    for (byte_index = 0u; byte_index < sizeof uiomove_prologue;
         byte_index++) {
        size_t ram_offset =
            ram_offset_for_va(IOS3_KERNEL_UIOMOVE_VA) + byte_index;
        uint8_t actual = saved_request.ram[ram_offset];
        if (actual != uiomove_prologue[byte_index]) {
            return report_failure(
                report, IOS3_KERNEL_PATCH_STATUS_UIOMOVE_MISMATCH,
                IOS3_KERNEL_PATCH_NO_SITE, IOS3_KERNEL_PATCH_NO_SEGMENT,
                (uint32_t)byte_index,
                (uint64_t)IOS3_KERNEL_UIOMOVE_VA + byte_index,
                saved_request.ram_base + ram_offset,
                uiomove_prologue[byte_index], actual, MACHO_OK,
                GUEST_PATCH_STATUS_OK);
        }
    }

    {
        size_t site_index;
        for (site_index = 0u; site_index < patch_site_count(&saved_request);
             site_index++) {
            const guest_patch_entry_t *entry = &kernel_patches[site_index];
            uint32_t site_byte;
            size_t ram_offset = ram_offset_for_va(entry->virtual_address);

            for (site_byte = 0u; site_byte < entry->length; site_byte++) {
                uint8_t actual = saved_request.ram[ram_offset + site_byte];
                if (actual != entry->expected[site_byte]) {
                    bool raw_entry =
                        site_index == IOS3_KERNEL_PATCH_SITE_RAW_WATCHER;
                    return report_failure(
                        report,
                        raw_entry
                            ? IOS3_KERNEL_PATCH_STATUS_RAW_WATCHER_MISMATCH
                            : IOS3_KERNEL_PATCH_STATUS_PATCH_TRANSACTION_FAILED,
                        (uint32_t)site_index,
                        IOS3_KERNEL_PATCH_NO_SEGMENT, site_byte,
                        (uint64_t)entry->virtual_address + site_byte,
                        saved_request.ram_base + ram_offset + site_byte,
                        entry->expected[site_byte], actual, MACHO_OK,
                        raw_entry ? GUEST_PATCH_STATUS_OK
                                  : GUEST_PATCH_STATUS_EXPECTED_MISMATCH);
                }
            }
        }
    }

    /* Bind the measured file to what the CPU will execute. Checking only the
     * handful of patch sites would allow unrelated loaded bytes to drift while
     * the immutable source still hashes correctly. Validate both file-backed
     * bytes and the loader-created zero-fill tail. Exact topology above bounds
     * every offset by the kernel file and required RAM aperture. */
    {
        size_t segment_index;
        for (segment_index = 0u;
             segment_index <
                 sizeof expected_segments / sizeof expected_segments[0];
             segment_index++) {
            const expected_segment_t *segment =
                &expected_segments[segment_index];
            size_t ram_offset =
                (size_t)(segment->vmaddr - IOS3_KERNEL_PATCH_VIRT_BASE);
            uint32_t segment_byte;

            for (segment_byte = 0u; segment_byte < segment->vmsize;
                 segment_byte++) {
                uint8_t expected = segment_byte < segment->filesize
                    ? saved_request.kernel_file[
                        (size_t)segment->fileoff + segment_byte]
                    : 0u;
                uint8_t actual = saved_request.ram[
                    ram_offset + segment_byte];
                if (actual != expected) {
                    uint64_t virtual_address =
                        (uint64_t)segment->vmaddr + segment_byte;
                    uint64_t physical_address = saved_request.ram_base +
                        ram_offset + segment_byte;
                    return report_failure(
                        report,
                        IOS3_KERNEL_PATCH_STATUS_LOADED_SEGMENT_MISMATCH,
                        IOS3_KERNEL_PATCH_NO_SITE, (uint32_t)segment_index,
                        segment_byte, virtual_address, physical_address,
                        expected, actual, MACHO_OK, GUEST_PATCH_STATUS_OK);
                }
            }
        }
    }

    if (!ios3_kernel_patch_sha256(saved_request.kernel_file,
                                  saved_request.kernel_file_size, digest)) {
        return simple_failure(report,
                              IOS3_KERNEL_PATCH_STATUS_INVALID_ARGUMENT,
                              0u, 0u);
    }
    if (memcmp(digest, ios3_kernel_patch_expected_sha256_m68,
               IOS3_KERNEL_PATCH_SHA256_LENGTH) == 0) {
        byte_index = IOS3_KERNEL_PATCH_SHA256_LENGTH;
    } else for (byte_index = 0u; byte_index < IOS3_KERNEL_PATCH_SHA256_LENGTH;
         byte_index++) {
        if (digest[byte_index] !=
            ios3_kernel_patch_expected_sha256[byte_index]) {
            return report_failure(
                report, IOS3_KERNEL_PATCH_STATUS_KERNEL_DIGEST_MISMATCH,
                IOS3_KERNEL_PATCH_NO_SITE, IOS3_KERNEL_PATCH_NO_SEGMENT,
                (uint32_t)byte_index, IOS3_KERNEL_PATCH_NO_ADDRESS,
                IOS3_KERNEL_PATCH_NO_ADDRESS,
                ios3_kernel_patch_expected_sha256[byte_index],
                digest[byte_index], MACHO_OK, GUEST_PATCH_STATUS_OK);
        }
    }

    manifest = (guest_patch_manifest_t){
        .ram = saved_request.ram,
        .ram_size = saved_request.ram_size,
        .ram_base = saved_request.ram_base,
        .virt_base = saved_request.virt_base,
        .entries = kernel_patches,
        .entry_count = patch_site_count(&saved_request)
    };
    patch_status = guest_patch_apply(&manifest, &patch_report);
    if (patch_status != GUEST_PATCH_STATUS_OK) {
        uint32_t site = patch_report.entry_index <
                            (uint32_t)(sizeof kernel_patches /
                                       sizeof kernel_patches[0])
            ? patch_report.entry_index : IOS3_KERNEL_PATCH_NO_SITE;
        return report_failure(
            report, IOS3_KERNEL_PATCH_STATUS_PATCH_TRANSACTION_FAILED,
            site, IOS3_KERNEL_PATCH_NO_SEGMENT, patch_report.byte_index,
            patch_report.virtual_address, patch_report.physical_address,
            patch_report.expected, patch_report.actual, MACHO_OK,
            patch_status);
    }

    return IOS3_KERNEL_PATCH_STATUS_OK;
}

const char *ios3_kernel_patch_status_string(
        ios3_kernel_patch_status_t status) {
    switch (status) {
    case IOS3_KERNEL_PATCH_STATUS_OK: return "ok";
    case IOS3_KERNEL_PATCH_STATUS_INVALID_ARGUMENT: return "invalid argument";
    case IOS3_KERNEL_PATCH_STATUS_HOST_RANGE_OVERFLOW:
        return "host range overflow (report untouched)";
    case IOS3_KERNEL_PATCH_STATUS_REPORT_OVERLAP:
        return "report overlaps an input range (report untouched)";
    case IOS3_KERNEL_PATCH_STATUS_REQUEST_KERNEL_OVERLAP:
        return "request overlaps kernel file";
    case IOS3_KERNEL_PATCH_STATUS_REQUEST_RAM_OVERLAP:
        return "request overlaps guest RAM";
    case IOS3_KERNEL_PATCH_STATUS_KERNEL_RAM_OVERLAP:
        return "kernel file overlaps guest RAM";
    case IOS3_KERNEL_PATCH_STATUS_KERNEL_SIZE_MISMATCH:
        return "kernel file size mismatch";
    case IOS3_KERNEL_PATCH_STATUS_MACHO_PARSE_FAILED:
        return "kernel Mach-O parse failed";
    case IOS3_KERNEL_PATCH_STATUS_CPU_TYPE_MISMATCH:
        return "kernel CPU type mismatch";
    case IOS3_KERNEL_PATCH_STATUS_CPU_SUBTYPE_MISMATCH:
        return "kernel CPU subtype mismatch";
    case IOS3_KERNEL_PATCH_STATUS_FILE_TYPE_MISMATCH:
        return "kernel Mach-O file type mismatch";
    case IOS3_KERNEL_PATCH_STATUS_UUID_MISSING:
        return "kernel LC_UUID missing";
    case IOS3_KERNEL_PATCH_STATUS_UUID_MISMATCH:
        return "kernel LC_UUID mismatch";
    case IOS3_KERNEL_PATCH_STATUS_SEGMENT_COUNT_MISMATCH:
        return "kernel segment count mismatch";
    case IOS3_KERNEL_PATCH_STATUS_SEGMENT_NAME_MISMATCH:
        return "kernel segment name mismatch";
    case IOS3_KERNEL_PATCH_STATUS_SEGMENT_VMADDR_MISMATCH:
        return "kernel segment virtual address mismatch";
    case IOS3_KERNEL_PATCH_STATUS_SEGMENT_VMSIZE_MISMATCH:
        return "kernel segment virtual size mismatch";
    case IOS3_KERNEL_PATCH_STATUS_SEGMENT_FILEOFF_MISMATCH:
        return "kernel segment file offset mismatch";
    case IOS3_KERNEL_PATCH_STATUS_SEGMENT_FILESIZE_MISMATCH:
        return "kernel segment file size mismatch";
    case IOS3_KERNEL_PATCH_STATUS_VM_RANGE_MISMATCH:
        return "kernel virtual range mismatch";
    case IOS3_KERNEL_PATCH_STATUS_ENTRYPOINT_MISSING:
        return "kernel entrypoint missing";
    case IOS3_KERNEL_PATCH_STATUS_ENTRYPOINT_MISMATCH:
        return "kernel entrypoint mismatch";
    case IOS3_KERNEL_PATCH_STATUS_ENTRY_STACK_MISMATCH:
        return "kernel initial stack mismatch";
    case IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISSING:
        return "kernel symbol table missing";
    case IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISMATCH:
        return "kernel symbol table mismatch";
    case IOS3_KERNEL_PATCH_STATUS_KERNEL_DIGEST_MISMATCH:
        return "kernel SHA-256 mismatch";
    case IOS3_KERNEL_PATCH_STATUS_VIRT_BASE_MISMATCH:
        return "kernel virtual base mismatch";
    case IOS3_KERNEL_PATCH_STATUS_RAM_BASE_MISMATCH:
        return "guest RAM base mismatch";
    case IOS3_KERNEL_PATCH_STATUS_INVALID_RAM_GEOMETRY:
        return "invalid guest RAM geometry";
    case IOS3_KERNEL_PATCH_STATUS_RAM_TOO_SMALL:
        return "guest RAM aperture too small";
    case IOS3_KERNEL_PATCH_STATUS_LOADED_SEGMENT_MISMATCH:
        return "loaded kernel segment differs from kernel file";
    case IOS3_KERNEL_PATCH_STATUS_RAW_WATCHER_MISMATCH:
        return "raw mdev watcher byte mismatch";
    case IOS3_KERNEL_PATCH_STATUS_UIOMOVE_MISMATCH:
        return "raw mdev uiomove target mismatch";
    case IOS3_KERNEL_PATCH_STATUS_PATCH_TRANSACTION_FAILED:
        return "kernel patch transaction failed";
    default: return "unknown iOS 3 kernel patch status";
    }
}

const char *ios3_kernel_patch_site_string(uint32_t site) {
    switch (site) {
    case IOS3_KERNEL_PATCH_SITE_IORTC: return "IORTC";
    case IOS3_KERNEL_PATCH_SITE_BSD_ROOT: return "IOFindBSDRoot";
    case IOS3_KERNEL_PATCH_SITE_MD_READ: return "mdevstrategy read";
    case IOS3_KERNEL_PATCH_SITE_MD_WRITE: return "mdevstrategy write";
    case IOS3_KERNEL_PATCH_SITE_RAW_WATCHER: return "mdevrw raw watcher";
    case IOS3_KERNEL_PATCH_SITE_PACKET_RX: return "packet receive handoff";
    case IOS3_KERNEL_PATCH_SITE_PACKET_TX: return "packet transmit handoff";
    case IOS3_KERNEL_PATCH_SITE_PACKET_BATCH: return "packet receive batch";
    case IOS3_KERNEL_PATCH_SITE_MD_REGISTER: return "mdev capacity registration";
    case IOS3_KERNEL_PATCH_SITE_MD_BOUNDS: return "mdev 64-bit bounds";
    case IOS3_KERNEL_PATCH_SITE_MD_HIGH: return "mdev high offset";
    case IOS3_KERNEL_PATCH_SITE_MD_COUNT64: return "mdev block count 64";
    case IOS3_KERNEL_PATCH_SITE_MD_COUNT32: return "mdev block count 32";
    case IOS3_KERNEL_PATCH_NO_SITE: return "none";
    default: return "unknown kernel patch site";
    }
}
