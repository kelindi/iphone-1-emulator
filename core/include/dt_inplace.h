/*
 * S5LBox — in-place Apple device-tree patching, standing in for iBoot.
 *
 * The device tree shipped in the IPSW is a TEMPLATE. On real hardware iBoot
 * measures the PLLs and writes the actual clock rates into it before handing
 * it to the kernel; every frequency property in the file on disk is zero.
 * pe_identify_machine() copies those zeros into gPEClockFrequencyInfo, and the
 * kernel then divides by them:
 *
 *   pe_identify_machine+0xbe:  bus_to_cpu_rate_num = (cpu_clock_hz * 2) / bus_clock_hz
 *   pe_identify_machine+0xd0:  bus_to_dec_rate_den = bus_clock_hz / dec_clock_hz
 *   rtclock.c:132              panic if timebase_num < timebase_den
 *
 * so we do iBoot's job here. Patching is IN PLACE and SAME-LENGTH: the flat
 * format has no relocation table but every offset is implicit in the byte
 * stream, so resizing a property would mean rebuilding the whole blob.
 *
 * Format (Apple's, not FDT):
 *   node     := u32 nProperties, u32 nChildren, property[], node[]
 *   property := char name[32], u32 length (bit31 is a tool flag), value[],
 *               padded up to a 4-byte boundary
 *
 * These live in a header rather than inside bootkernel.c because the values
 * they write decide whether the guest's display comes up at all — /vram's reg
 * entry in particular — and a rule that important has to be reachable from
 * core/tests/test_devicetree.c. They are `static inline` so a translation unit
 * that uses only some of them still compiles under -Wall -Wextra -Werror.
 * Nothing here allocates, and nothing writes outside the caller's buffer.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_DT_INPLACE_H
#define S5LBOX_DT_INPLACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DTN_NAME_LEN 32           /* device-tree property names are char[32] */

static inline uint32_t dtn_ld32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline size_t dtn_hdr(const uint8_t *b, size_t len, size_t off,
                             uint32_t *np, uint32_t *nc) {
    if (off + 8 > len) return 0;
    *np = dtn_ld32(b + off);
    *nc = dtn_ld32(b + off + 4);
    if (*np > 4096 || *nc > 4096) return 0;   /* corrupt-input guard */
    return off + 8;
}

/* Offset just past the last property of the node at `off`. */
static inline size_t dtn_props_end(const uint8_t *b, size_t len, size_t off) {
    uint32_t np, nc;
    size_t p = dtn_hdr(b, len, off, &np, &nc);
    if (!p) return 0;
    for (uint32_t i = 0; i < np; i++) {
        if (p + 36 > len) return 0;
        uint32_t l = dtn_ld32(b + p + 32) & 0x7fffffffu;
        p += 36 + ((l + 3u) & ~3u);
        if (p > len) return 0;
    }
    return p;
}

/* Offset just past the whole subtree rooted at `off`. */
static inline size_t dtn_end(const uint8_t *b, size_t len, size_t off,
                             unsigned depth) {
    uint32_t np, nc;
    if (depth > 32) return 0;
    if (!dtn_hdr(b, len, off, &np, &nc)) return 0;
    size_t p = dtn_props_end(b, len, off);
    for (uint32_t i = 0; p && i < nc; i++) p = dtn_end(b, len, p, depth + 1);
    return p;
}

/* Writable pointer to a property's value on the node at `off`. */
static inline uint8_t *dtn_prop(uint8_t *b, size_t len, size_t off,
                                const char *name, uint32_t *vlen) {
    uint32_t np, nc;
    size_t p = dtn_hdr(b, len, off, &np, &nc);
    if (!p) return NULL;
    for (uint32_t i = 0; i < np; i++) {
        if (p + 36 > len) return NULL;
        char nm[DTN_NAME_LEN + 1];
        memcpy(nm, b + p, DTN_NAME_LEN); nm[DTN_NAME_LEN] = '\0';
        uint32_t l = dtn_ld32(b + p + 32) & 0x7fffffffu;
        if (p + 36 + (size_t)l > len) return NULL;
        if (!strcmp(nm, name)) { if (vlen) *vlen = l; return b + p + 36; }
        p += 36 + ((l + 3u) & ~3u);
    }
    return NULL;
}

#define DT_NONE ((size_t)-1)

/* Walk a slash-separated path of node "name" properties from the root.
 * "" is the root itself; "cpus/cpu0" is the CPU node. */
static inline size_t dtn_path(uint8_t *b, size_t len, const char *path) {
    size_t off = 0;
    while (path && *path) {
        while (*path == '/') path++;
        if (!*path) break;
        const char *slash = strchr(path, '/');
        size_t clen = slash ? (size_t)(slash - path) : strlen(path);
        uint32_t np, nc;
        if (!dtn_hdr(b, len, off, &np, &nc)) return DT_NONE;
        size_t c = dtn_props_end(b, len, off);
        size_t found = DT_NONE;
        for (uint32_t i = 0; c && i < nc; i++) {
            uint32_t vl = 0;
            const uint8_t *nm = dtn_prop(b, len, c, "name", &vl);
            if (nm && vl >= clen && !memcmp(nm, path, clen) &&
                (vl == clen || nm[clen] == '\0')) { found = c; break; }
            c = dtn_end(b, len, c, 0);
        }
        if (found == DT_NONE) return DT_NONE;
        off = found;
        path = slash ? slash + 1 : path + clen;
    }
    return off;
}

/* Overwrite a 4-byte property in place. Refuses to change its length. */
static inline bool dt_set_u32(uint8_t *b, size_t len, const char *path,
                              const char *prop, uint32_t v) {
    size_t node = dtn_path(b, len, path);
    if (node == DT_NONE) {
        printf("  dt: node /%-22s NOT FOUND (skipping %s)\n", path, prop);
        return false;
    }
    uint32_t vl = 0;
    uint8_t *p = dtn_prop(b, len, node, prop, &vl);
    if (!p) {
        printf("  dt: /%s: property %s NOT FOUND\n", path, prop);
        return false;
    }
    if (vl != 4) {
        printf("  dt: /%s:%s is %u bytes, not 4 — refusing to resize\n",
               path, prop, vl);
        return false;
    }
    uint32_t old = dtn_ld32(p);
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    printf("  dt: /%-14s %-22s 0x%08x -> 0x%08x (%u)\n",
           *path ? path : "device-tree", prop, old, v, v);
    return true;
}

/*
 * A path lookup alone is not enough for /arm-io/spi0/lcd0: the 7E18 tree has
 * two siblings with the same name.  dtn_path() deliberately returns the first,
 * which is the Merlot panel in the stock tree, but silently writing whichever
 * duplicate happens to come first would corrupt a different device if the
 * template order ever changed.  Require one exact, bounded C string before the
 * panel-specific patch.  A compatible string list, missing terminator, prefix,
 * or trailing bytes all fail closed.
 */
static inline bool dt_node_compatible_exact(uint8_t *b, size_t len,
                                            const char *path,
                                            const char *expected) {
    size_t node = dtn_path(b, len, path);
    if (node == DT_NONE) {
        printf("  dt: node /%-22s NOT FOUND (checking compatible)\n", path);
        return false;
    }

    uint32_t vl = 0;
    const uint8_t *p = dtn_prop(b, len, node, "compatible", &vl);
    size_t expected_n = strlen(expected) + 1u;
    if (!p) {
        printf("  dt: /%s:compatible NOT FOUND\n", path);
        return false;
    }
    if ((size_t)vl != expected_n || p[expected_n - 1u] != '\0' ||
        memchr(p, '\0', expected_n - 1u) != NULL ||
        memcmp(p, expected, expected_n - 1u) != 0) {
        printf("  dt: /%s:compatible is not exact \"%s\" "
               "(length %u; refusing panel-specific patch)\n",
               path, expected, vl);
        return false;
    }
    return true;
}

/* Overwrite two consecutive 32-bit cells (an 8-byte "reg" entry) in place. */
static inline bool dt_set_reg(uint8_t *b, size_t len, const char *path,
                              const char *prop, uint32_t base, uint32_t size) {
    size_t node = dtn_path(b, len, path);
    if (node == DT_NONE) { printf("  dt: node /%s NOT FOUND\n", path); return false; }
    uint32_t vl = 0;
    uint8_t *p = dtn_prop(b, len, node, prop, &vl);
    if (!p || vl != 8) {
        printf("  dt: /%s:%s missing or not 8 bytes (%u)\n", path, prop, vl);
        return false;
    }
    uint32_t o0 = dtn_ld32(p), o1 = dtn_ld32(p + 4);
    uint32_t vals[2] = { base, size };
    for (int i = 0; i < 2; i++) {
        p[i*4+0] = (uint8_t)vals[i];        p[i*4+1] = (uint8_t)(vals[i] >> 8);
        p[i*4+2] = (uint8_t)(vals[i] >> 16); p[i*4+3] = (uint8_t)(vals[i] >> 24);
    }
    printf("  dt: /%-14s %-22s {0x%08x,0x%08x} -> {0x%08x,0x%08x}\n",
           path, prop, o0, o1, base, size);
    return true;
}

#endif /* S5LBOX_DT_INPLACE_H */
