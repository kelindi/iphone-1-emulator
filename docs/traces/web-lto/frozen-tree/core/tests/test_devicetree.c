/*
 * S5LBox — Apple device tree parser tests.
 *
 * Builds trees in memory (we ship no Apple firmware) and checks both correct
 * traversal and, importantly, that malformed trees are rejected rather than
 * read out of bounds — device trees arrive inside user-supplied firmware.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "devicetree.h"
#include "dt_inplace.h"          /* the in-place writers bootkernel ships */
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* --------------------------------------------------------- tree building */
static uint8_t  g_dt[4096];
static uint32_t g_len;

static void put32(uint32_t off, uint32_t v) {
    g_dt[off] = (uint8_t)v; g_dt[off+1] = (uint8_t)(v >> 8);
    g_dt[off+2] = (uint8_t)(v >> 16); g_dt[off+3] = (uint8_t)(v >> 24);
}

/* Emit a node header and remember where to patch the counts. */
static uint32_t node_begin(uint32_t nprops, uint32_t nchildren) {
    uint32_t off = g_len;
    put32(off, nprops);
    put32(off + 4, nchildren);
    g_len += 8;
    return off;
}

static void prop_ex(const char *name, const void *val, uint32_t len, uint32_t flags) {
    memset(&g_dt[g_len], 0, DT_PROP_NAME_LEN);
    memcpy(&g_dt[g_len], name, strlen(name));
    g_len += DT_PROP_NAME_LEN;
    put32(g_len, len | flags);      /* Apple stores flags in the top bit */
    g_len += 4;
    if (val && len) memcpy(&g_dt[g_len], val, len);
    g_len += (len + 3u) & ~3u;      /* values are 4-byte padded */
}

static void prop(const char *name, const void *val, uint32_t len) {
    prop_ex(name, val, len, 0u);
}

static void prop_str(const char *name, const char *val) {
    prop(name, val, (uint32_t)strlen(val) + 1u);
}
static void prop_u32(const char *name, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
    prop(name, b, 4);
}

/* An 8-byte {address, size} "reg" entry, the shape iBoot fills in. */
static void prop_reg(const char *name, uint32_t base, uint32_t size) {
    uint8_t b[8] = {
        (uint8_t)base, (uint8_t)(base>>8), (uint8_t)(base>>16), (uint8_t)(base>>24),
        (uint8_t)size, (uint8_t)(size>>8), (uint8_t)(size>>16), (uint8_t)(size>>24)
    };
    prop(name, b, 8);
}

/*
 * Build a small tree shaped like a real one:
 *   / (name="device-tree", model)
 *     chosen (name, memory-size)
 *     arm-io (name)
 *       uart0 (name, reg)
 */
static void build_tree(void) {
    memset(g_dt, 0, sizeof g_dt);
    g_len = 0;

    node_begin(2, 2);                       /* root */
    prop_str("name", "device-tree");
    prop_str("model", "iPhone1,1");

    node_begin(2, 0);                       /* chosen */
    prop_str("name", "chosen");
    prop_u32("memory-size", 0x08000000u);

    node_begin(1, 1);                       /* arm-io */
    prop_str("name", "arm-io");

    node_begin(2, 0);                       /* uart0 */
    prop_str("name", "uart0");
    prop_u32("reg", 0x3cc00000u);
}

/* ------------------------------------------------------------- the tests */

static void test_parse_and_root_properties(void) {
    build_tree();
    dt_t dt; dt_node_t root;
    dt_status_t st = dt_parse(g_dt, g_len, &dt, &root);
    CHECK(st == DT_OK, "parse failed: %s", dt_strerror(st));
    CHECK(root.n_props == 2, "root props=%u expect 2", root.n_props);
    CHECK(root.n_children == 2, "root children=%u expect 2", root.n_children);

    const uint8_t *v; uint32_t n;
    CHECK(dt_property(&dt, &root, "model", &v, &n) == DT_OK, "model missing");
    CHECK(n >= 9 && memcmp(v, "iPhone1,1", 9) == 0, "model value wrong");
}

static void test_find_child_and_property(void) {
    build_tree();
    dt_t dt; dt_node_t root, chosen;
    dt_parse(g_dt, g_len, &dt, &root);

    CHECK(dt_child(&dt, &root, "chosen", &chosen) == DT_OK, "chosen not found");
    uint32_t mem = 0;
    CHECK(dt_property_u32(&dt, &chosen, "memory-size", &mem) == DT_OK,
          "memory-size missing");
    CHECK(mem == 0x08000000u, "memory-size=%08x expect 08000000", mem);
}

static void test_path_lookup(void) {
    /* Walking past a sibling and into a grandchild is where offset arithmetic
     * usually goes wrong, so this is the important traversal case. */
    build_tree();
    dt_t dt; dt_node_t root, uart;
    dt_parse(g_dt, g_len, &dt, &root);

    dt_status_t st = dt_path(&dt, &root, "arm-io/uart0", &uart);
    CHECK(st == DT_OK, "path lookup failed: %s", dt_strerror(st));
    uint32_t reg = 0;
    CHECK(dt_property_u32(&dt, &uart, "reg", &reg) == DT_OK, "reg missing");
    CHECK(reg == 0x3cc00000u, "reg=%08x expect 3cc00000 (the UART base)", reg);
    printf("  [device tree] /arm-io/uart0 reg = 0x%08x\n", reg);
}

static void test_missing_lookups_report_not_found(void) {
    build_tree();
    dt_t dt; dt_node_t root, n;
    dt_parse(g_dt, g_len, &dt, &root);
    CHECK(dt_child(&dt, &root, "nope", &n) == DT_ERR_NOT_FOUND, "phantom child found");
    CHECK(dt_property(&dt, &root, "nope", NULL, NULL) == DT_ERR_NOT_FOUND,
          "phantom property found");
    CHECK(dt_path(&dt, &root, "arm-io/nope", &n) == DT_ERR_NOT_FOUND,
          "phantom path found");
}

/* --- malformed trees must be rejected, not read out of bounds ------------ */

static void test_reject_truncated(void) {
    build_tree();
    dt_t dt; dt_node_t root;
    /* Cut the blob in half: the declared structure now runs off the end. */
    CHECK(dt_parse(g_dt, g_len / 2, &dt, &root) == DT_ERR_TRUNCATED,
          "truncated tree accepted");
    CHECK(dt_parse(g_dt, 4, &dt, &root) == DT_ERR_TRUNCATED, "4-byte blob accepted");
    CHECK(dt_parse(NULL, 0, &dt, &root) == DT_ERR_TRUNCATED, "NULL accepted");
}

static void test_reject_absurd_property_length(void) {
    build_tree();
    /* Root's first property claims a gigantic length. */
    put32(8 + DT_PROP_NAME_LEN, 0x7fffffffu);
    dt_t dt; dt_node_t root;
    CHECK(dt_parse(g_dt, g_len, &dt, &root) == DT_ERR_TRUNCATED,
          "absurd property length accepted");
}

static void test_reject_absurd_child_count(void) {
    build_tree();
    put32(4, 0x00ffffffu);                  /* root claims 16M children */
    dt_t dt; dt_node_t root;
    CHECK(dt_parse(g_dt, g_len, &dt, &root) == DT_ERR_TRUNCATED,
          "absurd child count accepted");
}

static void test_reject_excessive_nesting(void) {
    /* A tree nested deeper than DT_MAX_DEPTH must be refused rather than
     * driving unbounded recursion on user-supplied data. */
    memset(g_dt, 0, sizeof g_dt);
    g_len = 0;
    for (unsigned i = 0; i < DT_MAX_DEPTH + 8u; i++) node_begin(0, 1);
    node_begin(0, 0);                       /* innermost leaf */

    dt_t dt; dt_node_t root;
    dt_status_t st = dt_parse(g_dt, g_len, &dt, &root);
    CHECK(st != DT_OK, "excessively nested tree accepted");
}

/*
 * Apple's tooling sets the top bit of a property length as a flag, and the
 * parser masks it off. That is the one piece of Apple-format knowledge in this
 * parser and nothing exercised it — mutation testing showed the whole mask
 * could be deleted with the suite staying green, which would break every real
 * Apple device tree. A flagged tree must parse exactly like an unflagged one,
 * and the reported length must be the masked value.
 */
static void test_property_length_flag_bit(void) {
    static const uint8_t regval[4] = { 0x11, 0x22, 0x33, 0x44 };
    memset(g_dt, 0, sizeof g_dt);
    g_len = 0;
    node_begin(2, 0);
    prop_ex("name", "flagged", 8, 0x80000000u);
    prop_ex("reg", regval, 4, 0x80000000u);

    dt_t dt; dt_node_t root;
    dt_status_t st = dt_parse(g_dt, g_len, &dt, &root);
    CHECK(st == DT_OK, "flagged tree failed to parse: %s", dt_strerror(st));

    const uint8_t *v; uint32_t n;
    CHECK(dt_property(&dt, &root, "name", &v, &n) == DT_OK, "flagged name missing");
    CHECK(n == 8, "length=%u expect 8 (the flag bit must be masked off)", n);

    uint32_t reg = 0;
    CHECK(dt_property_u32(&dt, &root, "reg", &reg) == DT_OK, "flagged reg missing");
    CHECK(reg == 0x44332211u, "reg=%08x expect 44332211", reg);
}

/* --- in-place patching: /vram, the property that lets the guest draw ------
 *
 * We load the kernel directly and never run iBoot, so every value iBoot would
 * have measured arrives as zero. /vram:reg is the one where zero is not merely
 * uninformative: IOSurfaceRoot builds its "PurpleGfxMem" region from that node,
 * AppleH1CLCD falls back to an IOMemoryDescriptor with kIODirectionOut when the
 * region is absent, and IOSurfaceRootUserClient turns kIODirectionOut into
 * kIOMapReadOnly — so userspace gets the framebuffer read-only and SpringBoard's
 * compositor faults on its first store. bootkernel therefore writes this entry
 * itself, with dt_set_reg() from tools/dt_inplace.h, which is what these cases
 * exercise: the real writer, not a copy of it.
 *
 * The N82 numbers are bootkernel's own: framebuffer PA 0x0885c000 and
 * N82_FB_BYTES == 320 * 480 * 4 == 0x00096000.
 */
#define VRAM_FB_PA    0x0885c000u
#define VRAM_FB_BYTES (320u * 480u * 4u)

static uint32_t g_vram_reg_off;         /* file offset of /vram:reg's value */

/*
 * Shaped like the node in the shipped 7E18 tree: four properties, the third an
 * 8-byte reg of two zero cells. A sibling is emitted after it so that an edit
 * which changed any length would show up as downstream corruption rather than
 * as a passing test.
 */
static void build_vram_tree(void) {
    memset(g_dt, 0, sizeof g_dt);
    g_len = 0;

    node_begin(1, 3);                       /* root */
    prop_str("name", "device-tree");

    node_begin(3, 0);                       /* memory */
    prop_str("name", "memory");
    prop_str("device_type", "memory");
    prop_reg("reg", 0, 0);

    node_begin(4, 0);                       /* vram */
    prop_str("name", "vram");
    prop_str("device_type", "vram");
    g_vram_reg_off = g_len + DT_PROP_NAME_LEN + 4u;
    prop_reg("reg", 0, 0);
    prop_u32("AAPL,phandle", 0x00b042d0u);

    node_begin(2, 0);                       /* the sibling after /vram */
    prop_str("name", "arm-io");
    prop_u32("ranges", 0x38000000u);
}

static void test_inplace_vram_reg_is_written_exactly(void) {
    build_vram_tree();
    uint8_t before[sizeof g_dt];
    memcpy(before, g_dt, sizeof g_dt);
    uint32_t len_before = g_len;

    CHECK(dt_set_reg(g_dt, g_len, "vram", "reg", VRAM_FB_PA, VRAM_FB_BYTES),
          "dt_set_reg refused a shipped-shape /vram node");
    CHECK(g_len == len_before, "the blob was resized (%u -> %u)",
          len_before, g_len);

    /* Pinned as bytes, not as two words: little-endian cells are the whole
     * contract with the kernel, and a byte-order slip reads as a plausible
     * address rather than as a crash. */
    static const uint8_t expect[8] = {
        0x00, 0xc0, 0x85, 0x08,             /* 0x0885c000                    */
        0x00, 0x60, 0x09, 0x00              /* 0x00096000 == 320 * 480 * 4   */
    };
    const uint8_t *got = &g_dt[g_vram_reg_off];
    CHECK(memcmp(got, expect, 8) == 0,
          "reg = %02x%02x%02x%02x %02x%02x%02x%02x, expect 00c08508 00600900",
          got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7]);

    /* Same-length and in place: nothing outside those eight bytes may move. */
    uint32_t bad = 0xffffffffu;
    for (uint32_t i = 0; i < len_before && bad == 0xffffffffu; i++) {
        if (i >= g_vram_reg_off && i < g_vram_reg_off + 8u) continue;
        if (g_dt[i] != before[i]) bad = i;
    }
    CHECK(bad == 0xffffffffu, "byte %u changed outside the reg entry", bad);

    /* And the parser the emulator actually reads trees with agrees. */
    dt_t dt; dt_node_t root, vram;
    CHECK(dt_parse(g_dt, g_len, &dt, &root) == DT_OK,
          "the patched tree no longer parses");
    CHECK(dt_path(&dt, &root, "vram", &vram) == DT_OK, "/vram disappeared");
    const uint8_t *v; uint32_t n;
    CHECK(dt_property(&dt, &vram, "reg", &v, &n) == DT_OK, "/vram:reg disappeared");
    CHECK(n == 8, "/vram:reg is %u bytes, expect 8", n);
    if (n == 8) {
        uint32_t base = (uint32_t)v[0] | ((uint32_t)v[1] << 8) |
                        ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
        uint32_t size = (uint32_t)v[4] | ((uint32_t)v[5] << 8) |
                        ((uint32_t)v[6] << 16) | ((uint32_t)v[7] << 24);
        CHECK(base == VRAM_FB_PA, "/vram:reg base = %08x, expect %08x",
              base, VRAM_FB_PA);
        CHECK(size == VRAM_FB_BYTES, "/vram:reg size = %08x, expect %08x",
              size, VRAM_FB_BYTES);
        printf("  [device tree] /vram reg = {0x%08x, 0x%08x}\n", base, size);
    }

    /* Re-running is a no-op, not a second edit: bootkernel patches a fresh
     * in-memory copy each boot and must not depend on that. */
    CHECK(dt_set_reg(g_dt, g_len, "vram", "reg", VRAM_FB_PA, VRAM_FB_BYTES),
          "dt_set_reg refused an already-patched node");
    CHECK(memcmp(&g_dt[g_vram_reg_off], expect, 8) == 0,
          "a second dt_set_reg changed the bytes");
}

/*
 * The writer is only safe because it is same-length. A node whose reg is not
 * exactly two cells must be refused outright — a partial write there would
 * either truncate the entry or run into the following property's name.
 */
static void test_inplace_reg_refuses_wrong_shape(void) {
    static const uint8_t sixteen[16] = {0};
    memset(g_dt, 0, sizeof g_dt);
    g_len = 0;

    node_begin(1, 3);                       /* root */
    prop_str("name", "device-tree");

    node_begin(2, 0);                       /* reg is one cell, not two */
    prop_str("name", "vram");
    prop_u32("reg", 0xa5a5a5a5u);

    node_begin(2, 0);                       /* reg is two {addr,size} pairs */
    prop_str("name", "pram");
    prop("reg", sixteen, 16);

    node_begin(1, 0);                       /* no reg property at all */
    prop_str("name", "nvram");

    uint8_t before[sizeof g_dt];
    memcpy(before, g_dt, sizeof g_dt);

    CHECK(!dt_set_reg(g_dt, g_len, "vram", "reg", VRAM_FB_PA, VRAM_FB_BYTES),
          "a 4-byte reg was accepted and silently resized");
    CHECK(!dt_set_reg(g_dt, g_len, "pram", "reg", VRAM_FB_PA, VRAM_FB_BYTES),
          "a 16-byte reg was accepted");
    CHECK(!dt_set_reg(g_dt, g_len, "nvram", "reg", VRAM_FB_PA, VRAM_FB_BYTES),
          "a missing reg property was accepted");
    CHECK(!dt_set_reg(g_dt, g_len, "vram0", "reg", VRAM_FB_PA, VRAM_FB_BYTES),
          "a missing node was accepted");
    CHECK(memcmp(before, g_dt, sizeof g_dt) == 0,
          "a refused dt_set_reg still modified the blob");

    /* Refusal has to be observable, because bootkernel fails the boot on it
     * rather than shipping a half-configured display. */
    dt_t dt; dt_node_t root;
    CHECK(dt_parse(g_dt, g_len, &dt, &root) == DT_OK,
          "the refused-edit tree no longer parses");
}

int main(void) {
    printf("S5LBox device tree tests\n");
    test_parse_and_root_properties();
    test_find_child_and_property();
    test_path_lookup();
    test_missing_lookups_report_not_found();
    test_reject_truncated();
    test_reject_absurd_property_length();
    test_reject_absurd_child_count();
    test_reject_excessive_nesting();
    test_property_length_flag_bit();
    test_inplace_vram_reg_is_written_exactly();
    test_inplace_reg_refuses_wrong_shape();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
