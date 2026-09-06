/*
 * S5LBox — PC-to-symbol resolution tests.
 *
 * We ship no Apple firmware, so the kernelcache under test is built in memory:
 * a 32-bit ARM Mach-O with an LC_SYMTAB, a __PRELINK_TEXT extent and a
 * __PRELINK_INFO plist shaped exactly like the one in an iPhone OS 3.1.3
 * cache — nested IOKitPersonalities dicts, ID=/IDREF= back-references and all.
 *
 * Half of these tests are about the failure path on purpose. A resolver that
 * quietly produces an empty kext map is worse than no resolver: it reports
 * bare addresses and looks like it worked. Every malformed input below has to
 * come back with a status AND a sentence in ks.detail saying what it saw.
 *
 * The last test runs against the real firmware/kernel.macho if it happens to
 * be present, and reports "skipped" if it is not — the image is git-ignored,
 * so CI never sees it.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "ksyms.h"
#include "macho.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); \
           printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* --------------------------------------------------- image construction --- */

#define IMG_CAP 65536u
static uint8_t  g_img[IMG_CAP];
static size_t   g_len;

static void put32(size_t off, uint32_t v) {
    g_img[off] = (uint8_t)v;          g_img[off+1] = (uint8_t)(v >> 8);
    g_img[off+2] = (uint8_t)(v >> 16); g_img[off+3] = (uint8_t)(v >> 24);
}

static uint32_t get32(size_t off) {
    return (uint32_t)g_img[off] | ((uint32_t)g_img[off + 1] << 8) |
           ((uint32_t)g_img[off + 2] << 16) | ((uint32_t)g_img[off + 3] << 24);
}

/* Two symbols, so "nearest symbol below" has something to choose between. */
static const struct { const char *name; uint32_t value; } SYMS[] = {
    { "_alpha", 0xc0008000u },
    { "_beta",  0xc0008100u },
};
#define NSYMS 2u

#define PTEXT_VM   0xc0100000u
#define PTEXT_SIZE 0x00020000u

/*
 * Build the image. `plist` NULL means "no __PRELINK_INFO at all" (a plain
 * kernel rather than a kernelcache).
 */
static void build_image(const char *plist, bool with_symtab) {
    memset(g_img, 0, sizeof g_img);

    unsigned ncmds = 1 + (plist ? 2 : 0) + (with_symtab ? 1 : 0);
    size_t   off   = 28;

    const size_t DATA = 4096;              /* payloads start here */
    size_t plist_off = DATA;
    size_t plist_len = plist ? strlen(plist) : 0;
    size_t plist_pad = (plist_len + 0xfffu) & ~(size_t)0xfff;
    if (plist) memcpy(g_img + plist_off, plist, plist_len);

    size_t symoff  = plist_off + plist_pad;
    size_t nsym    = with_symtab ? NSYMS : 0;
    size_t stroff  = symoff + nsym * 12;
    size_t strsize = 1;                    /* index 0 is the empty string */
    if (with_symtab) {
        for (unsigned i = 0; i < NSYMS; i++) {
            size_t strx = stroff + strsize;
            memcpy(g_img + strx, SYMS[i].name, strlen(SYMS[i].name) + 1);
            put32(symoff + i * 12, (uint32_t)strsize);      /* n_strx */
            g_img[symoff + i * 12 + 4] = 0x0f;              /* N_SECT|N_EXT */
            g_img[symoff + i * 12 + 5] = 1;                 /* n_sect */
            put32(symoff + i * 12 + 8, SYMS[i].value);      /* n_value */
            strsize += strlen(SYMS[i].name) + 1;
        }
    }
    g_len = stroff + strsize;

    /* header */
    put32(0, MH_MAGIC_32);
    put32(4, MH_CPU_TYPE_ARM);
    put32(8, MH_CPU_SUBTYPE_V6);
    put32(12, MH_EXECUTE);
    put32(16, ncmds);

#define SEGMENT(name, vma, vms, fo, fs) do {                      \
        put32(off, LC_SEGMENT); put32(off + 4, 56);               \
        memcpy(g_img + off + 8, (name), strlen(name));            \
        put32(off + 24, (vma)); put32(off + 28, (vms));           \
        put32(off + 32, (uint32_t)(fo)); put32(off + 36, (uint32_t)(fs)); \
        off += 56;                                                \
    } while (0)

    SEGMENT("__TEXT", 0xc0008000u, 0x1000u, 0, 0);
    if (plist) {
        SEGMENT("__PRELINK_TEXT", PTEXT_VM, PTEXT_SIZE, 0, 0);
        SEGMENT("__PRELINK_INFO", 0xc0200000u, (uint32_t)plist_pad,
                plist_off, plist_pad);
    }
    if (with_symtab) {
        put32(off, LC_SYMTAB); put32(off + 4, 24);
        put32(off + 8,  (uint32_t)symoff);
        put32(off + 12, (uint32_t)nsym);
        put32(off + 16, (uint32_t)stroff);
        put32(off + 20, (uint32_t)strsize);
        off += 24;
    }
#undef SEGMENT
    put32(20, (uint32_t)(off - 28));       /* sizeofcmds */
}

/*
 * A well-formed cache, matching the real one's shape:
 *   - a kext whose identifier is defined with ID= and echoed by IDREF= inside
 *     a nested IOKitPersonalities dict,
 *   - a nested CFBundleIdentifier with a DIFFERENT value, which must be
 *     ignored (it is not a key of the kext dict),
 *   - an <integer ID=> reused later by IDREF=,
 *   - a KPI pseudo-extension with no executable.
 */
static const char *GOOD_PLIST =
"<array>"
  "<dict>"
    "<key>CFBundleName</key><string ID=\"3\">One</string>"
    "<key>CFBundleIdentifier</key><string ID=\"1\">com.example.one</string>"
    "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0100000</integer>"
    "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x10000</integer>"
    "<key>_PrelinkKmodInfo</key><integer ID=\"7\" size=\"64\">0x8000</integer>"
    "<key>OSKernelResource</key><false/>"
    "<key>IOKitPersonalities</key><dict><key>Nested</key><dict>"
      "<key>CFBundleIdentifier</key><string>com.example.WRONG</string>"
      "<key>IOKitDebug</key><integer size=\"32\">0x0</integer>"
      "<key>Names</key><array><string IDREF=\"3\"/><string>x</string></array>"
    "</dict></dict>"
    "<key>OSBundleRequired</key><string>Root</string>"
  "</dict>"
  "<dict>"
    "<key>CFBundleIdentifier</key><string>com.example.two</string>"
    "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0110000</integer>"
    "<key>_PrelinkExecutableSize</key><integer IDREF=\"7\"/>"
  "</dict>"
  "<dict>"
    "<key>CFBundleIdentifier</key><string>com.apple.kpi.mach</string>"
    "<key>OSKernelResource</key><true/>"
  "</dict>"
"</array>";

static const char *name_of(ksyms_t *ks, uint32_t a) {
    static char buf[192];
    return ksyms_resolve(ks, a, buf, sizeof buf);
}

static void test_macho_command_boundaries(void) {
    macho_t m;
    build_image(GOOD_PLIST, true);
    uint32_t full_cmdsz = get32(20);

    /* ncmds may not walk past the header's declared sizeofcmds area even when
     * the surrounding file contains plausible command-shaped bytes. */
    put32(20, 56u);
    CHECK(macho_parse(g_img, g_len, &m) == MACHO_ERR_MALFORMED,
          "load commands escaped sizeofcmds");
    put32(20, full_cmdsz);

    /* A loader cannot safely copy more initialized bytes than the segment's
     * virtual allocation, nor represent a 32-bit extent that wraps. */
    put32(28 + 28, 1u); put32(28 + 36, 2u);
    CHECK(macho_parse(g_img, g_len, &m) == MACHO_ERR_MALFORMED,
          "segment filesize larger than vmsize was accepted");
    put32(28 + 28, 0x20u); put32(28 + 36, 0u); put32(28 + 24, 0xfffffff0u);
    CHECK(macho_parse(g_img, g_len, &m) == MACHO_ERR_MALFORMED,
          "wrapping segment VM extent was accepted");

    build_image(GOOD_PLIST, true);
    put32(28 + 48, 1u); /* one section declared, no section_32 bytes present */
    CHECK(macho_parse(g_img, g_len, &m) == MACHO_ERR_MALFORMED,
          "LC_SEGMENT section count escaped cmdsize");
    CHECK(macho_parse(g_img, g_len, NULL) == MACHO_ERR_INVALID_ARGUMENT,
          "NULL Mach-O result pointer was accepted");
    CHECK(macho_segment(NULL, "__TEXT") == NULL &&
          macho_segment(&m, NULL) == NULL,
          "macho_segment accepted a NULL argument");
}

static void test_unterminated_symbol_name_is_skipped(void) {
    build_image(NULL, true);
    g_img[g_len - 1u] = 'X'; /* remove _beta's terminator inside LC_SYMTAB */
    ksyms_t ks;
    (void)ksyms_load(&ks, g_img, g_len);
    CHECK(ks.sym_status == KSYMS_OK && ks.nsym == 1,
          "unterminated symbol was retained (status=%s nsym=%u)",
          ksyms_strerror(ks.sym_status), ks.nsym);
    CHECK(ksyms_value(&ks, "_beta") == 0,
          "unterminated symbol name was exposed to string consumers");
    ksyms_free(&ks);
}

/* ----------------------------------------------------------- happy path --- */

static void test_good_cache(void) {
    build_image(GOOD_PLIST, true);
    ksyms_t ks;
    ksyms_status_t st = ksyms_load(&ks, g_img, g_len);
    CHECK(st == KSYMS_OK, "load: %s (%s)", ksyms_strerror(st), ks.detail);
    CHECK(ks.nsym == NSYMS, "nsym=%u expect %u", ks.nsym, NSYMS);
    CHECK(ks.nkext == 3, "nkext=%u expect 3", ks.nkext);
    CHECK(ks.nkext_exec == 2, "nkext_exec=%u expect 2", ks.nkext_exec);
    CHECK(ks.detail[0] == '\0', "clean parse left a warning: %s", ks.detail);

    /* kernel symbols */
    CHECK(!strcmp(name_of(&ks, 0xc0008000u), "_alpha"), "%s", name_of(&ks, 0xc0008000u));
    CHECK(!strcmp(name_of(&ks, 0xc0008010u), "_alpha+0x10"), "%s", name_of(&ks, 0xc0008010u));
    CHECK(!strcmp(name_of(&ks, 0xc0008101u), "_beta"), "thumb bit: %s",
          name_of(&ks, 0xc0008101u));
    CHECK(!strcmp(name_of(&ks, 0xc0000000u), "?"), "below every symbol: %s",
          name_of(&ks, 0xc0000000u));
    CHECK(ksyms_value(&ks, "_beta") == 0xc0008100u, "ksyms_value(_beta)");

    /* kexts: the nested CFBundleIdentifier must not have won */
    CHECK(!strcmp(name_of(&ks, 0xc0100000u), "com.example.one+0x0"),
          "%s", name_of(&ks, 0xc0100000u));
    CHECK(!strcmp(name_of(&ks, 0xc0100122u), "com.example.one+0x122"),
          "%s", name_of(&ks, 0xc0100122u));
    CHECK(!strcmp(name_of(&ks, 0xc010fffeu), "com.example.one+0xfffe"),
          "last byte of a kext: %s", name_of(&ks, 0xc010fffeu));
    CHECK(!strcmp(name_of(&ks, 0xc0110000u), "com.example.two+0x0"),
          "%s", name_of(&ks, 0xc0110000u));

    /* _PrelinkExecutableSize came through an IDREF */
    const kext_t *two = ksyms_kext_at(&ks, 0xc0110000u);
    CHECK(two && two->size == 0x8000u, "IDREF integer size=%x",
          two ? two->size : 0);
    const kext_t *one = ksyms_kext_at(&ks, 0xc0100000u);
    CHECK(one && one->kmod_info == 0x8000u, "kmod_info");

    /* inside __PRELINK_TEXT but claimed by nobody: say so, do not fall back
     * to a kernel symbol megabytes away */
    CHECK(!strcmp(name_of(&ks, 0xc0118000u), "__PRELINK_TEXT+0x18000"),
          "prelink gap: %s", name_of(&ks, 0xc0118000u));
    /* past the end of __PRELINK_TEXT entirely */
    CHECK(!strcmp(name_of(&ks, 0xc0120000u), "?"), "past prelink: %s",
          name_of(&ks, 0xc0120000u));
    CHECK(ksyms_kext_at(&ks, 0xc0118000u) == NULL, "gap claimed by a kext");

    /* the KPI pseudo-extension is listed but owns no address */
    bool saw_kpi = false;
    for (unsigned i = 0; i < ks.nkext; i++)
        if (!strcmp(ks.kext[i].bundle, "com.apple.kpi.mach"))
            { saw_kpi = true; CHECK(!ks.kext[i].has_exec, "KPI has code?"); }
    CHECK(saw_kpi, "the pseudo-extension is missing from the map");

    ksyms_free(&ks);
}

/* A plain kernel: no kext map, but the symbols must still work. */
static void test_no_prelink_still_names_kernel(void) {
    build_image(NULL, true);
    ksyms_t ks;
    ksyms_status_t st = ksyms_load(&ks, g_img, g_len);
    CHECK(st == KSYMS_ERR_NO_PRELINK, "status %s", ksyms_strerror(st));
    CHECK(ks.sym_status == KSYMS_OK, "symbols should be unaffected");
    CHECK(ks.detail[0] != '\0', "no explanation for the missing map");
    CHECK(!strcmp(name_of(&ks, 0xc0008010u), "_alpha+0x10"),
          "degraded to: %s", name_of(&ks, 0xc0008010u));
    ksyms_free(&ks);
}

static void test_stripped_kernel(void) {
    build_image(GOOD_PLIST, false);
    ksyms_t ks;
    ksyms_status_t st = ksyms_load(&ks, g_img, g_len);
    CHECK(st == KSYMS_ERR_NO_SYMTAB, "status %s", ksyms_strerror(st));
    CHECK(ks.prelink_status == KSYMS_OK, "kexts should be unaffected: %s",
          ks.detail);
    CHECK(!strcmp(name_of(&ks, 0xc0100122u), "com.example.one+0x122"),
          "%s", name_of(&ks, 0xc0100122u));
    CHECK(!strcmp(name_of(&ks, 0xc0008010u), "?"), "no symbols: %s",
          name_of(&ks, 0xc0008010u));
    ksyms_free(&ks);
}

/* ---------------------------------------------------- the failure path ---- */

/* Every one of these must fail AND explain itself. */
static void bad_plist(const char *what, const char *plist, const char *expect) {
    build_image(plist, true);
    ksyms_t ks;
    ksyms_load(&ks, g_img, g_len);
    CHECK(ks.prelink_status != KSYMS_OK, "%s: accepted silently", what);
    CHECK(ks.nkext_exec == 0 || ks.prelink_status != KSYMS_OK,
          "%s: built a map anyway", what);
    CHECK(strstr(ks.detail, expect) != NULL,
          "%s: detail does not mention \"%s\": %s", what, expect, ks.detail);
    /* a broken kext map must never take the kernel symbols down with it */
    CHECK(ks.sym_status == KSYMS_OK, "%s: lost the kernel symbols", what);
    ksyms_free(&ks);
}

static void test_malformed_plists(void) {
    bad_plist("kext outside __PRELINK_TEXT",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string>com.example.far</string>"
          "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0500000</integer>"
          "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x1000</integer>"
        "</dict></array>",
        "outside __PRELINK_TEXT");

    /* The later (iOS 4+) key spelling. Accepting the dict without it would
     * produce a map with no addresses; guessing that it means the same thing
     * would be a guess. Say which key the image actually uses. */
    bad_plist("newer _PrelinkExecutableLoadAddr spelling",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string>com.example.new</string>"
          "<key>_PrelinkExecutableLoadAddr</key><integer size=\"64\">0xc0100000</integer>"
          "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x1000</integer>"
        "</dict></array>",
        "_PrelinkExecutableLoadAddr");

    bad_plist("IDREF with no ID",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string IDREF=\"99\"/>"
          "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0100000</integer>"
          "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x1000</integer>"
        "</dict></array>",
        "IDREF");

    bad_plist("unclosed dict",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string>com.example.one</string>"
          "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0100000</integer>"
          "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x1000</integer>"
        "</array>",
        "closes a");

    bad_plist("load address without a size",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string>com.example.one</string>"
          "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0100000</integer>"
        "</dict></array>",
        "not the other");

    bad_plist("dict in the root array with no identifier",
        "<array><dict>"
          "<key>OSBundleRequired</key><string>Root</string>"
        "</dict></array>",
        "no CFBundleIdentifier");

    bad_plist("integer that is not a number",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string>com.example.one</string>"
          "<key>_PrelinkExecutable</key><integer size=\"64\">0xzz</integer>"
          "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x1000</integer>"
        "</dict></array>",
        "digit");

    bad_plist("undecoded XML entity in an identifier",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string>com.example&amp;one</string>"
          "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0100000</integer>"
          "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x1000</integer>"
        "</dict></array>",
        "CFBundleIdentifier contains");

    bad_plist("unknown plist element",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string>com.example.one</string>"
          "<key>Weird</key><set><string>x</string></set>"
          "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0100000</integer>"
          "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x1000</integer>"
        "</dict></array>",
        "unknown plist element");

    bad_plist("no kext carries code",
        "<array><dict>"
          "<key>CFBundleIdentifier</key><string>com.apple.kpi.mach</string>"
        "</dict></array>",
        "not one with a _PrelinkExecutable");
}

/* An overlap is survivable, so it must not fail — but it must not be silent. */
static void test_overlap_warns(void) {
    build_image(
        "<array>"
          "<dict><key>CFBundleIdentifier</key><string>com.example.one</string>"
            "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0100000</integer>"
            "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x11000</integer>"
          "</dict>"
          "<dict><key>CFBundleIdentifier</key><string>com.example.two</string>"
            "<key>_PrelinkExecutable</key><integer size=\"64\">0xc0110000</integer>"
            "<key>_PrelinkExecutableSize</key><integer size=\"64\">0x1000</integer>"
          "</dict>"
        "</array>", true);
    ksyms_t ks;
    ksyms_status_t st = ksyms_load(&ks, g_img, g_len);
    CHECK(st == KSYMS_OK, "overlap should still give a usable map");
    CHECK(strstr(ks.detail, "overlap") != NULL, "overlap not reported: '%s'",
          ks.detail);
    ksyms_free(&ks);
}

/* ------------------------------------------------ the real cache, if any --- */

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    uint8_t *b = malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f);
    *n = (size_t)sz;
    return b;
}

static void test_real_kernelcache(void) {
    /* git-ignored and large: absent on CI, present for anyone who ran the
     * IPSW extraction. Tried relative to the build dir and to the repo root. */
    static const char *TRY[] = {
        "firmware/kernel.macho",
        "../../firmware/kernel.macho",
        "../../../firmware/kernel.macho",
    };
    size_t n = 0;
    uint8_t *img = NULL;
    for (unsigned i = 0; i < sizeof TRY / sizeof TRY[0] && !img; i++)
        img = slurp(TRY[i], &n);
    if (!img) {
        printf("  (skipped: no firmware/kernel.macho — this test needs the "
               "extracted IPSW)\n");
        return;
    }

    ksyms_t ks;
    ksyms_status_t st = ksyms_load(&ks, img, n);
    CHECK(st == KSYMS_OK, "real cache: %s (%s)", ksyms_strerror(st), ks.detail);
    CHECK(ks.nsym > 10000, "real cache has %u symbols", ks.nsym);
    CHECK(ks.nkext_exec > 50, "real cache has %u kexts with code", ks.nkext_exec);

    /* Every kext with code must sit inside __PRELINK_TEXT, and the map must be
     * ordered. (ksyms_kext_at is a binary search and quietly returns nonsense
     * if it is not.) */
    for (unsigned i = 0; i < ks.nkext_exec; i++) {
        CHECK(ks.kext[i].addr >= ks.prelink_lo &&
              (uint64_t)ks.kext[i].addr + ks.kext[i].size <= ks.prelink_hi,
              "%s escapes __PRELINK_TEXT", ks.kext[i].bundle);
        if (i) CHECK(ks.kext[i].addr >= ks.kext[i-1].addr, "map is not sorted");
    }
    /* Round trip: the middle of every kext resolves back to that kext. */
    for (unsigned i = 0; i < ks.nkext_exec; i++) {
        const kext_t *k = ksyms_kext_at(&ks, ks.kext[i].addr + ks.kext[i].size / 2);
        CHECK(k == &ks.kext[i], "%s does not own its own middle",
              ks.kext[i].bundle);
    }
    /* A symbol we depend on elsewhere in the harness. */
    CHECK(ksyms_value(&ks, "_IOFindBSDRoot") != 0, "_IOFindBSDRoot not found");

    printf("  (real cache: %u kernel symbols, %u kexts, %u with code)\n",
           ks.nsym, ks.nkext, ks.nkext_exec);
    ksyms_free(&ks);
    free(img);
}

int main(void) {
    printf("S5LBox symbol/kext resolver tests\n");
    test_good_cache();
    test_no_prelink_still_names_kernel();
    test_stripped_kernel();
    test_malformed_plists();
    test_overlap_warns();
    test_macho_command_boundaries();
    test_unterminated_symbol_name_is_skipped();
    test_real_kernelcache();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
