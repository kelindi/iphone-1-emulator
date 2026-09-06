/*
 * S5LBox — NAND device tests.
 *
 * The important property here is that the model behaves like real NAND rather
 * than like RAM: erased pages read as all ones, programming can only clear
 * bits, and out-of-range or bad-block access is refused. Getting that wrong
 * produces an emulator that works until it meets real firmware.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "nand.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* A small device: 64-byte pages, 8 bytes spare, 4 pages/block, 4 blocks. */
static bool small(nand_t *n) { return nand_init(n, 64, 8, 4, 4); }

static bool copy_prefix(const char *src, const char *dst, size_t keep) {
    FILE *in = fopen(src, "rb"), *out = fopen(dst, "wb");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return false; }
    bool ok = true;
    while (keep) {
        uint8_t buf[128];
        size_t want = keep < sizeof buf ? keep : sizeof buf;
        size_t got = fread(buf, 1, want, in);
        if (got != want || fwrite(buf, 1, got, out) != got) { ok = false; break; }
        keep -= got;
    }
    if (fclose(in) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    return ok;
}

static bool flip_file_byte(const char *path, long off) {
    FILE *f = fopen(path, "r+b");
    if (!f || fseek(f, off, SEEK_SET) != 0) { if (f) fclose(f); return false; }
    int c = fgetc(f);
    if (c == EOF || fseek(f, off, SEEK_SET) != 0) { fclose(f); return false; }
    bool ok = fputc(c ^ 0x80, f) != EOF;
    if (fclose(f) != 0) ok = false;
    return ok;
}

static bool append_file_byte(const char *path, uint8_t byte) {
    FILE *f = fopen(path, "ab");
    if (!f) return false;
    bool ok = fputc(byte, f) != EOF;
    if (fclose(f) != 0) ok = false;
    return ok;
}

static uint64_t storage_hash(const uint8_t *p, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

/* Rewrite a checksum-bearing small test image after an intentional semantic
 * corruption, so the loader must reach field validation rather than failing
 * at the checksum first. */
static bool refresh_storage_hash(const char *path, size_t payload_off) {
    uint8_t buf[2048];
    FILE *f = fopen(path, "r+b");
    if (!f || fseek(f, 0, SEEK_END) != 0) { if (f) fclose(f); return false; }
    long end = ftell(f);
    if (end < 0 || (size_t)end > sizeof buf ||
        (size_t)end < payload_off + 8u || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f); return false;
    }
    size_t len = (size_t)end;
    if (fread(buf, 1, len, f) != len) { fclose(f); return false; }
    uint64_t h = storage_hash(buf + payload_off, len - payload_off - 8u);
    for (unsigned i = 0; i < 8; i++) buf[len - 8u + i] = (uint8_t)(h >> (8u * i));
    bool ok = fseek(f, 0, SEEK_SET) == 0 && fwrite(buf, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    return ok;
}

static void test_erased_reads_all_ones(void) {
    nand_t n; CHECK(small(&n), "init failed");
    uint8_t buf[64], spare[8];
    CHECK(nand_read_page(&n, 0, buf, spare) == NAND_OK, "read failed");
    bool ones = true;
    for (unsigned i = 0; i < 64; i++) if (buf[i] != 0xff) ones = false;
    for (unsigned i = 0; i < 8; i++)  if (spare[i] != 0xff) ones = false;
    CHECK(ones, "erased NAND should read as all ones");
    nand_free(&n);
}

static void test_program_and_read_back(void) {
    nand_t n; small(&n);
    uint8_t page[64], spare[8], back[64], bspare[8];
    for (unsigned i = 0; i < 64; i++) page[i] = (uint8_t)i;
    for (unsigned i = 0; i < 8; i++)  spare[i] = (uint8_t)(0xa0 + i);

    CHECK(nand_program_page(&n, 3, page, spare) == NAND_OK, "program failed");
    CHECK(nand_read_page(&n, 3, back, bspare) == NAND_OK, "read failed");
    CHECK(memcmp(page, back, 64) == 0, "data did not round-trip");
    CHECK(memcmp(spare, bspare, 8) == 0, "spare did not round-trip");
    nand_free(&n);
}

static void test_program_can_only_clear_bits(void) {
    /* The defining property of NAND. Writing 0xFF over 0x00 must be refused,
     * not silently "work" the way it would on RAM. */
    nand_t n; small(&n);
    uint8_t zeros[64], ones[64], spare[8];
    memset(zeros, 0x00, sizeof zeros);
    memset(ones,  0xff, sizeof ones);
    memset(spare, 0xff, sizeof spare);

    CHECK(nand_program_page(&n, 0, zeros, spare) == NAND_OK, "first program failed");
    CHECK(nand_program_page(&n, 0, ones, spare) == NAND_ERR_NOT_ERASED,
          "programming 1s over 0s should be refused");

    /* Clearing further bits in an already-programmed page is legal. */
    uint8_t half[64];
    memset(half, 0x00, sizeof half);
    CHECK(nand_program_page(&n, 0, half, spare) == NAND_OK,
          "re-clearing already-zero bits should be allowed");
    nand_free(&n);
}

static void test_erase_restores_ones(void) {
    nand_t n; small(&n);
    uint8_t zeros[64], back[64], spare[8];
    memset(zeros, 0x00, sizeof zeros);
    memset(spare, 0x00, sizeof spare);
    nand_program_page(&n, 5, zeros, spare);        /* page 5 is in block 1 */

    CHECK(nand_erase_block(&n, 1) == NAND_OK, "erase failed");
    CHECK(nand_read_page(&n, 5, back, NULL) == NAND_OK, "read failed");
    bool ones = true;
    for (unsigned i = 0; i < 64; i++) if (back[i] != 0xff) ones = false;
    CHECK(ones, "erase should restore all ones");

    /* Erasing block 1 must not have touched block 0. */
    nand_program_page(&n, 0, zeros, NULL);
    nand_erase_block(&n, 1);
    nand_read_page(&n, 0, back, NULL);
    CHECK(back[0] == 0x00, "erasing block 1 disturbed block 0");
    nand_free(&n);
}

static void test_range_and_bad_blocks(void) {
    nand_t n; small(&n);
    uint8_t buf[64];
    CHECK(nand_total_pages(&n) == 16, "total pages=%u expect 16", nand_total_pages(&n));
    CHECK(nand_read_page(&n, 16, buf, NULL) == NAND_ERR_RANGE, "page 16 accepted");
    CHECK(nand_read_page(&n, 0xffffffffu, buf, NULL) == NAND_ERR_RANGE,
          "huge page index accepted");
    CHECK(nand_erase_block(&n, 4) == NAND_ERR_RANGE, "block 4 accepted");

    nand_mark_bad(&n, 2);
    CHECK(nand_is_bad(&n, 2), "block 2 should be bad");
    CHECK(!nand_is_bad(&n, 1), "block 1 wrongly bad");
    memset(buf, 0, sizeof buf);
    CHECK(nand_program_page(&n, 8, buf, NULL) == NAND_ERR_BAD_BLOCK,
          "programming a bad block should be refused");
    CHECK(nand_erase_block(&n, 2) == NAND_ERR_BAD_BLOCK,
          "erasing a bad block should be refused");
    nand_free(&n);
}

static void test_absurd_geometry_refused(void) {
    nand_t n;
    CHECK(!nand_init(&n, 0, 8, 4, 4), "zero page size accepted");
    CHECK(!nand_init(&n, 64, 8, 0, 4), "zero pages-per-block accepted");
    CHECK(!nand_init(&n, 64, 8, 4, 0), "zero block count accepted");
    /* A geometry whose page count overflows 32 bits must be refused. */
    CHECK(!nand_init(&n, 2048, 64, 0x10000u, 0x10000u),
          "overflowing geometry accepted");
}

/* ------------------------------------------------------------ persistence */

static void test_persistence_round_trip(void) {
    /* The jailbreak use case in miniature: modify the guest's storage, save it,
     * restore into a fresh device, and find the changes still there. Without
     * this a jailbreak would evaporate on every relaunch. */
    const char *path = "ios3vm_nand_test.img";
    nand_t a; small(&a);
    uint8_t page[64], spare[8];
    for (unsigned i = 0; i < 64; i++) page[i] = (uint8_t)(0xff ^ i);
    for (unsigned i = 0; i < 8; i++)  spare[i] = (uint8_t)(0xff ^ (i * 3));
    nand_program_page(&a, 7, page, spare);
    nand_mark_bad(&a, 3);

    CHECK(storage_save_nand(&a, path) == STORAGE_OK, "save failed");
    nand_free(&a);

    nand_t b; small(&b);
    storage_status_t st = storage_load_nand(&b, path);
    CHECK(st == STORAGE_OK, "load failed: %s", storage_strerror(st));

    uint8_t back[64], bspare[8];
    nand_read_page(&b, 7, back, bspare);
    CHECK(memcmp(page, back, 64) == 0, "data did not survive save/load");
    CHECK(memcmp(spare, bspare, 8) == 0, "spare did not survive save/load");
    CHECK(nand_is_bad(&b, 3), "bad-block map did not survive save/load");
    CHECK(!nand_is_bad(&b, 0), "block 0 wrongly bad after load");
    nand_free(&b);
    remove(path);
}

static void test_geometry_mismatch_refused(void) {
    /* Restoring into a differently-shaped device must be refused, not silently
     * misread — a misinterpreted NAND image looks like a guest bug for days. */
    const char *path = "ios3vm_nand_geom.img";
    nand_t a; small(&a);
    CHECK(storage_save_nand(&a, path) == STORAGE_OK, "save failed");
    nand_free(&a);

    nand_t b;
    CHECK(nand_init(&b, 128, 8, 4, 4), "init failed");   /* different page size */
    CHECK(storage_load_nand(&b, path) == STORAGE_ERR_GEOMETRY,
          "geometry mismatch accepted");
    nand_free(&b);
    remove(path);
}

static void test_bad_image_refused(void) {
    const char *path = "ios3vm_nand_junk.img";
    FILE *f = fopen(path, "wb");
    if (f) { fputs("not an image at all, really", f); fclose(f); }
    nand_t n; small(&n);
    storage_status_t st = storage_load_nand(&n, path);
    CHECK(st == STORAGE_ERR_FORMAT || st == STORAGE_ERR_TRUNCATED,
          "junk file accepted (%s)", storage_strerror(st));
    CHECK(storage_load_nand(&n, "definitely_missing_file.img") == STORAGE_ERR_IO,
          "missing file did not report an IO error");
    nand_free(&n);
    remove(path);
}

static void test_truncated_nand_load_is_transactional(void) {
    const char *good = "ios3vm_nand_tx_good.img";
    const char *shortp = "ios3vm_nand_tx_short.img";
    nand_t src, dst;
    CHECK(small(&src) && small(&dst), "init failed");
    memset(src.data, 0x11, (size_t)nand_total_pages(&src) * src.page_size);
    CHECK(storage_save_nand(&src, good) == STORAGE_OK, "save failed");
    CHECK(copy_prefix(good, shortp, 33u), "could not make truncated fixture");

    size_t db = (size_t)nand_total_pages(&dst) * dst.page_size;
    size_t sb = (size_t)nand_total_pages(&dst) * dst.spare_size;
    memset(dst.data, 0x5a, db); memset(dst.spare, 0x6b, sb); memset(dst.bad, 1, dst.block_count);
    uint8_t data_before[64 * 16], spare_before[8 * 16], bad_before[4];
    memcpy(data_before, dst.data, db); memcpy(spare_before, dst.spare, sb);
    memcpy(bad_before, dst.bad, dst.block_count);

    CHECK(storage_load_nand(&dst, shortp) == STORAGE_ERR_TRUNCATED,
          "truncated NAND image was not rejected");
    CHECK(memcmp(dst.data, data_before, db) == 0 &&
          memcmp(dst.spare, spare_before, sb) == 0 &&
          memcmp(dst.bad, bad_before, dst.block_count) == 0,
          "failed NAND load partially mutated the live device");
    nand_free(&src); nand_free(&dst); remove(good); remove(shortp);
}

static void test_nand_integrity_and_exact_length(void) {
    const char *path = "ios3vm_nand_integrity.img";
    nand_t src, dst;
    CHECK(small(&src) && small(&dst), "init failed");
    memset(src.data, 0x3c, (size_t)nand_total_pages(&src) * src.page_size);
    CHECK(storage_save_nand(&src, path) == STORAGE_OK, "save failed");
    CHECK(flip_file_byte(path, 32), "could not corrupt payload");
    CHECK(storage_load_nand(&dst, path) == STORAGE_ERR_CHECKSUM,
          "same-length payload corruption was accepted");

    CHECK(storage_save_nand(&src, path) == STORAGE_OK, "resave failed");
    CHECK(append_file_byte(path, 0xa5), "could not append junk");
    CHECK(storage_load_nand(&dst, path) == STORAGE_ERR_TRAILING,
          "trailing NAND bytes were accepted");

    CHECK(storage_save_nand(&src, path) == STORAGE_OK, "resave failed");
    CHECK(flip_file_byte(path, 24), "could not set an unknown header flag");
    CHECK(storage_load_nand(&dst, path) == STORAGE_ERR_TRAILING,
          "unknown NAND header flags were accepted");

    CHECK(storage_save_nand(&src, path) == STORAGE_OK, "resave failed");
    size_t bad_off = 32u + (size_t)nand_total_pages(&src) * src.page_size +
                     (size_t)nand_total_pages(&src) * src.spare_size;
    FILE *f = fopen(path, "r+b");
    bool wrote_bad = f && fseek(f, (long)bad_off, SEEK_SET) == 0 &&
                     fputc(2, f) != EOF;
    if (f && fclose(f) != 0) wrote_bad = false;
    CHECK(wrote_bad && refresh_storage_hash(path, 32u),
          "could not make checksum-valid noncanonical bad map");
    CHECK(storage_load_nand(&dst, path) == STORAGE_ERR_FORMAT,
          "noncanonical NAND bad-map byte was accepted");

    src.bad[0] = 2;
    CHECK(storage_save_nand(&src, path) == STORAGE_ERR_FORMAT,
          "noncanonical in-memory bad-map byte was persisted");
    src.bad[0] = 0;
    nand_free(&src); nand_free(&dst); remove(path);
}

static void test_truncated_nor_load_is_transactional(void) {
    const char *good = "ios3vm_nor_tx_good.img";
    const char *shortp = "ios3vm_nor_tx_short.img";
    s5l_nor_t src, dst;
    CHECK(s5l_nor_init(&src, 4096) && s5l_nor_init(&dst, 4096), "NOR init failed");
    memset(src.data, 0x22, src.size);
    CHECK(storage_save_nor(&src, good) == STORAGE_OK, "NOR save failed");
    CHECK(copy_prefix(good, shortp, 17u), "could not make truncated NOR fixture");

    memset(dst.data, 0x7c, dst.size);
    dst.image_count = 1;
    dst.images[0].ident = 0x69626f74u;
    dst.images[0].offset = 0x100;
    dst.images[0].size = 0x80;
    CHECK(storage_load_nor(&dst, shortp) == STORAGE_ERR_TRUNCATED,
          "truncated NOR image was not rejected");
    bool unchanged = dst.image_count == 1 && dst.images[0].offset == 0x100;
    for (uint32_t i = 0; i < dst.size; i++) if (dst.data[i] != 0x7c) unchanged = false;
    CHECK(unchanged, "failed NOR load changed data or its scanned directory");

    s5l_nor_free(&src); s5l_nor_free(&dst); remove(good); remove(shortp);
}

static void test_nor_integrity_and_exact_length(void) {
    const char *path = "ios3vm_nor_integrity.img";
    s5l_nor_t src, dst;
    CHECK(s5l_nor_init(&src, 4096) && s5l_nor_init(&dst, 4096), "NOR init failed");
    memset(src.data, 0x3c, src.size);
    CHECK(storage_save_nor(&src, path) == STORAGE_OK, "NOR save failed");
    CHECK(flip_file_byte(path, 16), "could not corrupt NOR payload");
    CHECK(storage_load_nor(&dst, path) == STORAGE_ERR_CHECKSUM,
          "same-length NOR corruption was accepted");

    CHECK(storage_save_nor(&src, path) == STORAGE_OK, "NOR replace failed");
    CHECK(append_file_byte(path, 0xa5), "could not append NOR junk");
    CHECK(storage_load_nor(&dst, path) == STORAGE_ERR_TRAILING,
          "trailing NOR bytes were accepted");
    s5l_nor_free(&src); s5l_nor_free(&dst); remove(path);
}

int main(void) {
    printf("S5LBox NAND device tests\n");
    test_erased_reads_all_ones();
    test_program_and_read_back();
    test_program_can_only_clear_bits();
    test_erase_restores_ones();
    test_range_and_bad_blocks();
    test_absurd_geometry_refused();
    test_persistence_round_trip();
    test_geometry_mismatch_refused();
    test_bad_image_refused();
    test_truncated_nand_load_is_transactional();
    test_nand_integrity_and_exact_length();
    test_truncated_nor_load_is_transactional();
    test_nor_integrity_and_exact_length();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
