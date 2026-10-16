/* /arm-io/camin, the S5L8900 camera input block ("camin,s5l8900x", VIC 15),
 * as driven by 1A543a's AppleH1CamIn. Observed in its interrupt handler
 * c0308ad0: two capture channels report through status words at 0x64 and
 * 0xB8. Bit 12 there means "one frame, index reset", bit 13 "frames done up
 * to the hardware index in bits 27:26"; bits 31/30/29/18 are error reports.
 * Writes to 0x08 acknowledge: 0x30/0x20 for the 0x64 channel, 0x03/0x02 for
 * the 0xB8 channel, 0xC0000 for the block. Register 0xC0 bit 31 is the
 * capture enable the handler drops when software runs out of buffers. */
#include "soc.h"
#include <stdio.h>
#include <string.h>

void s5l_camin_reset(s5l_camin_t *c) {
    if (!c) return;
    bool trace = c->trace;
    uint32_t frame_ticks = c->frame_ticks, hw = c->host_w, hh = c->host_h;
    const uint32_t *hf = c->host_frame;
    memset(c, 0, sizeof *c);
    c->trace = trace;
    c->frame_ticks = frame_ticks;
    c->host_frame = hf; c->host_w = hw; c->host_h = hh;
}

static unsigned word(uint32_t off) { return (off >> 2) % (S5L_CAMIN_SIZE / 4u); }

uint32_t s5l_camin_read(s5l_camin_t *c, uint32_t off) {
    uint32_t v = off < S5L_CAMIN_SIZE ? c->regs[word(off)] : 0u;
    c->reads++;
    if (c->trace) fprintf(stderr, "camin r %03x = %08x\n", off, v);
    return v;
}

void s5l_camin_write(s5l_camin_t *c, uint32_t off, uint32_t val) {
    c->writes++;
    if (c->trace) fprintf(stderr, "camin w %03x = %08x\n", off, val);
    if (off == CAMIN_ACK) {
        if (val & 0x10u) c->regs[word(CAMIN_STAT_A)] &= ~CAMIN_STAT_RESET;
        if (val & 0x20u) c->regs[word(CAMIN_STAT_A)] &= ~CAMIN_STAT_DONE;
        if (val & 0x01u) c->regs[word(CAMIN_STAT_B)] &= ~CAMIN_STAT_RESET;
        if (val & 0x02u) c->regs[word(CAMIN_STAT_B)] &= ~CAMIN_STAT_DONE;
        c->regs[word(off)] = val & ~0xc0033u;
        return;
    }
    if (off >= S5L_CAMIN_SIZE || off == CAMIN_STAT_A || off == CAMIN_STAT_B)
        return;
    c->regs[word(off)] = val;
}

bool s5l_camin_irq(const s5l_camin_t *c) {
    return c && ((c->regs[word(CAMIN_STAT_A)] | c->regs[word(CAMIN_STAT_B)]) &
                 (CAMIN_STAT_RESET | CAMIN_STAT_DONE)) != 0u;
}

/* Deterministic test scene: eight vertical colour bars with a bright square
 * that walks one step per frame, so a live preview is visibly live. */
static uint32_t scene_pixel(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint32_t seq) {
    static const uint32_t bars[8] = {
        0xffffffffu, 0xffffff00u, 0xff00ffffu, 0xff00ff00u,
        0xffff00ffu, 0xffff0000u, 0xff0000ffu, 0xff202020u};
    uint32_t side = (w < h ? w : h) / 4u;
    if (!side) side = 1u;
    uint32_t span = w > side ? w - side : 1u;
    uint32_t sx = (seq * 4u) % span, sy = (h - side) / 2u;
    if (x >= sx && x < sx + side && y >= sy && y < sy + side)
        return 0xfff0f0f0u ^ ((seq & 16u) ? 0x00f00000u : 0u);
    return bars[(x * 8u) / (w ? w : 1u)];
}

void s5l_camin_fill(s5l_camin_t *c, uint8_t *dst, uint32_t w, uint32_t h,
                    uint32_t stride) {
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = dst + (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t p;
            if (c->host_frame && c->host_w && c->host_h)
                p = c->host_frame[(size_t)(y * c->host_h / h) * c->host_w +
                                  x * c->host_w / w];
            else
                p = scene_pixel(x, y, w, h, c->scene_seq);
            row[4 * x + 0] = (uint8_t)p;
            row[4 * x + 1] = (uint8_t)(p >> 8);
            row[4 * x + 2] = (uint8_t)(p >> 16);
            row[4 * x + 3] = 0xffu;
        }
    }
}

bool s5l_camin_tick(s5l_camin_t *c, uint32_t tb, s5l_camin_ram_fn ram,
                    void *ctx) {
    if (!c || !c->frame_ticks || !(c->regs[CAMIN_CTRL / 4u] & CAMIN_CTRL_EN)) {
        if (c) c->frame_accum = 0u;
        return s5l_camin_irq(c);
    }
    c->frame_accum += tb;
    if (c->frame_accum < c->frame_ticks) return s5l_camin_irq(c);
    c->frame_accum %= c->frame_ticks;

    uint32_t geom = c->regs[0];
    uint32_t w = geom & 0x1fffu, h = (geom >> 16) & 0x1fffu;
    uint32_t stride = w * 4u;
    uint32_t slot = c->frame_index & 3u;
    uint32_t pa = c->regs[(0x18u >> 2) + slot];
    uint8_t *dst = w && h && ram ? ram(ctx, pa, stride * h) : NULL;
    if (dst) s5l_camin_fill(c, dst, w, h, stride);
    else c->dropped++;
    c->scene_seq++;
    c->frames++;
    c->frame_index = (c->frame_index + 1u) & 3u;
    uint32_t *st = &c->regs[CAMIN_STAT_A / 4u];
    *st = (*st & ~(3u << 26)) | CAMIN_STAT_DONE | (c->frame_index << 26);
    if (c->trace)
        fprintf(stderr, "camin frame %llu slot %u pa %08x %ux%u%s\n",
                (unsigned long long)c->frames, slot, pa, w, h,
                dst ? "" : " (dropped)");
    return true;
}
