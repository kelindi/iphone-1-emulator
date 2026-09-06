/*
 * S5LBox — firmware loader tests.
 *
 * The headline test is the whole M3 pipeline in miniature: build an
 * AES-encrypted IMG3 whose payload is real ARM code, then decrypt it, load it
 * into guest RAM and execute it on the emulated S5L8900 until it prints over
 * the UART. That is exactly the path Apple's LLB/iBoot will take — the only
 * difference being who wrote the payload.
 *
 * We ship no Apple firmware, so every image here is built in memory.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "loader.h"
#include "aes.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ------------------------------------------------------- IMG3 construction */
static uint8_t  g_img[4096];
static uint32_t g_len;

static void put32(uint32_t off, uint32_t v) {
    g_img[off] = (uint8_t)v; g_img[off+1] = (uint8_t)(v >> 8);
    g_img[off+2] = (uint8_t)(v >> 16); g_img[off+3] = (uint8_t)(v >> 24);
}
static void img_begin(uint32_t ident) {
    memset(g_img, 0, sizeof g_img);
    put32(0, IMG3_MAGIC);   /* bytes 33 67 6d 49 == "3gmI" */
    put32(16, ident);
    g_len = 20;
}
static void img_tag(uint32_t magic, const void *data, uint32_t n) {
    uint32_t off = g_len;
    put32(off, magic); put32(off + 4, 12 + n); put32(off + 8, n);
    if (data && n) memcpy(&g_img[off + 12], data, n);
    g_len += 12 + n;
}
static void img_finish(void) {
    put32(4, g_len); put32(8, g_len - 20); put32(12, g_len - 20);
}

/*
 * The payload: ARM code that prints "iBoot" over the emulated UART and parks.
 * It reads the UART base from a literal exactly as real firmware does.
 */
static const uint32_t PAYLOAD[] = {
    0xe59f0028u,          /* LDR r0,[pc,#40]     -> UART base   */
    0xe3a01069u,          /* MOV r1,#'i' */  0xe5801020u,
    0xe3a01042u,          /* MOV r1,#'B' */  0xe5801020u,
    0xe3a0106fu,          /* MOV r1,#'o' */  0xe5801020u,
    0xe3a0106fu,          /* MOV r1,#'o' */  0xe5801020u,
    0xe3a01074u,          /* MOV r1,#'t' */  0xe5801020u,
    0xeafffffeu,          /* B .                                */
    S5L8900_UART0_BASE    /* literal at offset 0x30             */
};

static void test_plain_image_boots(void) {
    img_begin(0x69626f74u);                       /* 'ibot' */
    img_tag(0x44415441u, PAYLOAD, (uint32_t)sizeof PAYLOAD);   /* DATA */
    img_finish();

    s5l8900_t m;
    CHECK(s5l8900_init(&m, 0, 1u << 20), "machine init failed");

    fw_image_t info;
    fw_status_t st = fw_boot_img3(&m, g_img, g_len, NULL, 0, 0x1000, &info);
    CHECK(st == FW_OK, "boot failed: %s", fw_strerror(st));
    CHECK(strcmp(info.ident_str, "ibot") == 0, "ident=%s expect ibot", info.ident_str);
    CHECK(!info.was_encrypted, "plain image reported as encrypted");

    arm_status_t ast = ARM_OK;
    s5l8900_run(&m, 64, &ast);
    m.uart0.tx[m.uart0.tx_len] = '\0';
    CHECK(strcmp(m.uart0.tx, "iBoot") == 0,
          "uart=\"%s\" expect \"iBoot\"", m.uart0.tx);
    printf("  [plain IMG3 -> guest said] %s\n", m.uart0.tx);
    s5l8900_free(&m);
}

static void test_encrypted_image_boots(void) {
    /* Encrypt the payload exactly as Apple does, and give the loader the key. */
    static const uint8_t key[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
        0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
    };
    uint8_t iv[16];
    for (unsigned i = 0; i < 16; i++) iv[i] = (uint8_t)(0x20 + i);

    /* Real payloads are not always block-aligned, and img3_decrypt_data covers
     * the aligned prefix while passing any tail through. Mirror that here so
     * the test exercises the unaligned path rather than avoiding it. */
    uint8_t cipher[sizeof PAYLOAD];
    const uint32_t whole = (uint32_t)(sizeof PAYLOAD) & ~15u;
    const uint32_t tail  = (uint32_t)(sizeof PAYLOAD) - whole;
    aes_ctx_t ctx;
    aes_init(&ctx, key, 128);
    CHECK(aes_cbc_encrypt(&ctx, iv, (const uint8_t *)PAYLOAD, cipher, whole),
          "cbc encrypt of the aligned prefix failed");
    if (tail) memcpy(cipher + whole, (const uint8_t *)PAYLOAD + whole, tail);

    uint8_t kbag[24 + 16];
    memset(kbag, 0, sizeof kbag);
    kbag[0] = 1; kbag[4] = 128;
    memcpy(&kbag[8], iv, 16);

    img_begin(0x69626f74u);
    img_tag(0x4b424147u, kbag, sizeof kbag);                   /* KBAG */
    img_tag(0x44415441u, cipher, (uint32_t)sizeof cipher);     /* DATA */
    img_tag(0x53485348u, "sig", 3);                            /* SHSH */
    img_finish();

    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 20);

    fw_image_t info;
    memset(&m.ram[0x1000], 0xA5, sizeof PAYLOAD);
    fw_status_t st = fw_load_img3(&m, g_img, g_len, key, 128, 0x1000, &info);
    CHECK(st == FW_ERR_IV_REQUIRED,
          "implicit wrapped-KBAG IV should be refused, got: %s", fw_strerror(st));
    bool untouched = true;
    for (unsigned i = 0; i < sizeof PAYLOAD; i++)
        if (m.ram[0x1000 + i] != 0xA5) untouched = false;
    CHECK(untouched, "IV-required failure modified guest RAM");

    st = fw_boot_img3_iv(&m, g_img, g_len, key, 128, iv,
                         0x1000, &info);
    CHECK(st == FW_OK, "boot failed: %s", fw_strerror(st));
    CHECK(info.was_encrypted, "encrypted image not reported as encrypted");
    CHECK(info.was_signed, "SHSH present but not reported");

    arm_status_t ast = ARM_OK;
    s5l8900_run(&m, 64, &ast);
    m.uart0.tx[m.uart0.tx_len] = '\0';
    CHECK(strcmp(m.uart0.tx, "iBoot") == 0,
          "uart=\"%s\" expect \"iBoot\" (decrypted code executed)", m.uart0.tx);
    printf("  [encrypted IMG3 -> decrypted -> guest said] %s\n", m.uart0.tx);
    s5l8900_free(&m);
}

static void test_encrypted_without_key_is_refused(void) {
    uint8_t kbag[24 + 16];
    memset(kbag, 0, sizeof kbag);
    kbag[0] = 1; kbag[4] = 128;

    img_begin(0x69626f74u);
    img_tag(0x4b424147u, kbag, sizeof kbag);
    img_tag(0x44415441u, PAYLOAD, (uint32_t)sizeof PAYLOAD);
    img_finish();

    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 20);
    fw_status_t st = fw_load_img3(&m, g_img, g_len, NULL, 0, 0x1000, NULL);
    CHECK(st == FW_ERR_KEY_REQUIRED, "expected FW_ERR_KEY_REQUIRED, got %s",
          fw_strerror(st));
    s5l8900_free(&m);
}

static void test_oversized_payload_refused(void) {
    /* A payload that does not fit must be refused before anything is written. */
    img_begin(0x69626f74u);
    img_tag(0x44415441u, PAYLOAD, (uint32_t)sizeof PAYLOAD);
    img_finish();

    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 12);               /* only 4 KiB of RAM */
    fw_status_t st = fw_load_img3(&m, g_img, g_len, NULL, 0, 0xff0, NULL);
    CHECK(st == FW_ERR_NO_ROOM, "expected FW_ERR_NO_ROOM, got %s", fw_strerror(st));
    s5l8900_free(&m);
}

static void test_garbage_refused(void) {
    uint8_t junk[64];
    memset(junk, 0xA5, sizeof junk);
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 16);
    fw_status_t st = fw_load_img3(&m, junk, sizeof junk, NULL, 0, 0x100, NULL);
    CHECK(st == FW_ERR_PARSE, "expected FW_ERR_PARSE, got %s", fw_strerror(st));
    s5l8900_free(&m);
}

static void test_invalid_loader_arguments(void) {
    CHECK(fw_load_img3(NULL, g_img, g_len, NULL, 0, 0, NULL) ==
              FW_ERR_INVALID_ARGUMENT,
          "NULL machine was not refused");
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 16);
    CHECK(fw_load_img3(&m, NULL, 0, NULL, 0, 0, NULL) ==
              FW_ERR_INVALID_ARGUMENT,
          "NULL input was not refused");
    s5l8900_free(&m);
}

/* ------------------------------------------------------------- NOR flash */

static void test_nor_scan_and_find(void) {
    /* Put two images in flash the way a factory flasher would, then let the
     * scanner build the directory and look one up by ident.
     *
     * This doubles as a regression test: the first container is 42 bytes, i.e.
     * not a multiple of 4. An earlier scanner advanced by exactly the container
     * size and so fell permanently off the word grid, making every later image
     * invisible. Keep the first image's size unaligned. */
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 20);

    img_begin(0x696c6c62u);                        /* 'illb' */
    img_tag(0x44415441u, "LLBPAYLOAD", 10);
    img_finish();
    s5l_nor_program(&m.nor, 0x0000, g_img, g_len);

    img_begin(0x69626f74u);                        /* 'ibot' */
    img_tag(0x44415441u, PAYLOAD, (uint32_t)sizeof PAYLOAD);
    img_finish();
    s5l_nor_program(&m.nor, 0x1000, g_img, g_len);

    unsigned n = s5l_nor_scan(&m.nor);
    CHECK(n == 2, "scanned %u images, expect 2", n);

    const s5l_nor_entry_t *llb = s5l_nor_find(&m.nor, 0x696c6c62u);
    const s5l_nor_entry_t *ibot = s5l_nor_find(&m.nor, 0x69626f74u);
    CHECK(llb && llb->offset == 0x0000, "llb not found at 0");
    CHECK(ibot && ibot->offset == 0x1000, "ibot not found at 0x1000");
    CHECK(s5l_nor_find(&m.nor, 0x64747265u) == NULL, "found a 'dtre' that is not there");
    s5l8900_free(&m);
}

static void test_nor_is_memory_mapped(void) {
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 16);
    uint32_t marker = 0xcafebabeu;
    s5l_nor_program(&m.nor, 0x40, &marker, 4);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_NOR_BASE + 0x40) == 0xcafebabeu,
          "NOR not readable through the bus");
    /* Guest writes now program the flash (bits can only be cleared), which is
     * the path a jailbreak payload needs. They must not be miscounted as
     * unmapped accesses. */
    m.bus.write32(m.bus.ctx, S5L8900_NOR_BASE + 0x40, 0x0a0eba0eu);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_NOR_BASE + 0x40) == (0xcafebabeu & 0x0a0eba0eu),
          "guest write should AND into the flash");
    CHECK(m.unmapped_writes == 0, "NOR write counted as unmapped");
    s5l8900_free(&m);
}

static void test_nor_scan_rejects_corrupt_header(void) {
    /* A container whose declared size runs past the flash must be ignored,
     * not trusted — NOR contents are user-supplied. */
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 16);
    img_begin(0x69626f74u);
    img_tag(0x44415441u, "x", 1);
    img_finish();
    /* Claim a fullSize far larger than the flash. */
    g_img[4] = 0xff; g_img[5] = 0xff; g_img[6] = 0xff; g_img[7] = 0x7f;
    s5l_nor_program(&m.nor, 0, g_img, g_len);
    CHECK(s5l_nor_scan(&m.nor) == 0, "corrupt container was accepted");
    s5l8900_free(&m);
}

static void test_nor_find_rejects_corrupt_directory(void) {
    s5l_nor_t n;
    CHECK(s5l_nor_init(&n, 4096), "NOR init failed");
    n.image_count = 1;
    n.images[0].ident = 0x69626f74u;
    n.images[0].offset = n.size - 4u;
    n.images[0].size = 20u;
    CHECK(s5l_nor_find(&n, n.images[0].ident) == NULL,
          "out-of-range in-memory NOR directory entry was returned");
    CHECK(s5l_nor_find(NULL, n.images[0].ident) == NULL,
          "NULL NOR directory was dereferenced");
    s5l_nor_free(&n);
}

/* The M3 boot-chain shape in miniature: locate iBoot in NOR by ident, load it
 * out of flash, and execute it. */
static void test_boot_from_nor(void) {
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 20);

    img_begin(0x69626f74u);
    img_tag(0x44415441u, PAYLOAD, (uint32_t)sizeof PAYLOAD);
    img_finish();
    s5l_nor_program(&m.nor, 0x2000, g_img, g_len);
    s5l_nor_scan(&m.nor);

    const s5l_nor_entry_t *e = s5l_nor_find(&m.nor, 0x69626f74u);
    CHECK(e != NULL, "iBoot not found in NOR");
    if (!e) { s5l8900_free(&m); return; }

    fw_image_t info;
    fw_status_t st = fw_boot_img3(&m, &m.nor.data[e->offset], e->size,
                                  NULL, 0, 0x1000, &info);
    CHECK(st == FW_OK, "boot from NOR failed: %s", fw_strerror(st));

    arm_status_t ast = ARM_OK;
    s5l8900_run(&m, 64, &ast);
    m.uart0.tx[m.uart0.tx_len] = '\0';
    CHECK(strcmp(m.uart0.tx, "iBoot") == 0, "uart=\"%s\" expect iBoot", m.uart0.tx);
    printf("  [found '%s' in NOR @0x%x -> booted -> guest said] %s\n",
           info.ident_str, e->offset, m.uart0.tx);
    s5l8900_free(&m);
}

/* --- regressions from the adversarial audit ----------------------------- */

static void test_nor_directory_cap(void) {
    /* NOR contents are user-supplied dumps and can trivially hold more images
     * than the directory has room for. Mutation testing showed an off-by-one in
     * the cap would overflow s5l_nor_t.images with the suite still green. */
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 16);

    /* A minimal 20-byte container, repeated well past S5L_NOR_MAX_IMAGES. */
    uint8_t tiny[20];
    memset(tiny, 0, sizeof tiny);
    tiny[0] = 0x33; tiny[1] = 0x67; tiny[2] = 0x6d; tiny[3] = 0x49;  /* "3gmI" */
    tiny[4] = 20;                                                    /* fullSize */
    for (unsigned i = 0; i < S5L_NOR_MAX_IMAGES * 4u; i++)
        s5l_nor_program(&m.nor, i * 32u, tiny, sizeof tiny);

    unsigned n = s5l_nor_scan(&m.nor);
    CHECK(n <= S5L_NOR_MAX_IMAGES, "scan reported %u images, cap is %u",
          n, S5L_NOR_MAX_IMAGES);
    CHECK(m.nor.image_count <= S5L_NOR_MAX_IMAGES,
          "image_count=%u exceeds the directory size", m.nor.image_count);
    s5l8900_free(&m);
}

static void test_load_with_non_zero_ram_base(void) {
    /* Every other test uses ram_base 0, which makes the loader's ram_base
     * subtraction a no-op. Real S5L8900 SDRAM does not live at 0 and M4 will
     * need a non-zero base, so exercise it. */
    const uint32_t base = S5L8900_SDRAM_BASE;
    img_begin(0x69626f74u);
    img_tag(0x44415441u, PAYLOAD, (uint32_t)sizeof PAYLOAD);
    img_finish();

    s5l8900_t m;
    CHECK(s5l8900_init(&m, base, 1u << 20), "init failed");

    fw_image_t info;
    fw_status_t st = fw_boot_img3(&m, g_img, g_len, NULL, 0, base + 0x1000, &info);
    CHECK(st == FW_OK, "boot at a non-zero RAM base failed: %s", fw_strerror(st));

    /* The payload must land at the right offset within the allocation. */
    CHECK(memcmp(&m.ram[0x1000], PAYLOAD, sizeof PAYLOAD) == 0,
          "payload was written to the wrong offset for a non-zero ram_base");

    arm_status_t ast = ARM_OK;
    s5l8900_run(&m, 64, &ast);
    m.uart0.tx[m.uart0.tx_len] = '\0';
    CHECK(strcmp(m.uart0.tx, "iBoot") == 0, "uart=\"%s\" expect iBoot", m.uart0.tx);

    /* An address below the RAM base must be refused. */
    CHECK(fw_load_img3(&m, g_img, g_len, NULL, 0, base - 0x100, NULL) == FW_ERR_NO_ROOM,
          "a load address below ram_base should be refused");
    printf("  [non-zero RAM base 0x%08x -> guest said] %s\n", base, m.uart0.tx);
    s5l8900_free(&m);
}

static void test_malformed_kbag_refused_by_loader(void) {
    /* "Encrypted, method unknown" must not be downgraded to "plaintext" and
     * executed as code. */
    uint8_t k[8] = {1,0,0,0, 128,0,0,0};
    img_begin(0x69626f74u);
    img_tag(0x4b424147u, k, sizeof k);
    img_tag(0x44415441u, PAYLOAD, (uint32_t)sizeof PAYLOAD);
    img_finish();

    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 20);
    fw_status_t st = fw_load_img3(&m, g_img, g_len, NULL, 0, 0x1000, NULL);
    CHECK(st == FW_ERR_DECRYPT, "expected FW_ERR_DECRYPT, got %s", fw_strerror(st));
    s5l8900_free(&m);
}

/* ------------------------------------------- writable + persistent NOR --- */

static void test_nor_write_is_flash_not_ram(void) {
    /* Programming can only clear bits; setting one back to 1 needs an erase. */
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 16);

    CHECK(s5l_nor_write(&m.nor, 0x40, 0x0f0f0f0fu, 4), "initial program failed");
    CHECK(s5l_nor_read(&m.nor, 0x40, 4) == 0x0f0f0f0fu, "program did not take");

    CHECK(!s5l_nor_write(&m.nor, 0x40, 0xffffffffu, 4),
          "setting bits back to 1 should require an erase");
    CHECK(s5l_nor_write(&m.nor, 0x40, 0x03030303u, 4),
          "clearing further bits should be allowed");
    CHECK(s5l_nor_read(&m.nor, 0x40, 4) == 0x03030303u, "further clear did not take");

    CHECK(s5l_nor_erase_sector(&m.nor, 0x40), "erase failed");
    CHECK(s5l_nor_read(&m.nor, 0x40, 4) == 0xffffffffu, "erase should restore ones");
    s5l8900_free(&m);
}

/*
 * The untether shape in miniature: guest code writes a payload into NOR, the
 * image is saved, and a freshly-created machine loads it and still sees the
 * payload. Without this a jailbreak would evaporate on every relaunch.
 */
static void test_guest_payload_persists_in_nor(void) {
    const char *path = "ios3vm_nor_test.img";
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 20);

    /* A store into the NOR window programs the flash, which is the path a
     * guest payload would use to persist itself. */
    m.bus.write32(m.bus.ctx, S5L8900_NOR_BASE + 0x800, 0xa5a5a5a5u);
    CHECK(m.bus.read32(m.bus.ctx, S5L8900_NOR_BASE + 0x800) == 0xa5a5a5a5u,
          "guest write to the NOR window did not program the flash");

    CHECK(storage_save_nor(&m.nor, path) == STORAGE_OK, "NOR save failed");
    s5l8900_free(&m);

    s5l8900_t m2;
    s5l8900_init(&m2, 0, 1u << 20);
    storage_status_t st = storage_load_nor(&m2.nor, path);
    CHECK(st == STORAGE_OK, "NOR load failed: %s", storage_strerror(st));
    CHECK(m2.bus.read32(m2.bus.ctx, S5L8900_NOR_BASE + 0x800) == 0xa5a5a5a5u,
          "the payload did not survive a relaunch");
    printf("  [payload written to NOR survived a relaunch] 0x%08x\n",
           m2.bus.read32(m2.bus.ctx, S5L8900_NOR_BASE + 0x800));
    s5l8900_free(&m2);
    remove(path);
}

static void test_nor_image_rebuilds_directory_on_load(void) {
    /* A restored image must have its image directory rebuilt, or a boot after
     * a relaunch would find no images in a perfectly good flash. */
    const char *path = "ios3vm_nor_dir.img";
    s5l8900_t m;
    s5l8900_init(&m, 0, 1u << 20);
    img_begin(0x69626f74u);
    img_tag(0x44415441u, PAYLOAD, (uint32_t)sizeof PAYLOAD);
    img_finish();
    s5l_nor_program(&m.nor, 0x3000, g_img, g_len);
    s5l_nor_scan(&m.nor);
    CHECK(storage_save_nor(&m.nor, path) == STORAGE_OK, "save failed");
    s5l8900_free(&m);

    s5l8900_t m2;
    s5l8900_init(&m2, 0, 1u << 20);
    CHECK(storage_load_nor(&m2.nor, path) == STORAGE_OK, "load failed");
    const s5l_nor_entry_t *e = s5l_nor_find(&m2.nor, 0x69626f74u);
    CHECK(e != NULL && e->offset == 0x3000,
          "directory was not rebuilt after restoring the image");
    s5l8900_free(&m2);
    remove(path);
}

int main(void) {
    printf("S5LBox firmware loader tests\n");
    test_plain_image_boots();
    test_encrypted_image_boots();
    test_encrypted_without_key_is_refused();
    test_oversized_payload_refused();
    test_garbage_refused();
    test_invalid_loader_arguments();
    test_nor_scan_and_find();
    test_nor_is_memory_mapped();
    test_nor_scan_rejects_corrupt_header();
    test_nor_find_rejects_corrupt_directory();
    test_boot_from_nor();
    test_nor_directory_cap();
    test_load_with_non_zero_ram_base();
    test_malformed_kbag_refused_by_loader();
    test_nor_write_is_flash_not_ram();
    test_guest_payload_persists_in_nor();
    test_nor_image_rebuilds_directory_on_load();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
