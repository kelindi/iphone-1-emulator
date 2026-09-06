/*
 * S5LBox — Apple flattened device tree.
 *
 * iBoot hands XNU a device tree describing the hardware: which peripherals
 * exist, where their registers live, which interrupts they use. The kernel
 * cannot boot without it, so parsing (and later synthesising) this format is a
 * prerequisite for M4.
 *
 * This is Apple's own format, not the Open Firmware / FDT one:
 *
 *   node     := uint32 nProperties, uint32 nChildren,
 *               property[nProperties], node[nChildren]
 *   property := char name[32] (NUL-padded), uint32 length, byte value[length]
 *               with the value padded out to a 4-byte boundary
 *
 * The top bit of `length` is used as a flag by Apple's tooling, so it is masked
 * off before use.
 *
 * Device trees come from user-supplied firmware, so this parser is written
 * defensively: every offset is bounds-checked, and nesting is depth-limited so
 * a hostile or corrupt tree cannot drive unbounded recursion.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_DEVICETREE_H
#define S5LBOX_DEVICETREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DT_PROP_NAME_LEN 32
#define DT_MAX_DEPTH     32       /* hostile-input guard */

typedef enum {
    DT_OK = 0,
    DT_ERR_TRUNCATED,     /* a node or property runs past the end of the blob */
    DT_ERR_TOO_DEEP,      /* nesting exceeded DT_MAX_DEPTH                    */
    DT_ERR_NOT_FOUND      /* lookup failed                                     */
} dt_status_t;

typedef struct {
    const uint8_t *blob;
    size_t         len;
} dt_t;

/* A node is identified by its offset within the blob. */
typedef struct {
    size_t   offset;      /* offset of the node header      */
    uint32_t n_props;
    uint32_t n_children;
    size_t   props_off;   /* offset of the first property   */
} dt_node_t;

/* Validate the whole tree and return the root. */
dt_status_t dt_parse(const uint8_t *blob, size_t len, dt_t *dt, dt_node_t *root);

/* Look up a property on a node. `value`/`len` may be NULL. */
dt_status_t dt_property(const dt_t *dt, const dt_node_t *node, const char *name,
                        const uint8_t **value, uint32_t *len);

/* Convenience: read a property as a little-endian uint32. */
dt_status_t dt_property_u32(const dt_t *dt, const dt_node_t *node,
                            const char *name, uint32_t *out);

/* Find a direct child by its "name" property. */
dt_status_t dt_child(const dt_t *dt, const dt_node_t *parent, const char *name,
                     dt_node_t *out);

/* Walk a slash-separated path from `root`, e.g. "chosen" or "arm-io/uart0". */
dt_status_t dt_path(const dt_t *dt, const dt_node_t *root, const char *path,
                    dt_node_t *out);

const char *dt_strerror(dt_status_t st);

#endif /* S5LBOX_DEVICETREE_H */
