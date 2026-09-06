/*
 * S5LBox — 32-bit Mach-O parser.
 *
 * Bounds-checked throughout: this reads a kernel image that came out of a
 * user-supplied IPSW, so a malformed load command must produce an error rather
 * than an out-of-bounds read.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "macho.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const char *macho_strerror(macho_status_t st) {
    switch (st) {
        case MACHO_OK:            return "ok";
        case MACHO_ERR_INVALID_ARGUMENT: return "invalid Mach-O parser argument";
        case MACHO_ERR_TOO_SMALL: return "buffer too small for a Mach-O header";
        case MACHO_ERR_BAD_MAGIC: return "not a 32-bit little-endian Mach-O";
        case MACHO_ERR_NOT_ARM:   return "not an ARM executable";
        case MACHO_ERR_MALFORMED: return "a load command or segment runs past the end";
        case MACHO_ERR_TOO_MANY:  return "more segments than the loader tracks";
        default:                  return "unknown error";
    }
}

macho_status_t macho_parse(const uint8_t *buf, size_t len, macho_t *out) {
    if (!out) return MACHO_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    if (!buf || len < 28) return MACHO_ERR_TOO_SMALL;
    if (rd32(buf) != MH_MAGIC_32) return MACHO_ERR_BAD_MAGIC;

    out->cputype    = rd32(buf + 4);
    out->cpusubtype = rd32(buf + 8);
    out->filetype   = rd32(buf + 12);
    uint32_t ncmds  = rd32(buf + 16);
    uint32_t cmdsz  = rd32(buf + 20);

    if (out->cputype != MH_CPU_TYPE_ARM) return MACHO_ERR_NOT_ARM;
    uint64_t cmds_end = (uint64_t)28 + cmdsz;
    if (cmds_end > (uint64_t)len) return MACHO_ERR_MALFORMED;

    out->vm_low = 0xffffffffu;
    uint64_t off = 28;

    for (uint32_t i = 0; i < ncmds; i++) {
        if (off + 8 > cmds_end) return MACHO_ERR_MALFORMED;
        uint32_t cmd     = rd32(buf + off);
        uint32_t cmdsize = rd32(buf + off + 4);
        /* A zero or tiny cmdsize would loop forever. */
        if (cmdsize < 8 || off + cmdsize > cmds_end) return MACHO_ERR_MALFORMED;

        if (cmd == LC_SEGMENT) {
            if (cmdsize < 56) return MACHO_ERR_MALFORMED;
            uint32_t nsects = rd32(buf + off + 48);
            if ((uint64_t)56 + (uint64_t)nsects * 68u != cmdsize)
                return MACHO_ERR_MALFORMED;
            if (out->segment_count >= MACHO_MAX_SEGMENTS) return MACHO_ERR_TOO_MANY;

            macho_segment_t *s = &out->segments[out->segment_count];
            memcpy(s->name, buf + off + 8, 16);
            s->name[16]  = '\0';
            s->vmaddr    = rd32(buf + off + 24);
            s->vmsize    = rd32(buf + off + 28);
            s->fileoff   = rd32(buf + off + 32);
            s->filesize  = rd32(buf + off + 36);

            /* The segment's file extent must actually be inside the image. */
            if ((uint64_t)s->fileoff + s->filesize > (uint64_t)len)
                return MACHO_ERR_MALFORMED;
            if (s->filesize > s->vmsize) return MACHO_ERR_MALFORMED;

            if (s->vmsize) {
                if (s->vmaddr < out->vm_low) out->vm_low = s->vmaddr;
                uint64_t end = (uint64_t)s->vmaddr + s->vmsize;
                if (end > 0xffffffffu) return MACHO_ERR_MALFORMED;
                if (end > out->vm_high) out->vm_high = (uint32_t)end;
            }
            out->segment_count++;
        } else if (cmd == LC_SYMTAB) {
            if (cmdsize < 24) return MACHO_ERR_MALFORMED;
            uint32_t symoff  = rd32(buf + off + 8);
            uint32_t nsyms   = rd32(buf + off + 12);
            uint32_t stroff  = rd32(buf + off + 16);
            uint32_t strsize = rd32(buf + off + 20);
            /* Validate here so no consumer has to re-derive the bounds. */
            if ((uint64_t)symoff + (uint64_t)nsyms * 12 > (uint64_t)len)
                return MACHO_ERR_MALFORMED;
            if ((uint64_t)stroff + strsize > (uint64_t)len)
                return MACHO_ERR_MALFORMED;
            out->symoff = symoff; out->nsyms = nsyms;
            out->stroff = stroff; out->strsize = strsize;
            out->has_symtab = true;
        } else if (cmd == LC_UNIXTHREAD) {
            /*
             * The initial thread state. For ARM the flavour is followed by a
             * register count and then r0..r12, sp, lr, pc, cpsr — so the entry
             * PC sits at word 15 of the register block.
             */
            if (cmdsize >= 8 + 8 + 17 * 4) {
                const uint8_t *regs = buf + off + 16;
                out->entry_sp  = rd32(regs + 13 * 4);
                out->entry     = rd32(regs + 15 * 4);
                out->has_entry = true;
            }
        } else if (cmd == LC_UUID) {
            /* uuid_command is exactly cmd/cmdsize plus 16 opaque bytes.
             * Multiple identities are ambiguous and therefore malformed. */
            if (cmdsize != 24 || out->has_uuid)
                return MACHO_ERR_MALFORMED;
            memcpy(out->uuid, buf + off + 8, sizeof out->uuid);
            out->has_uuid = true;
        }
        off += cmdsize;
    }

    if (off != cmds_end) return MACHO_ERR_MALFORMED;

    if (out->vm_low == 0xffffffffu) out->vm_low = 0;
    return MACHO_OK;
}

const macho_segment_t *macho_segment(const macho_t *m, const char *name) {
    if (!m || !name) return NULL;
    for (unsigned i = 0; i < m->segment_count; i++)
        if (!strcmp(m->segments[i].name, name)) return &m->segments[i];
    return NULL;
}
