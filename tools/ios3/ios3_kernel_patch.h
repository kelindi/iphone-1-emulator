/*
 * S5LBox -- fail-closed iPhone OS 3.1.3 (7E18) kernel compatibility gate.
 *
 * The compatibility patches in this interface are valid for one exact
 * decrypted kernel build. They are deliberately host policy rather than a
 * generic ARM-core feature: a different kernel fails before any guest byte is
 * changed.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_IOS3_KERNEL_PATCH_H
#define S5LBOX_IOS3_KERNEL_PATCH_H

#include "guest_patch.h"
#include "macho.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IOS3_KERNEL_PATCH_FILE_SIZE UINT64_C(7942144)
#define IOS3_KERNEL_PATCH_SHA256_LENGTH UINT32_C(32)
#define IOS3_KERNEL_PATCH_UUID_LENGTH UINT32_C(16)

#define IOS3_KERNEL_PATCH_VIRT_BASE UINT32_C(0xc0000000)
#define IOS3_KERNEL_PATCH_RAM_BASE UINT64_C(0x08000000)

#define IOS3_KERNEL_PATCH_RAW_WATCHER_VA UINT32_C(0xc0073f94)
#define IOS3_KERNEL_UIOMOVE_VA UINT32_C(0xc0128d14)
#define IOS3_KERNEL_PATCH_IORTC_VA UINT32_C(0xc0175b3e)
#define IOS3_KERNEL_PATCH_BSD_ROOT_VA UINT32_C(0xc01a1b5a)
#define IOS3_KERNEL_PATCH_MD_READ_VA UINT32_C(0xc0074140)
#define IOS3_KERNEL_PATCH_MD_WRITE_VA UINT32_C(0xc00741e6)
#define IOS3_KERNEL_PATCH_PACKET_RX_VA UINT32_C(0xc0593068)
#define IOS3_KERNEL_PATCH_PACKET_TX_VA UINT32_C(0xc0593af8)
#define IOS3_KERNEL_PATCH_PACKET_BATCH_VA UINT32_C(0xc05930b0)
#define IOS3_KERNEL_PATCH_MD_REGISTER_VA UINT32_C(0xc01a1b66)
#define IOS3_KERNEL_PATCH_MD_BOUNDS_VA UINT32_C(0xc0074088)
#define IOS3_KERNEL_PATCH_MD_HIGH_VA UINT32_C(0xc00740e2)
#define IOS3_KERNEL_PATCH_MD_COUNT64_VA UINT32_C(0xc0073eb4)
#define IOS3_KERNEL_PATCH_MD_COUNT32_VA UINT32_C(0xc0073f22)
#define IOS3_KERNEL_MBUF_FREE_VA UINT32_C(0xc012d374)
#define IOS3_KERNEL_PACKET_TX_DONE_VA UINT32_C(0xc0593bc0)
#define IOS3_KERNEL_PACKET_RX_DROP_VA UINT32_C(0xc05930a8)
#define IOS3_KERNEL_PACKET_RX_ENQUEUE_VA UINT32_C(0xc0593034)
#define IOS3_KERNEL_PACKET_RX_UNLOCK_VA UINT32_C(0xc0593630)

/* Smallest RAM aperture, starting at IOS3_KERNEL_PATCH_RAM_BASE, that contains
 * every byte of every file-backed segment in the supported kernel. Real boots
 * use the full 128 MiB aperture. */
#define IOS3_KERNEL_PATCH_MIN_RAM_SIZE ((size_t)UINT32_C(0x007d1000))

#define IOS3_KERNEL_PATCH_NO_SITE UINT32_MAX
#define IOS3_KERNEL_PATCH_NO_SEGMENT UINT32_MAX
#define IOS3_KERNEL_PATCH_NO_BYTE UINT32_MAX
#define IOS3_KERNEL_PATCH_NO_ADDRESS UINT64_MAX

/* Exact identity of the supported decrypted kernel bytes. */
extern const uint8_t ios3_kernel_patch_expected_sha256[
    IOS3_KERNEL_PATCH_SHA256_LENGTH];
extern const uint8_t ios3_kernel_patch_expected_sha256_m68[
    IOS3_KERNEL_PATCH_SHA256_LENGTH];
extern const uint8_t ios3_kernel_patch_expected_uuid[
    IOS3_KERNEL_PATCH_UUID_LENGTH];

typedef enum {
    IOS3_KERNEL_PATCH_SITE_IORTC = 0,
    IOS3_KERNEL_PATCH_SITE_BSD_ROOT,
    IOS3_KERNEL_PATCH_SITE_MD_READ,
    IOS3_KERNEL_PATCH_SITE_MD_WRITE,
    IOS3_KERNEL_PATCH_SITE_RAW_WATCHER,
    IOS3_KERNEL_PATCH_SITE_PACKET_RX,
    IOS3_KERNEL_PATCH_SITE_PACKET_TX,
    IOS3_KERNEL_PATCH_SITE_PACKET_BATCH,
    IOS3_KERNEL_PATCH_SITE_MD_REGISTER,
    IOS3_KERNEL_PATCH_SITE_MD_BOUNDS,
    IOS3_KERNEL_PATCH_SITE_MD_HIGH,
    IOS3_KERNEL_PATCH_SITE_MD_COUNT64,
    IOS3_KERNEL_PATCH_SITE_MD_COUNT32
} ios3_kernel_patch_site_t;

typedef enum {
    IOS3_KERNEL_PATCH_STATUS_OK = 0,
    IOS3_KERNEL_PATCH_STATUS_INVALID_ARGUMENT,
    IOS3_KERNEL_PATCH_STATUS_HOST_RANGE_OVERFLOW,
    IOS3_KERNEL_PATCH_STATUS_REPORT_OVERLAP,
    IOS3_KERNEL_PATCH_STATUS_REQUEST_KERNEL_OVERLAP,
    IOS3_KERNEL_PATCH_STATUS_REQUEST_RAM_OVERLAP,
    IOS3_KERNEL_PATCH_STATUS_KERNEL_RAM_OVERLAP,
    IOS3_KERNEL_PATCH_STATUS_KERNEL_SIZE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_MACHO_PARSE_FAILED,
    IOS3_KERNEL_PATCH_STATUS_CPU_TYPE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_CPU_SUBTYPE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_FILE_TYPE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_UUID_MISSING,
    IOS3_KERNEL_PATCH_STATUS_UUID_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_SEGMENT_COUNT_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_SEGMENT_NAME_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_SEGMENT_VMADDR_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_SEGMENT_VMSIZE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_SEGMENT_FILEOFF_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_SEGMENT_FILESIZE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_VM_RANGE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_ENTRYPOINT_MISSING,
    IOS3_KERNEL_PATCH_STATUS_ENTRYPOINT_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_ENTRY_STACK_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISSING,
    IOS3_KERNEL_PATCH_STATUS_SYMTAB_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_KERNEL_DIGEST_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_VIRT_BASE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_RAM_BASE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_INVALID_RAM_GEOMETRY,
    IOS3_KERNEL_PATCH_STATUS_RAM_TOO_SMALL,
    IOS3_KERNEL_PATCH_STATUS_LOADED_SEGMENT_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_RAW_WATCHER_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_UIOMOVE_MISMATCH,
    IOS3_KERNEL_PATCH_STATUS_PATCH_TRANSACTION_FAILED
} ios3_kernel_patch_status_t;

/*
 * `kernel_file` is the complete decrypted Mach-O whose exact byte length is
 * `kernel_file_size`; the gate parses and SHA-256-hashes those bytes itself.
 * `ram` must already contain the loaded kernel segments, including zero-filled
 * bytes from each segment's file size through its VM size. This manifest
 * accepts only the iPhone 2,1 mapping C0000000 -> 08000000.
 *
 * Every nonempty range below must be a valid, non-wrapping host range. The
 * request object, kernel file, and RAM aperture must be pairwise disjoint.
 * Guest execution and all access to those ranges must remain stopped for the
 * whole call.
 */
typedef struct {
    const uint8_t *kernel_file;
    size_t kernel_file_size;
    uint8_t *ram;
    size_t ram_size;
    uint64_t ram_base;
    uint32_t virt_base;
    /* Opt-in only with an installed md_geometry_bridge. Legacy media and
     * desktop callers retain the original eight-site patch manifest. */
    bool extended_md_geometry;
    /* Opt-in only with an installed guest packet bridge; requires nothing
     * else. Extended geometry is accepted only together with it. */
    bool packet_bridge;
} ios3_kernel_patch_request_t;

/*
 * The first deterministic failure is reported. `site`, `segment_index`, and
 * `byte_index` use the *_NO_* sentinels when they do not apply.
 * expected_value/actual_value hold scalar identity values, or the mismatching
 * digest, UUID, or site byte. `macho_status` and `guest_patch_status` retain
 * detailed subordinate failures.
 *
 * The report must be a valid object disjoint from the request object, kernel
 * file, and RAM aperture. This is checked before the first report write. On
 * REPORT_OVERLAP or HOST_RANGE_OVERFLOW the function returns the status but
 * deliberately leaves `*report` byte-for-byte untouched, because writing it
 * could corrupt an input or guest RAM. On every other call with a non-NULL,
 * disjoint report, the report is fully reset and populated.
 */
typedef struct {
    ios3_kernel_patch_status_t status;
    uint32_t site;
    uint32_t segment_index;
    uint32_t byte_index;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t expected_value;
    uint64_t actual_value;
    macho_status_t macho_status;
    guest_patch_status_t guest_patch_status;
} ios3_kernel_patch_report_t;

/*
 * Validate the complete build identity, exact segment topology, loaded file
 * bytes and zero-fill tails, fixed mapping, and every expected byte at the
 * eight base patch sites (plus five optional extended-geometry sites) before
 * applying any replacement. The raw-mdev entry/completion pair is one site.
 * Any rejection leaves the kernel file and guest RAM unchanged. The
 * implementation performs no allocation.
 */
ios3_kernel_patch_status_t ios3_kernel_patch_apply(
    const ios3_kernel_patch_request_t *request,
    ios3_kernel_patch_report_t *report);

/* Portable allocation-free SHA-256 used by the gate. This diagnostic helper
 * exists so its implementation can be checked against public known-answer
 * vectors. `digest` may overlap `data`: it is written only after all input has
 * been consumed. NULL data is accepted only for a zero-length message. Inputs
 * longer than SHA-256's 64-bit bit-length field are rejected before output. */
bool ios3_kernel_patch_sha256(
    const uint8_t *data,
    size_t length,
    uint8_t digest[IOS3_KERNEL_PATCH_SHA256_LENGTH]);

const char *ios3_kernel_patch_status_string(ios3_kernel_patch_status_t status);
const char *ios3_kernel_patch_site_string(uint32_t site);

#endif /* S5LBOX_IOS3_KERNEL_PATCH_H */
