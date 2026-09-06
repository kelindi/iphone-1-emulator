/*
 * S5LBox -- focused 32-bit Mach-O identity/parser tests.
 *
 * Version-specific guest patches must never run against a merely similar
 * kernel. These fixtures exercise LC_UUID as untrusted input: exact parsing,
 * absence, malformed sizes, duplicate identities, and command truncation.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "macho.h"

#include <stdio.h>
#include <string.h>

static int g_pass;
static int g_fail;

#define CHECK(condition, ...) do {                                           \
    if (condition) {                                                         \
        g_pass++;                                                            \
    } else {                                                                \
        g_fail++;                                                            \
        printf("  FAIL %s:%d: ", __func__, __LINE__);                       \
        printf(__VA_ARGS__);                                                 \
        printf("\n");                                                       \
    }                                                                        \
} while (0)

static const uint8_t TEST_UUID[16] = {
    0x7f, 0x87, 0xdd, 0x4b, 0xdc, 0x3d, 0xf5, 0x22,
    0xa8, 0x43, 0x07, 0x57, 0x08, 0x59, 0x35, 0x7e
};

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static size_t make_header(uint8_t *image, size_t capacity,
                          uint32_t ncmds, uint32_t sizeofcmds) {
    if (capacity < 28u + sizeofcmds)
        return 0;
    memset(image, 0, capacity);
    put32(image + 0, MH_MAGIC_32);
    put32(image + 4, MH_CPU_TYPE_ARM);
    put32(image + 8, MH_CPU_SUBTYPE_V6);
    put32(image + 12, MH_EXECUTE);
    put32(image + 16, ncmds);
    put32(image + 20, sizeofcmds);
    return 28u + sizeofcmds;
}

static void put_uuid_command(uint8_t *command, uint32_t command_size,
                             const uint8_t uuid[16]) {
    put32(command + 0, LC_UUID);
    put32(command + 4, command_size);
    if (command_size >= 24u)
        memcpy(command + 8, uuid, 16);
}

static void test_uuid_present_and_absent(void) {
    uint8_t image[64];
    macho_t parsed;
    size_t length = make_header(image, sizeof image, 1, 24);

    put_uuid_command(image + 28, 24, TEST_UUID);
    CHECK(macho_parse(image, length, &parsed) == MACHO_OK,
          "valid UUID command was rejected");
    CHECK(parsed.has_uuid, "valid UUID command was not reported");
    CHECK(memcmp(parsed.uuid, TEST_UUID, sizeof TEST_UUID) == 0,
          "UUID bytes changed during parsing");
    CHECK(parsed.cputype == MH_CPU_TYPE_ARM &&
          parsed.cpusubtype == MH_CPU_SUBTYPE_V6 &&
          parsed.filetype == MH_EXECUTE,
          "UUID parsing damaged the Mach-O header fields");

    length = make_header(image, sizeof image, 0, 0);
    CHECK(macho_parse(image, length, &parsed) == MACHO_OK,
          "command-free Mach-O was rejected");
    CHECK(!parsed.has_uuid, "absent UUID inherited stale parser state");
    for (size_t i = 0; i < sizeof parsed.uuid; i++)
        CHECK(parsed.uuid[i] == 0u,
              "absent UUID byte %zu retained %u", i,
              (unsigned)parsed.uuid[i]);
}

static void test_uuid_command_sizes(void) {
    uint8_t image[80];
    macho_t parsed;
    size_t length;

    length = make_header(image, sizeof image, 1, 23);
    put32(image + 28, LC_UUID);
    put32(image + 32, 23);
    CHECK(macho_parse(image, length, &parsed) == MACHO_ERR_MALFORMED,
          "23-byte UUID command was accepted");

    length = make_header(image, sizeof image, 1, 25);
    put_uuid_command(image + 28, 25, TEST_UUID);
    image[52] = 0xa5u;
    CHECK(macho_parse(image, length, &parsed) == MACHO_ERR_MALFORMED,
          "oversized UUID command was accepted");

    length = make_header(image, sizeof image, 1, 24);
    put_uuid_command(image + 28, 24, TEST_UUID);
    CHECK(macho_parse(image, length - 1u, &parsed) == MACHO_ERR_MALFORMED,
          "truncated UUID command payload was accepted");

    put32(image + 32, 0);
    CHECK(macho_parse(image, length, &parsed) == MACHO_ERR_MALFORMED,
          "zero-sized UUID command was accepted");
}

static void test_duplicate_uuid_is_ambiguous(void) {
    uint8_t image[96];
    uint8_t second[16];
    macho_t parsed;
    size_t length = make_header(image, sizeof image, 2, 48);

    memcpy(second, TEST_UUID, sizeof second);
    second[15] ^= 0xffu;
    put_uuid_command(image + 28, 24, TEST_UUID);
    put_uuid_command(image + 52, 24, second);
    CHECK(macho_parse(image, length, &parsed) == MACHO_ERR_MALFORMED,
          "two different UUID commands were accepted");

    put_uuid_command(image + 52, 24, TEST_UUID);
    CHECK(macho_parse(image, length, &parsed) == MACHO_ERR_MALFORMED,
          "duplicate identical UUID commands were accepted");
}

int main(void) {
    test_uuid_present_and_absent();
    test_uuid_command_sizes();
    test_duplicate_uuid_is_ambiguous();

    printf("macho: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
