/*
 * S5LBox — Apple flattened device tree parser.
 *
 * Written for untrusted input: every read is bounds-checked against the blob,
 * all offset arithmetic is done in 64-bit so it cannot wrap, and recursion is
 * depth-limited. A corrupt tree must produce an error, never an out-of-bounds
 * read or a stack overflow.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "devicetree.h"
#include <string.h>

#define PROP_HEADER (DT_PROP_NAME_LEN + 4u)   /* name[32] + length */
#define NODE_HEADER 8u                        /* nProperties + nChildren */

const char *dt_strerror(dt_status_t st) {
    switch (st) {
        case DT_OK:            return "ok";
        case DT_ERR_TRUNCATED: return "device tree is truncated or malformed";
        case DT_ERR_TOO_DEEP:  return "device tree nesting is too deep";
        case DT_ERR_NOT_FOUND: return "not found";
        default:               return "unknown error";
    }
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline bool fits(const dt_t *dt, size_t off, uint64_t need) {
    return (uint64_t)off + need <= (uint64_t)dt->len;
}

static inline uint64_t align4(uint64_t v) { return (v + 3u) & ~(uint64_t)3u; }

/* Read a node header at `off`. */
static dt_status_t read_node(const dt_t *dt, size_t off, dt_node_t *out) {
    if (!fits(dt, off, NODE_HEADER)) return DT_ERR_TRUNCATED;
    out->offset     = off;
    out->n_props    = rd32(&dt->blob[off]);
    out->n_children = rd32(&dt->blob[off + 4]);
    out->props_off  = off + NODE_HEADER;
    return DT_OK;
}

/* Offset just past the property at `off`, or 0 on error. */
static size_t skip_property(const dt_t *dt, size_t off) {
    if (!fits(dt, off, PROP_HEADER)) return 0;
    uint32_t len = rd32(&dt->blob[off + DT_PROP_NAME_LEN]) & 0x7fffffffu;
    uint64_t next = align4((uint64_t)off + PROP_HEADER + len);
    if (next > (uint64_t)dt->len) return 0;
    return (size_t)next;
}

/* Offset just past the whole subtree rooted at `off`, or 0 on error. */
static size_t skip_node(const dt_t *dt, size_t off, unsigned depth) {
    if (depth > DT_MAX_DEPTH) return 0;

    dt_node_t n;
    if (read_node(dt, off, &n) != DT_OK) return 0;

    size_t cur = n.props_off;
    for (uint32_t i = 0; i < n.n_props; i++) {
        cur = skip_property(dt, cur);
        if (!cur) return 0;
    }
    for (uint32_t i = 0; i < n.n_children; i++) {
        cur = skip_node(dt, cur, depth + 1);
        if (!cur) return 0;
    }
    return cur;
}

dt_status_t dt_parse(const uint8_t *blob, size_t len, dt_t *dt, dt_node_t *root) {
    if (!blob || len < NODE_HEADER) return DT_ERR_TRUNCATED;
    dt->blob = blob;
    dt->len  = len;

    /* Validate the entire tree up front so later lookups can be simple. */
    if (skip_node(dt, 0, 0) == 0) return DT_ERR_TRUNCATED;
    return read_node(dt, 0, root);
}

dt_status_t dt_property(const dt_t *dt, const dt_node_t *node, const char *name,
                        const uint8_t **value, uint32_t *len) {
    size_t cur = node->props_off;
    for (uint32_t i = 0; i < node->n_props; i++) {
        if (!fits(dt, cur, PROP_HEADER)) return DT_ERR_TRUNCATED;

        /* Names are NUL-padded to 32 bytes; compare within that window only. */
        const char *pname = (const char *)&dt->blob[cur];
        if (strncmp(pname, name, DT_PROP_NAME_LEN) == 0) {
            uint32_t plen = rd32(&dt->blob[cur + DT_PROP_NAME_LEN]) & 0x7fffffffu;
            if (!fits(dt, cur + PROP_HEADER, plen)) return DT_ERR_TRUNCATED;
            if (value) *value = &dt->blob[cur + PROP_HEADER];
            if (len)   *len   = plen;
            return DT_OK;
        }
        cur = skip_property(dt, cur);
        if (!cur) return DT_ERR_TRUNCATED;
    }
    return DT_ERR_NOT_FOUND;
}

dt_status_t dt_property_u32(const dt_t *dt, const dt_node_t *node,
                            const char *name, uint32_t *out) {
    const uint8_t *v; uint32_t n;
    dt_status_t st = dt_property(dt, node, name, &v, &n);
    if (st != DT_OK) return st;
    if (n < 4) return DT_ERR_TRUNCATED;
    if (out) *out = rd32(v);
    return DT_OK;
}

dt_status_t dt_child(const dt_t *dt, const dt_node_t *parent, const char *name,
                     dt_node_t *out) {
    /* Children follow this node's properties. */
    size_t cur = parent->props_off;
    for (uint32_t i = 0; i < parent->n_props; i++) {
        cur = skip_property(dt, cur);
        if (!cur) return DT_ERR_TRUNCATED;
    }

    for (uint32_t i = 0; i < parent->n_children; i++) {
        dt_node_t child;
        if (read_node(dt, cur, &child) != DT_OK) return DT_ERR_TRUNCATED;

        const uint8_t *v; uint32_t n;
        if (dt_property(dt, &child, "name", &v, &n) == DT_OK) {
            /* The stored name may or may not include its NUL. */
            size_t want = strlen(name);
            if (n >= want && strncmp((const char *)v, name, want) == 0 &&
                (n == want || v[want] == '\0')) {
                *out = child;
                return DT_OK;
            }
        }
        cur = skip_node(dt, cur, 0);
        if (!cur) return DT_ERR_TRUNCATED;
    }
    return DT_ERR_NOT_FOUND;
}

dt_status_t dt_path(const dt_t *dt, const dt_node_t *root, const char *path,
                    dt_node_t *out) {
    dt_node_t cur = *root;
    const char *p = path;

    while (*p == '/') p++;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t seg = slash ? (size_t)(slash - p) : strlen(p);
        if (seg == 0 || seg >= DT_PROP_NAME_LEN) return DT_ERR_NOT_FOUND;

        char name[DT_PROP_NAME_LEN];
        memcpy(name, p, seg);
        name[seg] = '\0';

        dt_node_t child;
        dt_status_t st = dt_child(dt, &cur, name, &child);
        if (st != DT_OK) return st;
        cur = child;

        p += seg;
        while (*p == '/') p++;
    }
    *out = cur;
    return DT_OK;
}
