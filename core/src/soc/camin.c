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

/* 0x58 bit 12 is set for 'BGRA' (32 bpp) by the buffer-setup helper
 * c0306c68 and clear for the 16 bpp still, which the JPEG encoder takes as
 * 4:2:2 YCbCr ('2vuy': Cb Y0 Cr Y1); 0x5C holds the buffer size in pixels. */
#define CAMIN_FMT          0x58u
#define CAMIN_FMT_BGRA     (1u << 12)
#define CAMIN_PIXELS       0x5cu
#define CAMIN_OUT_SIZE     0x54u
#define CAMIN_SCALER       0x48u
#define CAMIN_SCALER_PORTRAIT (1u << 13)

/* The host frame and test scene are upright as the user holds the phone.
 * The preview scaler outputs upright portrait frames; the still and the
 * post-capture review keep the sensor's layout, which the guest turns a
 * quarter clockwise for display, so those are sampled turned back. */
static uint32_t frame_pixel(const s5l_camin_t *c, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, bool turned) {
    uint32_t X = x, Y = y, W = w, H = h;
    if (turned) { X = h - 1u - y; Y = x; W = h; H = w; }
    if (c->host_frame && c->host_w && c->host_h)
        return c->host_frame[(size_t)(Y * c->host_h / H) * c->host_w +
                             X * c->host_w / W];
    return scene_pixel(X, Y, W, H, c->scene_seq);
}

static void fill_bgra(s5l_camin_t *c, uint8_t *dst, uint32_t w, uint32_t h,
                      uint32_t stride, bool turned) {
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = dst + (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t p = frame_pixel(c, x, y, w, h, turned);
            row[4 * x + 0] = (uint8_t)p;
            row[4 * x + 1] = (uint8_t)(p >> 8);
            row[4 * x + 2] = (uint8_t)(p >> 16);
            row[4 * x + 3] = 0xffu;
        }
    }
}

void s5l_camin_fill(s5l_camin_t *c, uint8_t *dst, uint32_t w, uint32_t h,
                    uint32_t stride) {
    fill_bgra(c, dst, w, h, stride, false);
}

static void fill422(s5l_camin_t *c, uint8_t *dst, uint32_t w, uint32_t h,
                    uint32_t stride) {
    bool turned = true;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = dst + (size_t)y * stride;
        for (uint32_t x = 0; x + 1u < w; x += 2u) {
            int yy[2], cb = 0, cr = 0;
            for (int k = 0; k < 2; k++) {
                uint32_t p = frame_pixel(c, x + (uint32_t)k, y, w, h, turned);
                int r = (int)((p >> 16) & 0xffu), g = (int)((p >> 8) & 0xffu),
                    b = (int)(p & 0xffu);
                yy[k] = (77 * r + 150 * g + 29 * b + 128) >> 8;
                cb += ((-43 * r - 85 * g + 128 * b + 128) >> 8) + 128;
                cr += ((128 * r - 107 * g - 21 * b + 128) >> 8) + 128;
            }
            cb /= 2; cr /= 2;
            row[2 * x + 0] = (uint8_t)(cb > 255 ? 255 : cb < 0 ? 0 : cb);
            row[2 * x + 1] = (uint8_t)(yy[0] > 255 ? 255 : yy[0]);
            row[2 * x + 2] = (uint8_t)(cr > 255 ? 255 : cr < 0 ? 0 : cr);
            row[2 * x + 3] = (uint8_t)(yy[1] > 255 ? 255 : yy[1]);
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

    /* 0x54 is the output size (height << 16 | width). Preview and still
     * also mirror it in 0x00, but the post-capture review frame keeps 0x00
     * at the 480x640 sensor window and scales to 300x400 through 0x54. */
    uint32_t geom = c->regs[CAMIN_OUT_SIZE / 4u] ? c->regs[CAMIN_OUT_SIZE / 4u]
                                                 : c->regs[0];
    uint32_t w = geom & 0x1fffu, h = (geom >> 16) & 0x1fffu;
    bool bgra = (c->regs[CAMIN_FMT / 4u] & CAMIN_FMT_BGRA) != 0u;
    /* 0x48 bit 13 is set for the live preview, which leaves the scaler
     * turned to portrait (304 wide). The post-capture review clears it and
     * keeps the sensor's landscape layout: 0x54's high field is its width. */
    if (bgra && !(c->regs[CAMIN_SCALER / 4u] & CAMIN_SCALER_PORTRAIT)) {
        uint32_t t = w; w = h; h = t;
    }
    uint32_t stride = w * (bgra ? 4u : 2u);
    uint32_t limit = c->regs[CAMIN_PIXELS / 4u];
    if (limit && w && h > limit / w) h = limit / w;
    uint32_t slot = c->frame_index & 3u;
    uint32_t pa = c->regs[(0x18u >> 2) + slot];
    uint8_t *dst = w && h && ram ? ram(ctx, pa, stride * h) : NULL;
    bool portrait = (c->regs[CAMIN_SCALER / 4u] & CAMIN_SCALER_PORTRAIT) != 0u;
    if (dst && bgra) fill_bgra(c, dst, w, h, stride, !portrait);
    else if (dst) fill422(c, dst, w, h, stride);
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
