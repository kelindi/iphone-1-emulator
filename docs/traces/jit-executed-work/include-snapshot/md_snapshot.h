/*
 * S5LBox -- persisted host state for an external memory-disk snapshot.
 *
 * A core snapshot owns the emulated machine and guest RAM.  The external
 * memory-disk bridges also have a small amount of host-owned state which the
 * core cannot serialize: the coherent allocation-tail overlay and diagnostic
 * counters.  The desktop harness has written this exact sidecar since version
 * 1.  Keeping the format here lets the app consume the same checkpoint without
 * maintaining a second, silently drifting struct definition.
 *
 * The disk image itself is not in this structure.  A restore is coherent only
 * when `image_bytes` describes the byte-exact work image installed beside the
 * snapshot.  Size equality is necessary but not sufficient; the caller owns
 * provenance and integrity verification of that image.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_MD_SNAPSHOT_H
#define S5LBOX_MD_SNAPSHOT_H

#include "md_bridge.h"
#include "md_raw_bridge.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXTERNAL_MD_SIDECAR_MAGIC UINT32_C(0x3144534d) /* "MDS1" */
#define EXTERNAL_MD_SIDECAR_VERSION UINT32_C(1)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t media_size;
    uint64_t image_bytes;
    md_bridge_stats_t strategy_stats;
    md_raw_bridge_stats_t raw_stats;
    uint8_t guard_tail[MD_RAW_BRIDGE_MAX_TRANSFER];
} external_md_sidecar_t;

#ifdef __cplusplus
}
#endif

#endif /* S5LBOX_MD_SNAPSHOT_H */
