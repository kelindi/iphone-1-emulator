#include "soc.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int32_t axis_max(unsigned electrodes, unsigned num, unsigned den) {
    unsigned i = electrodes ? electrodes - 1u : 0u;
    return 75 + (int32_t)(i * num / den);
}

static int32_t to_surface(uint32_t pixel, uint32_t span_pixels,
                          int32_t lo, int32_t hi, bool flip) {
    uint32_t span, step;
    if (!span_pixels || hi <= lo) return lo;
    span = (uint32_t)(hi - lo);
    step = (2u * pixel + 1u) * span / (2u * span_pixels);
    return flip ? hi - (int32_t)step : lo + (int32_t)step;
}

unsigned s5l_mt_report(const s5l_mt_geom_t *g, uint8_t id, uint8_t *out) {
    if (!g || !out) return 0u;
    memset(out, 0, MTZ2_REPORT_MAX);
    switch (id) {
    case MTZ2_REPORT_FAMILY_ID:
        out[0] = g->family_id;
        return 1u;
    case MTZ2_REPORT_GEOMETRY:
        out[0] = g->endianness;
        out[1] = g->rows;
        out[2] = g->columns;
        out[3] = (uint8_t)(g->bcd_version >> 8);
        out[4] = (uint8_t)g->bcd_version;
        return 5u;
    case MTZ2_REPORT_BUTTONS:
        out[0] = g->buttons;
        return 1u;
    case MTZ2_REPORT_SURFACE:
        put32(out, g->surface_width);
        put32(out + 4, g->surface_height);
        return 8u;
    case MTZ2_REPORT_REGION_DESC:
        out[MTZ2_REGION_RECORD + 0u] = 1u;
        out[MTZ2_REGION_RECORD + 2u] = g->rows;
        out[MTZ2_REGION_RECORD + 3u] = 1u;
        out[MTZ2_REGION_RECORD + 5u] = g->columns;
        return MTZ2_REGION_RECORD * MTZ2_REGION_RECORDS;
    case MTZ2_REPORT_REGION_PARAM:
        return 8u;
    case 0xabu:
        return 1u;
    default:
        return 0u;
    }
}

unsigned s5l_mt_frame_encode(const s5l_mt_geom_t *g,
                             const s5l_mt_contact_t *c, unsigned n,
                             uint8_t seq, uint32_t ms, uint8_t *out) {
    if (!g || !out || !c || n == 0u || n > MTZ2_CONTACT_MAX) return 0u;
    uint32_t scale_x = g->surface_width / S5L_MT_PANEL_W;
    unsigned len = MTZ2_FRAME_HEADER + n * MTZ2_CONTACT_STRIDE;
    memset(out, 0, MTZ2_PAYLOAD_LIMIT);
    out[0] = MTZ2_FRAME_TYPE;
    out[1] = seq;
    out[3] = (uint8_t)n;
    put32(out + 6, ms);
    for (unsigned i = 0; i < n; i++) {
        uint8_t *r = out + MTZ2_FRAME_HEADER + i * MTZ2_CONTACT_STRIDE;
        if (!c[i].id || c[i].id > MTZ2_CONTACT_MAX ||
            c[i].phase > MTZ2_PHASE_OUT_OF_RANGE ||
            c[i].x >= S5L_MT_PANEL_W || c[i].y >= S5L_MT_PANEL_H)
            return 0u;
        int32_t sx = to_surface(c[i].x, S5L_MT_PANEL_W, -75,
                                axis_max(g->columns, 5600u, 11u), false);
        int32_t sy = to_surface(c[i].y, S5L_MT_PANEL_H, -75,
                                axis_max(g->rows, 3600u, 7u), true);
        r[0] = c[i].id;
        r[1] = c[i].phase;
        r[2] = c[i].id;
        r[3] = 1u;
        put32(r + 4, (uint32_t)sx << 8);
        put32(r + 8, (uint32_t)sy << 8);
        put16(r + 20, c[i].pressure);
        put16(r + 28, (uint16_t)(c[i].major * scale_x));
        put16(r + 30, (uint16_t)(c[i].minor * scale_x));
    }
    return len;
}

unsigned s5l_mt_frame_encode_ce(const s5l_mt_geom_t *g,
                                const s5l_mt_contact_t *c, unsigned n,
                                uint8_t seq, uint32_t ms, uint8_t *out) {
    if (!g || !out || !c || n == 0u || n > MTZ2_CONTACT_MAX) return 0u;
    uint32_t scale_x = g->surface_width / S5L_MT_PANEL_W;
    unsigned len = MTZ1_FRAME_HEADER + n * MTZ2_CONTACT_STRIDE;
    if (len > MTZ2_PAYLOAD_LIMIT) return 0u;
    memset(out, 0, MTZ2_PAYLOAD_LIMIT);
    out[0] = MTZ1_FRAME_TYPE;
    out[1] = seq;
    out[3] = (uint8_t)n;
    put32(out + 6, ms);
    out[10] = MTZ1_FRAME_HEADER;
    for (unsigned i = 0; i < n; i++) {
        uint8_t *r = out + MTZ1_FRAME_HEADER + i * MTZ2_CONTACT_STRIDE;
        if (!c[i].id || c[i].id > MTZ2_CONTACT_MAX ||
            c[i].phase > MTZ2_PHASE_OUT_OF_RANGE ||
            c[i].x >= S5L_MT_PANEL_W || c[i].y >= S5L_MT_PANEL_H)
            return 0u;
        /* 1A543a's family-1 Z1 grid has zero edge margins and pitches
         * 38100/76 (columns), 9400/19 (rows). See MultitouchSupport:
         * _alg_InitGridPitchAndEdgeOptions 0x3130a35c..0x3130a3a8 and
         * _alg_InitRowColXYConvert 0x3130a11c..0x3130a238. The loaded
         * home snapshot confirms grid+0x150 bounds {4511,0,6926,0}
         * for ten columns/fifteen rows. N82's 75-unit margins and different
         * pitches belong only to the CC encoder above; reusing them shifts
         * a top-row tap (45,65) to approximately (42,45) in the real guest.
         * Map pixel centers into the bounds the guest actually normalizes. */
        int32_t xmax = g->columns ? (int32_t)((g->columns - 1u) * 38100u / 76u) : 0;
        int32_t ymax = g->rows ? (int32_t)((g->rows - 1u) * 9400u / 19u) : 0;
        int32_t sx = to_surface(c[i].x, S5L_MT_PANEL_W, 0, xmax, false);
        int32_t sy = to_surface(c[i].y, S5L_MT_PANEL_H, 0, ymax, true);
        r[0] = c[i].id;
        r[1] = c[i].phase;
        r[2] = c[i].id;
        r[3] = 1u;
        put16(r + 4, (uint16_t)sx);
        put16(r + 6, (uint16_t)sy);
        put16(r + 0x10u, c[i].pressure);
        put16(r + 0x14u, c[i].pressure);
        put16(r + 0x1cu, (uint16_t)(c[i].major * scale_x));
        put16(r + 0x1eu, (uint16_t)(c[i].minor * scale_x));
    }
    return len;
}
