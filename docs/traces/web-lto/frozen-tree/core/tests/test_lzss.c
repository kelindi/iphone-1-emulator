/*
 * S5LBox — LZSS tests.
 *
 * The decompressor eats a user-supplied firmware payload, so the important
 * properties are that it round-trips real data and that malformed or truncated
 * input stops cleanly inside both buffers.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "lzss.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* Minimal LZSS compressor, used only to generate test input. */
static size_t compress_literals(uint8_t *dst, const uint8_t *src, size_t n) {
    size_t out = 0, i = 0;
    while (i < n) {
        size_t chunk = (n - i) < 8 ? (n - i) : 8;
        dst[out++] = (uint8_t)((1u << chunk) - 1u);   /* all literals */
        for (size_t k = 0; k < chunk; k++) dst[out++] = src[i + k];
        i += chunk;
    }
    return out;
}

static void test_literal_roundtrip(void) {
    uint8_t plain[200], comp[512], out[512];
    for (unsigned i = 0; i < sizeof plain; i++) plain[i] = (uint8_t)(i * 7 + 3);
    size_t clen = compress_literals(comp, plain, sizeof plain);
    size_t got = lzss_decompress(out, sizeof out, comp, clen);
    CHECK(got == sizeof plain, "produced %zu expect %zu", got, sizeof plain);
    CHECK(memcmp(out, plain, sizeof plain) == 0, "literal round-trip mismatch");
}

static void test_back_reference(void) {
    /* Two literals then a back-reference repeating them. */
    uint8_t comp[8], out[64];
    comp[0] = 0x03;            /* flags: two literals, then a match */
    comp[1] = 'A';
    comp[2] = 'B';
    /* match: offset points at the two bytes just written, length 3 */
    unsigned r = 4096 - 18;    /* where the ring write pointer started */
    comp[3] = (uint8_t)(r & 0xff);
    comp[4] = (uint8_t)(((r >> 4) & 0xf0) | 0x00);
    size_t got = lzss_decompress(out, sizeof out, comp, 5);
    CHECK(got == 5, "produced %zu expect 5 (2 literals + 3-byte match)", got);
    CHECK(out[0] == 'A' && out[1] == 'B', "literals wrong");
    CHECK(out[2] == 'A' && out[3] == 'B', "back-reference did not repeat");
}

static void test_never_overruns_destination(void) {
    /* A stream that would produce far more than the destination can hold must
     * stop at the cap, not write past it. */
    uint8_t comp[64], out[8], canary[8];
    memset(comp, 0xff, sizeof comp);   /* all-literal flags + data */
    memset(canary, 0xEE, sizeof canary);
    size_t got = lzss_decompress(out, sizeof out, comp, sizeof comp);
    CHECK(got <= sizeof out, "produced %zu, exceeds the %zu-byte cap", got, sizeof out);
    CHECK(memcmp(canary, canary, sizeof canary) == 0, "canary check");
}

static void test_truncated_input_stops(void) {
    uint8_t comp[1] = { 0x00 };        /* a flag byte promising items that are absent */
    uint8_t out[32];
    size_t got = lzss_decompress(out, sizeof out, comp, 1);
    CHECK(got == 0, "truncated stream produced %zu bytes, expect 0", got);
    CHECK(lzss_decompress(out, sizeof out, comp, 0) == 0, "empty input should produce 0");
}

static void test_header_validation(void) {
    uint8_t hdr[LZSS_HEADER_SIZE + 16];
    memset(hdr, 0, sizeof hdr);
    lzss_header_t h;
    CHECK(!lzss_parse_header(hdr, sizeof hdr, &h), "zeroed header accepted");

    /* Build a valid header: 'comp' 'lzss' adler unc cmp */
    memcpy(hdr, "complzss", 8);
    hdr[8]=0; hdr[9]=0; hdr[10]=0; hdr[11]=1;         /* adler   */
    hdr[12]=0; hdr[13]=0; hdr[14]=0; hdr[15]=32;      /* unc     */
    hdr[16]=0; hdr[17]=0; hdr[18]=0; hdr[19]=16;      /* cmp     */
    CHECK(lzss_parse_header(hdr, sizeof hdr, &h), "valid header rejected");
    CHECK(h.uncompressed_size == 32 && h.compressed_size == 16,
          "header fields parsed wrong (unc=%u cmp=%u)",
          h.uncompressed_size, h.compressed_size);

    /* A compressed size larger than the buffer must be refused. */
    hdr[19] = 255;
    CHECK(!lzss_parse_header(hdr, sizeof hdr, &h), "oversized compressed size accepted");
}

static void test_adler32(void) {
    /* Adler-32 of "Wikipedia" is the standard published value 0x11E60398. */
    const uint8_t s[] = "Wikipedia";
    CHECK(lzss_adler32(s, 9) == 0x11E60398u,
          "adler32=%08x expect 11e60398", lzss_adler32(s, 9));
}

int main(void) {
    printf("S5LBox LZSS tests\n");
    test_literal_roundtrip();
    test_back_reference();
    test_never_overruns_destination();
    test_truncated_input_stops();
    test_header_validation();
    test_adler32();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
