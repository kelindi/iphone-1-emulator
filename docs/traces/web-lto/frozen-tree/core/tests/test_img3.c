/*
 * S5LBox — IMG3 parser tests.
 *
 * Builds IMG3 containers in memory (we ship no Apple firmware) and checks both
 * correct parsing and, importantly, that malformed containers are rejected
 * rather than read out of bounds — this parser is the first thing to touch
 * untrusted user-supplied files.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "img3.h"
#include "aes.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* -------------------------------------------------------- image building */
static uint8_t  g_buf[4096];
static uint32_t g_len;

static void put32(uint32_t off, uint32_t v) {
    g_buf[off] = (uint8_t)v; g_buf[off+1] = (uint8_t)(v >> 8);
    g_buf[off+2] = (uint8_t)(v >> 16); g_buf[off+3] = (uint8_t)(v >> 24);
}

/* Start a container with the given ident (e.g. 'ibot' as 0x69626f74). */
static void img_begin(uint32_t ident) {
    memset(g_buf, 0, sizeof g_buf);
    put32(0, IMG3_MAGIC);   /* bytes 33 67 6d 49 == "3gmI" */      /* "3gmI" on disk */
    put32(16, ident);
    g_len = 20;
}

/* Append a tag with `data_len` bytes of payload copied from `data`. */
static uint32_t img_tag(uint32_t magic, const void *data, uint32_t data_len) {
    uint32_t off = g_len;
    uint32_t total = 12 + data_len;
    put32(off, magic); put32(off + 4, total); put32(off + 8, data_len);
    if (data && data_len) memcpy(&g_buf[off + 12], data, data_len);
    g_len += total;
    return off;
}

static void img_finish(void) {
    put32(4, g_len);            /* fullSize   */
    put32(8, g_len - 20);       /* dataSize   */
    put32(12, g_len - 20);      /* signedSize */
}

/* -------------------------------------------------------------- the tests */

static void test_parse_minimal(void) {
    const char payload[] = "IBOOTPAYLOAD";
    img_begin(0x69626f74u);                       /* 'ibot' */
    img_tag(IMG3_TAG_DATA, payload, (uint32_t)strlen(payload));
    img_finish();

    img3_t img;
    img3_status_t st = img3_parse(g_buf, g_len, &img);
    CHECK(st == IMG3_OK, "status=%s", img3_strerror(st));
    CHECK(img.data != NULL && img.data_len == strlen(payload),
          "data_len=%u expect %u", img.data_len, (unsigned)strlen(payload));
    CHECK(img.data && memcmp(img.data, payload, strlen(payload)) == 0,
          "payload mismatch");

    char id[5]; img3_ident_str(img.ident, id);
    CHECK(strcmp(id, "ibot") == 0, "ident=\"%s\" expect ibot", id);
}

static void test_parse_kbag_and_vers(void) {
    uint8_t kbag[24 + 16];
    memset(kbag, 0, sizeof kbag);
    kbag[0] = 1;                                  /* cryptState = 1 (production) */
    kbag[4] = 128;                                /* keyBits = 128 */
    for (int i = 0; i < 16; i++) kbag[8 + i]  = (uint8_t)(0xa0 + i);   /* IV  */
    for (int i = 0; i < 16; i++) kbag[24 + i] = (uint8_t)(0xb0 + i);   /* key */

    uint8_t vers[4 + 5];
    memset(vers, 0, sizeof vers);
    vers[0] = 5;                                  /* length prefix */
    memcpy(&vers[4], "3.1.3", 5);

    img_begin(0x6b726e6cu);                       /* 'krnl' */
    img_tag(IMG3_TAG_VERS, vers, sizeof vers);
    img_tag(IMG3_TAG_KBAG, kbag, sizeof kbag);
    img_tag(IMG3_TAG_DATA, "xxxx", 4);
    img_tag(IMG3_TAG_SHSH, "sig", 3);
    img_finish();

    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_OK, "parse failed");
    CHECK(img.kbag.present, "kbag not parsed");
    CHECK(img.kbag.crypt_state == 1, "crypt_state=%u expect 1", img.kbag.crypt_state);
    CHECK(img.kbag.key_bits == 128, "key_bits=%u expect 128", img.kbag.key_bits);
    CHECK(img.kbag.iv[0] == 0xa0 && img.kbag.iv[15] == 0xaf, "IV mismatch");
    CHECK(img.kbag.key[0] == 0xb0 && img.kbag.key[15] == 0xbf, "key mismatch");
    CHECK(img.has_shsh, "SHSH not noticed");
    CHECK(strcmp(img.vers, "3.1.3") == 0, "vers=\"%s\" expect 3.1.3", img.vers);
    CHECK(img.data_len == 4, "data_len=%u expect 4", img.data_len);
}

/* --- malformed inputs must be rejected, not read out of bounds ----------- */

static void test_reject_too_small(void) {
    img3_t img;
    CHECK(img3_parse(g_buf, 4, &img) == IMG3_ERR_TOO_SMALL, "short buffer accepted");
    CHECK(img3_parse(NULL, 0, &img) == IMG3_ERR_TOO_SMALL, "NULL accepted");
}

static void test_reject_bad_magic(void) {
    img_begin(0x69626f74u);
    put32(0, 0xdeadbeefu);
    img_finish();
    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_ERR_BAD_MAGIC, "bad magic accepted");
}

static void test_reject_oversized_fullsize(void) {
    /* fullSize claims far more than the buffer actually holds. */
    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_DATA, "abcd", 4);
    img_finish();
    put32(4, 0xfffffff0u);
    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_ERR_BAD_SIZE,
          "oversized fullSize accepted");
}

static void test_reject_tag_past_end(void) {
    /* A tag whose total length runs beyond the container. */
    img_begin(0x69626f74u);
    uint32_t off = img_tag(IMG3_TAG_DATA, "abcd", 4);
    img_finish();
    put32(off + 4, 0x10000u);                     /* absurd tag length */
    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_ERR_BAD_TAG, "overlong tag accepted");
}

static void test_reject_tag_data_exceeds_tag(void) {
    /* dataLen larger than the tag that contains it. */
    img_begin(0x69626f74u);
    uint32_t off = img_tag(IMG3_TAG_DATA, "abcd", 4);
    img_finish();
    put32(off + 8, 0x1000u);
    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_ERR_BAD_TAG,
          "tag with oversized dataLen accepted");
}

static void test_reject_zero_length_tag(void) {
    /* A tag claiming zero total length would loop forever if not rejected. */
    img_begin(0x69626f74u);
    uint32_t off = img_tag(IMG3_TAG_DATA, "abcd", 4);
    img_finish();
    put32(off + 4, 0);
    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_ERR_BAD_TAG,
          "zero-length tag accepted (infinite-loop hazard)");
}

static void test_reject_partial_trailing_tag(void) {
    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_DATA, "abcd", 4);
    g_buf[g_len++] = 0xAA;                       /* less than a tag header */
    img_finish();
    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_ERR_BAD_TAG,
          "partial trailing tag accepted");
}

static void test_truncated_kbag_ignored(void) {
    /* A KBAG too short to hold its own fields must simply not be marked
     * present, rather than reading past the tag. */
    uint8_t k[8] = {1,0,0,0, 128,0,0,0};
    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_KBAG, k, sizeof k);
    img_tag(IMG3_TAG_DATA, "abcd", 4);
    img_finish();
    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_OK, "parse failed");
    CHECK(!img.kbag.present, "truncated kbag should not be marked present");
}

/* Build an encrypted image the way Apple does, then decrypt it back — the
 * exact operation M3 performs on real firmware. */
static void test_decrypt_data_roundtrip(void) {
    static const uint8_t key[16] = {
        0x0f,0x1e,0x2d,0x3c,0x4b,0x5a,0x69,0x78,
        0x87,0x96,0xa5,0xb4,0xc3,0xd2,0xe1,0xf0
    };
    uint8_t iv[16];
    for (unsigned i = 0; i < 16; i++) iv[i] = (uint8_t)(0x10 + i);

    /* 32 bytes of "firmware" + a 5-byte unaligned tail. */
    uint8_t plain[37];
    for (unsigned i = 0; i < sizeof plain; i++) plain[i] = (uint8_t)(i * 3 + 1);

    uint8_t cipher[sizeof plain];
    aes_ctx_t ctx;
    aes_init(&ctx, key, 128);
    aes_cbc_encrypt(&ctx, iv, plain, cipher, 32);
    memcpy(cipher + 32, plain + 32, 5);            /* tail passes through */

    uint8_t kbag[24 + 16];
    memset(kbag, 0, sizeof kbag);
    kbag[0] = 1; kbag[4] = 128;
    memcpy(&kbag[8], iv, 16);

    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_KBAG, kbag, sizeof kbag);
    img_tag(IMG3_TAG_DATA, cipher, (uint32_t)sizeof cipher);
    img_finish();

    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_OK, "parse failed");

    uint8_t out[sizeof plain];
    uint32_t out_len = 0;
    CHECK(img3_decrypt_data_iv(&img, key, 128, iv, out, sizeof out, &out_len),
          "decrypt failed");
    CHECK(out_len == sizeof plain, "out_len=%u expect %u", out_len, (unsigned)sizeof plain);
    CHECK(memcmp(out, plain, sizeof plain) == 0, "decrypted payload mismatch");
}

static void test_decrypt_requires_kbag(void) {
    static const uint8_t key[16] = {0};
    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_DATA, "0123456789abcdef", 16);
    img_finish();
    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_OK, "parse failed");
    uint8_t out[16];
    uint8_t iv[16] = {0};
    CHECK(!img3_decrypt_data_iv(&img, key, 128, iv, out, sizeof out, NULL),
          "decrypt without a KBAG should fail");
}

static void test_decrypt_checks_output_capacity(void) {
    static const uint8_t key[16] = {0};
    uint8_t iv[16] = {0}, kbag[40] = {0}, out[16];
    kbag[0] = 1; kbag[4] = 128;
    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_KBAG, kbag, sizeof kbag);
    img_tag(IMG3_TAG_DATA, "0123456789abcdef", 16);
    img_finish();

    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_OK, "parse failed");
    memset(out, 0xEE, sizeof out);
    CHECK(!img3_decrypt_data_iv(&img, key, 128, iv, out, sizeof out - 1u, NULL),
          "undersized output buffer accepted");
    bool untouched = true;
    for (unsigned i = 0; i < sizeof out; i++)
        if (out[i] != 0xEE) untouched = false;
    CHECK(untouched, "capacity failure modified the output buffer");

    CHECK(!img3_decrypt_data_iv(&img, key, 256, iv, out, sizeof out, NULL),
          "key size inconsistent with KBAG was accepted");
}

static void test_null_output_struct_is_refused(void) {
    CHECK(img3_parse(g_buf, g_len, NULL) == IMG3_ERR_INVALID_ARGUMENT,
          "NULL output struct was dereferenced or accepted");
}

/* --- regressions from the adversarial audit ----------------------------- */

static void test_magic_is_literal_3gmI(void) {
    /* Asserted against raw bytes, not the constant, so a future byte-swap of
     * IMG3_MAGIC fails loudly instead of being mirrored by the fixtures. A real
     * Apple image begins with the ASCII bytes "3gmI". */
    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_DATA, "X", 1);
    img_finish();
    CHECK(memcmp(g_buf, "3gmI", 4) == 0, "header bytes must be ASCII 3gmI");

    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_OK, "real-layout header must parse");

    memcpy(g_buf, "Img3", 4);                    /* the byte-swapped form */
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_ERR_BAD_MAGIC,
          "byte-swapped magic must be rejected");
}

/* Build a KBAG declaring `key_bits` with `n` bytes of key material, parse it
 * behind a canary, and check nothing outside kbag.key was touched. */
static void kbag_case(uint32_t key_bits, uint32_t n, bool expect_present) {
    static uint8_t k[24 + 512];
    memset(k, 0, sizeof k);
    k[0] = 1;
    k[4] = (uint8_t)key_bits;        k[5] = (uint8_t)(key_bits >> 8);
    k[6] = (uint8_t)(key_bits >> 16);k[7] = (uint8_t)(key_bits >> 24);
    for (uint32_t i = 0; i < 16; i++) k[8 + i]  = (uint8_t)(0xa0 + i);
    for (uint32_t i = 0; i < n;  i++) k[24 + i] = (uint8_t)(0xb0 + i);

    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_KBAG, k, 24 + n);
    img_tag(IMG3_TAG_DATA, "abcd", 4);
    img_finish();

    struct { img3_t img; uint8_t canary[256]; } w;
    memset(&w, 0xEE, sizeof w);

    CHECK(img3_parse(g_buf, g_len, &w.img) == IMG3_OK,
          "keyBits=%u n=%u: parse failed", key_bits, n);
    CHECK(w.img.kbag.present == expect_present,
          "keyBits=%u n=%u: present=%d expect %d",
          key_bits, n, (int)w.img.kbag.present, (int)expect_present);

    unsigned dirty = 0;
    for (unsigned i = 0; i < sizeof w.canary; i++)
        if (w.canary[i] != 0xEE) dirty++;
    CHECK(dirty == 0, "keyBits=%u n=%u: %u bytes clobbered past the img3_t",
          key_bits, n, dirty);
}

static void test_kbag_192_and_256_accepted(void) {
    kbag_case(192, 24, true);
    kbag_case(256, 32, true);            /* exactly fills key[32] */
}

static void test_kbag_absurd_keybits_rejected(void) {
    /* The only thing protecting the fixed 32-byte key field is one bounds
     * check on an attacker-controlled length. Pin it. */
    kbag_case(0,     0,   false);
    kbag_case(127,  16,   false);
    kbag_case(129,  17,   false);
    kbag_case(264,  33,  false);         /* one byte past the field */
    kbag_case(512,  64,  false);
    kbag_case(4096, 512, false);
    kbag_case(0xffffffffu, 16, false);   /* must not wrap */
}

static void test_malformed_kbag_is_not_treated_as_plaintext(void) {
    /* A KBAG we cannot understand means "encrypted, method unknown". It must
     * not be silently downgraded to "not encrypted". */
    uint8_t k[8] = {1,0,0,0, 128,0,0,0};   /* too short to hold IV or key */
    img_begin(0x69626f74u);
    img_tag(IMG3_TAG_KBAG, k, sizeof k);
    img_tag(IMG3_TAG_DATA, "abcd", 4);
    img_finish();

    img3_t img;
    CHECK(img3_parse(g_buf, g_len, &img) == IMG3_OK, "parse failed");
    CHECK(!img.kbag.present, "unparseable KBAG should not be present");
    CHECK(img.kbag.malformed, "unparseable KBAG should be flagged malformed");
}

static void test_long_vers_is_clamped(void) {
    /* The version string is copied into a fixed 64-byte field from an
     * attacker-controlled length; the clamp was previously never exercised. */
    uint8_t vers[4 + 200];
    memset(vers, 0, sizeof vers);
    vers[0] = 200;                                  /* declared length */
    for (unsigned i = 0; i < 200; i++) vers[4 + i] = 'A';

    img_begin(0x6b726e6cu);
    img_tag(IMG3_TAG_VERS, vers, sizeof vers);
    img_tag(IMG3_TAG_DATA, "abcd", 4);
    img_finish();

    struct { img3_t img; uint8_t canary[64]; } w;
    memset(&w, 0xEE, sizeof w);
    CHECK(img3_parse(g_buf, g_len, &w.img) == IMG3_OK, "parse failed");
    CHECK(strlen(w.img.vers) < sizeof w.img.vers,
          "vers must stay NUL-terminated inside its field (len=%u)",
          (unsigned)strlen(w.img.vers));
    unsigned dirty = 0;
    for (unsigned i = 0; i < sizeof w.canary; i++)
        if (w.canary[i] != 0xEE) dirty++;
    CHECK(dirty == 0, "%u bytes clobbered past the img3_t by a long VERS", dirty);
}

int main(void) {
    printf("S5LBox IMG3 parser tests\n");
    test_parse_minimal();
    test_parse_kbag_and_vers();
    test_reject_too_small();
    test_reject_bad_magic();
    test_reject_oversized_fullsize();
    test_reject_tag_past_end();
    test_reject_tag_data_exceeds_tag();
    test_reject_zero_length_tag();
    test_reject_partial_trailing_tag();
    test_truncated_kbag_ignored();
    test_decrypt_data_roundtrip();
    test_decrypt_requires_kbag();
    test_decrypt_checks_output_capacity();
    test_null_output_struct_is_refused();
    test_magic_is_literal_3gmI();
    test_kbag_192_and_256_accepted();
    test_kbag_absurd_keybits_rejected();
    test_malformed_kbag_is_not_treated_as_plaintext();
    test_long_vers_is_clamped();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
