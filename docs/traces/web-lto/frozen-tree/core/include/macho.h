/*
 * S5LBox — 32-bit Mach-O loader for the XNU kernelcache.
 *
 * After decrypting and decompressing, the kernelcache is a plain 32-bit Mach-O
 * executable (magic feedface, cputype ARM, subtype v6). Booting it means
 * placing each segment at the physical address its virtual address maps to,
 * and finding the entry point the kernel wants to start at.
 *
 * Mach-O is a documented format, and this parser treats it as untrusted input
 * because it arrives inside user-supplied firmware: every load command and
 * segment extent is validated against the buffer before use.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_MACHO_H
#define S5LBOX_MACHO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MH_MAGIC_32       0xfeedfaceu
#define MH_CPU_TYPE_ARM   12u
#define MH_CPU_SUBTYPE_V6 6u
#define MH_EXECUTE        2u

#define LC_SEGMENT        0x01u
#define LC_SYMTAB         0x02u
#define LC_UNIXTHREAD     0x05u
#define LC_UUID           0x1bu

#define MACHO_MAX_SEGMENTS 16u

typedef struct {
    char     name[17];
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
} macho_segment_t;

typedef struct {
    uint32_t        cputype;
    uint32_t        cpusubtype;
    uint32_t        filetype;
    macho_segment_t segments[MACHO_MAX_SEGMENTS];
    unsigned        segment_count;

    bool     has_entry;
    uint32_t entry;          /* initial PC from LC_UNIXTHREAD */
    uint32_t entry_sp;       /* initial SP, if the thread state carries one */

    /*
     * LC_SYMTAB. The kernelcache is not stripped, so this is what turns a
     * panic (or a hot PC in a profile) from hex into names. Extents are
     * validated against the buffer here, once, so every consumer can trust
     * them — see ksyms.c, the only one so far.
     */
    bool     has_symtab;
    uint32_t symoff, nsyms;  /* nlist_32 array: 12 bytes per entry */
    uint32_t stroff, strsize;

    /* LC_UUID is the kernel-build identity used to gate version-specific
     * compatibility patches. It is an opaque 16-byte value, not text. */
    bool     has_uuid;
    uint8_t  uuid[16];

    uint32_t vm_low;         /* lowest  vmaddr across segments */
    uint32_t vm_high;        /* highest vmaddr + vmsize        */
} macho_t;

typedef enum {
    MACHO_OK = 0,
    MACHO_ERR_TOO_SMALL,
    MACHO_ERR_BAD_MAGIC,     /* not a 32-bit little-endian Mach-O          */
    MACHO_ERR_NOT_ARM,       /* wrong CPU type for this machine            */
    MACHO_ERR_MALFORMED,     /* a load command or segment runs past the end */
    MACHO_ERR_TOO_MANY,      /* more segments than we track                 */
    MACHO_ERR_INVALID_ARGUMENT
} macho_status_t;

macho_status_t macho_parse(const uint8_t *buf, size_t len, macho_t *out);
const char *macho_strerror(macho_status_t st);

/* The segment with this exact name, or NULL. */
const macho_segment_t *macho_segment(const macho_t *m, const char *name);

#endif /* S5LBOX_MACHO_H */
