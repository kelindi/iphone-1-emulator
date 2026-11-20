/* /arm-io/jpeg, the S5L8900 JPEG encoder (VIC 0x32), as driven by 1A543a's
 * AppleJPEGDriver (encode path c0322e40, interrupt handler c03227a4).
 * The driver resets the block (0x78 = 1), points 0x50 at the source frame
 * (the camera input's 4:2:2 still) and 0x58 at the destination, sets the
 * size (0x10 width, 0x14 height) and starts with 0x70 = 1. Completion is
 * status 0x20 bit 3, acknowledged by writing that bit to 0x1C; the driver
 * then reads the encoded length from 0x18 and, if it did not fit, lowers
 * its quality and starts again. The stream written here is a complete
 * baseline JFIF file. */
#include "soc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JPEG_W        0x10u
#define JPEG_H        0x14u
#define JPEG_LEN      0x18u
#define JPEG_ACK      0x1cu
#define JPEG_STATUS   0x20u
#define JPEG_SRC      0x50u
#define JPEG_DST      0x58u
#define JPEG_SAMPLING 0x68u   /* luma H:V factors, 0x21 = 4:2:2 */
#define JPEG_START    0x70u
#define JPEG_RESET    0x78u
#define JPEG_ST_DONE  0x8u
#define JPEG_QUALITY  85
#define JPEG_BUSY_HZ  200u

void s5l_jpeg_reset(s5l_jpeg_t *j) {
    if (!j) return;
    bool trace = j->trace;
    memset(j, 0, sizeof *j);
    j->trace = trace;
}

static unsigned jw(uint32_t off) { return (off >> 2) % (S5L_JPEG_SIZE / 4u); }

uint32_t s5l_jpeg_read(s5l_jpeg_t *j, uint32_t off) {
    uint32_t v = off < S5L_JPEG_SIZE ? j->regs[jw(off)] : 0u;
    j->reads++;
    if (j->trace) fprintf(stderr, "jpeg r %03x = %08x\n", off, v);
    return v;
}

void s5l_jpeg_write(s5l_jpeg_t *j, uint32_t off, uint32_t val,
                    uint32_t tb_hz) {
    j->writes++;
    if (j->trace) fprintf(stderr, "jpeg w %03x = %08x\n", off, val);
    if (off >= S5L_JPEG_SIZE) return;
    if (off == JPEG_ACK) {
        j->regs[jw(JPEG_STATUS)] &= ~val;
        return;
    }
    if (off == JPEG_STATUS || off == JPEG_LEN) return;
    j->regs[jw(off)] = val;
    if (off == JPEG_RESET && (val & 1u)) {
        j->regs[jw(JPEG_STATUS)] = 0u;
        j->regs[jw(JPEG_LEN)] = 0u;
        j->busy_ticks = 0u;
    } else if (off == JPEG_START && (val & 1u)) {
        uint32_t t = tb_hz / JPEG_BUSY_HZ;
        j->busy_ticks = t ? t : 1u;
    }
}

bool s5l_jpeg_irq(const s5l_jpeg_t *j) {
    return j && (j->regs[jw(JPEG_STATUS)] & JPEG_ST_DONE) != 0u;
}

uint32_t s5l_jpeg_ticks_to_event(const s5l_jpeg_t *j) {
    return j ? j->busy_ticks : 0u;
}

/* ---- baseline encoder (ITU T.81 Annex K tables) ---- */

static const uint8_t ZIGZAG[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static const uint8_t QLUM[64] = {
    16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,
    14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,
    18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,
    49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
static const uint8_t QCHR[64] = {
    17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,
    24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
static const uint8_t DC_LUM_BITS[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t DC_CHR_BITS[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t DC_VALS[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t AC_LUM_BITS[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const uint8_t AC_LUM_VALS[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa};
static const uint8_t AC_CHR_BITS[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const uint8_t AC_CHR_VALS[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa};

typedef struct { uint16_t code[256]; uint8_t len[256]; } huff_t;

typedef struct {
    uint8_t *out; size_t cap, n;
    uint32_t acc; int bits;
} bw_t;

static void put8(bw_t *b, uint8_t v) { if (b->n < b->cap) b->out[b->n] = v; b->n++; }
static void put16(bw_t *b, uint16_t v) { put8(b, (uint8_t)(v >> 8)); put8(b, (uint8_t)v); }

static void putbits(bw_t *b, uint32_t code, int len) {
    b->acc = (b->acc << len) | (code & ((1u << len) - 1u));
    b->bits += len;
    while (b->bits >= 8) {
        uint8_t byte = (uint8_t)(b->acc >> (b->bits - 8));
        put8(b, byte);
        if (byte == 0xff) put8(b, 0);
        b->bits -= 8;
    }
}

static void build_huff(huff_t *h, const uint8_t bits[16], const uint8_t *vals) {
    uint16_t code = 0; unsigned k = 0;
    memset(h, 0, sizeof *h);
    for (int l = 1; l <= 16; l++) {
        for (unsigned i = 0; i < bits[l - 1]; i++, k++) {
            h->code[vals[k]] = code++;
            h->len[vals[k]] = (uint8_t)l;
        }
        code <<= 1;
    }
}

static void scale_q(uint8_t out[64], const uint8_t base[64], int quality) {
    int s = quality < 50 ? 5000 / quality : 200 - quality * 2;
    for (int i = 0; i < 64; i++) {
        int q = (base[i] * s + 50) / 100;
        out[i] = (uint8_t)(q < 1 ? 1 : q > 255 ? 255 : q);
    }
}

static float COS[8][8];
static bool cos_ready;

static void fdct(const float in[64], float out[64]) {
    if (!cos_ready) {
        for (int u = 0; u < 8; u++)
            for (int x = 0; x < 8; x++)
                COS[u][x] = (float)(cos((2 * x + 1) * u * M_PI / 16.0) *
                                    (u ? 0.5 : 0.5 / sqrt(2.0)));
        cos_ready = true;
    }
    float tmp[64];
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {
            float s = 0.f;
            for (int x = 0; x < 8; x++) s += in[y * 8 + x] * COS[u][x];
            tmp[y * 8 + u] = s;
        }
    for (int u = 0; u < 8; u++)
        for (int v = 0; v < 8; v++) {
            float s = 0.f;
            for (int y = 0; y < 8; y++) s += tmp[y * 8 + u] * COS[v][y];
            out[v * 8 + u] = s;
        }
}

static void encode_block(bw_t *b, const float blk[64], const uint8_t q[64],
                         int *dc, const huff_t *hdc, const huff_t *hac) {
    float co[64];
    int zz[64];
    fdct(blk, co);
    for (int i = 0; i < 64; i++) {
        float v = co[ZIGZAG[i]] / q[ZIGZAG[i]];
        zz[i] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    }
    int diff = zz[0] - *dc;
    *dc = zz[0];
    int a = diff < 0 ? -diff : diff, nb = 0;
    while (a) { nb++; a >>= 1; }
    putbits(b, hdc->code[nb], hdc->len[nb]);
    if (nb) putbits(b, (uint32_t)(diff < 0 ? diff - 1 : diff), nb);
    int run = 0;
    for (int i = 1; i < 64; i++) {
        if (!zz[i]) { run++; continue; }
        while (run > 15) { putbits(b, hac->code[0xf0], hac->len[0xf0]); run -= 16; }
        int v = zz[i]; a = v < 0 ? -v : v; nb = 0;
        while (a) { nb++; a >>= 1; }
        int sym = (run << 4) | nb;
        putbits(b, hac->code[sym], hac->len[sym]);
        putbits(b, (uint32_t)(v < 0 ? v - 1 : v), nb);
        run = 0;
    }
    if (run) putbits(b, hac->code[0], hac->len[0]);
}

static void put_dht(bw_t *b, uint8_t cls_id, const uint8_t bits[16],
                    const uint8_t *vals) {
    unsigned n = 0;
    for (int i = 0; i < 16; i++) n += bits[i];
    put16(b, 0xffc4); put16(b, (uint16_t)(3u + 16u + n)); put8(b, cls_id);
    for (int i = 0; i < 16; i++) put8(b, bits[i]);
    for (unsigned i = 0; i < n; i++) put8(b, vals[i]);
}

/* Source pixel as YCbCr from a '2vuy' (Cb Y0 Cr Y1) frame, edge-clamped. */
static inline void ycc422(const uint8_t *src, uint32_t stride, uint32_t w,
                          uint32_t h, uint32_t x, uint32_t y, float *yv,
                          float *cb, float *cr) {
    if (x >= w) x = w - 1u;
    if (y >= h) y = h - 1u;
    const uint8_t *p = src + (size_t)y * stride + 4u * (x / 2u);
    *cb = (float)p[0] - 128.f;
    *yv = (float)p[(x & 1u) ? 3 : 1];
    *cr = (float)p[2] - 128.f;
}

size_t s5l_jpeg_encode_422(const uint8_t *src, uint32_t w, uint32_t h,
                           uint32_t stride, int quality, uint32_t sampling,
                           uint8_t *out, size_t cap) {
    uint32_t hs = (sampling >> 4) & 0xfu, vs = sampling & 0xfu;
    if (hs < 1u || hs > 2u) hs = 2u;
    if (vs < 1u || vs > 2u) vs = 1u;
    if (!src || !w || !h || w > 0xffffu || h > 0xffffu) return 0u;
    bw_t b = { out, cap, 0u, 0u, 0 };
    uint8_t ql[64], qc[64];
    scale_q(ql, QLUM, quality);
    scale_q(qc, QCHR, quality);
    huff_t dcl, dcc, acl, acc;
    build_huff(&dcl, DC_LUM_BITS, DC_VALS);
    build_huff(&dcc, DC_CHR_BITS, DC_VALS);
    build_huff(&acl, AC_LUM_BITS, AC_LUM_VALS);
    build_huff(&acc, AC_CHR_BITS, AC_CHR_VALS);

    put16(&b, 0xffd8);
    put16(&b, 0xffe0); put16(&b, 16);
    put8(&b, 'J'); put8(&b, 'F'); put8(&b, 'I'); put8(&b, 'F'); put8(&b, 0);
    put16(&b, 0x0101); put8(&b, 0); put16(&b, 1); put16(&b, 1);
    put8(&b, 0); put8(&b, 0);
    put16(&b, 0xffdb); put16(&b, 132);
    put8(&b, 0); for (int i = 0; i < 64; i++) put8(&b, ql[ZIGZAG[i]]);
    put8(&b, 1); for (int i = 0; i < 64; i++) put8(&b, qc[ZIGZAG[i]]);
    put16(&b, 0xffc0); put16(&b, 17); put8(&b, 8);
    put16(&b, (uint16_t)h); put16(&b, (uint16_t)w); put8(&b, 3);
    put8(&b, 1); put8(&b, (uint8_t)((hs << 4) | vs)); put8(&b, 0);
    put8(&b, 2); put8(&b, 0x11); put8(&b, 1);
    put8(&b, 3); put8(&b, 0x11); put8(&b, 1);
    put_dht(&b, 0x00, DC_LUM_BITS, DC_VALS);
    put_dht(&b, 0x10, AC_LUM_BITS, AC_LUM_VALS);
    put_dht(&b, 0x01, DC_CHR_BITS, DC_VALS);
    put_dht(&b, 0x11, AC_CHR_BITS, AC_CHR_VALS);
    put16(&b, 0xffda); put16(&b, 12); put8(&b, 3);
    put8(&b, 1); put8(&b, 0x00); put8(&b, 2); put8(&b, 0x11);
    put8(&b, 3); put8(&b, 0x11);
    put8(&b, 0); put8(&b, 63); put8(&b, 0);

    int dcy = 0, dcb = 0, dcr = 0;
    float Y[4][64], Cb[64], Cr[64];
    const uint32_t mw = 8u * hs, mh = 8u * vs;
    const float share = 1.f / (float)(hs * vs);
    for (uint32_t my = 0; my < h; my += mh)
        for (uint32_t mx = 0; mx < w; mx += mw) {
            memset(Cb, 0, sizeof Cb); memset(Cr, 0, sizeof Cr);
            for (uint32_t yy = 0; yy < mh; yy++)
                for (uint32_t xx = 0; xx < mw; xx++) {
                    float yv, cb, cr;
                    ycc422(src, stride, w, h, mx + xx, my + yy, &yv, &cb, &cr);
                    unsigned k = (yy / 8u) * hs + xx / 8u;
                    Y[k][(yy % 8u) * 8u + xx % 8u] = yv - 128.f;
                    unsigned c = (yy / vs) * 8u + xx / hs;
                    Cb[c] += share * cb;
                    Cr[c] += share * cr;
                }
            for (uint32_t k = 0; k < hs * vs; k++)
                encode_block(&b, Y[k], ql, &dcy, &dcl, &acl);
            encode_block(&b, Cb, qc, &dcb, &dcc, &acc);
            encode_block(&b, Cr, qc, &dcr, &dcc, &acc);
        }
    if (b.bits) putbits(&b, 0x7fu, 8 - b.bits);
    put16(&b, 0xffd9);
    return b.n;
}

bool s5l_jpeg_tick(s5l_jpeg_t *j, uint32_t tb, s5l_camin_ram_fn ram,
                   void *ctx) {
    if (!j || !j->busy_ticks) return s5l_jpeg_irq(j);
    if (tb < j->busy_ticks) { j->busy_ticks -= tb; return s5l_jpeg_irq(j); }
    j->busy_ticks = 0u;
    uint32_t w = j->regs[jw(JPEG_W)], h = j->regs[jw(JPEG_H)];
    uint32_t stride = w * 2u;
    const uint8_t *src = w && h && ram ? ram(ctx, j->regs[jw(JPEG_SRC)], stride * h) : NULL;
    size_t len = 0u;
    if (src) {
        size_t cap = 0u;
        uint8_t *dst = NULL;
        for (uint32_t c = S5L8900_EDRAM_SIZE; c >= 0x1000u && !dst; c >>= 1)
            if ((dst = ram(ctx, j->regs[jw(JPEG_DST)], c)) != NULL) cap = c;
        len = s5l_jpeg_encode_422(src, w, h, stride, JPEG_QUALITY,
                                  j->regs[jw(JPEG_SAMPLING)], dst, cap);
        const char *dump = getenv("S5LBOX_JPEG_DUMP");
        if (dump && *dump && dst && len <= cap) {
            FILE *f = fopen(dump, "wb");
            if (f) { fwrite(dst, 1, len, f); fclose(f); }
        }
    }
    j->regs[jw(JPEG_LEN)] = (uint32_t)len;
    j->regs[jw(JPEG_STATUS)] |= JPEG_ST_DONE;
    j->encodes++;
    if (j->trace)
        fprintf(stderr, "jpeg encode %ux%u src %08x dst %08x -> %zu bytes%s\n",
                w, h, j->regs[jw(JPEG_SRC)], j->regs[jw(JPEG_DST)], len,
                src ? "" : " (no source)");
    return true;
}
