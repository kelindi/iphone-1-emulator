/*
 * S5LBox — img3dump: inspect a real IMG3 file with the emulator's own parser.
 *
 * Purpose. Everything in this project has so far been validated against images
 * we generate ourselves, and that is exactly how the byte-swapped magic bug
 * survived a fully green test suite: our code and our fixtures shared the same
 * misunderstanding. This tool points the REAL parser at REAL Apple firmware and
 * prints what it sees, including the raw header bytes, so a mismatch between
 * our assumptions and reality is visible immediately rather than inferred.
 *
 * It ships no Apple data; the user supplies the file.
 *
 * Usage:
 *   img3dump <file.img3>                     describe the container
 *   img3dump <file.img3> -k <hex> -iv <hex> [-o out]
 *   img3dump -s <blob>                       scan a blob (e.g. a NOR dump)
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "img3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)n ? (size_t)n : 1u);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len_out = (size_t)n;
    return buf;
}

/* Parse an even-length hex string into bytes. Returns the bit count or 0. */
static unsigned parse_hex_key(const char *hex, uint8_t *out, size_t cap) {
    size_t n = strlen(hex);
    if (n % 2 || n / 2 > cap) return 0;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(&hex[i * 2], "%2x", &v) != 1) return 0;
        out[i] = (uint8_t)v;
    }
    unsigned bits = (unsigned)(n / 2) * 8u;
    return (bits == 128 || bits == 192 || bits == 256) ? bits : 0;
}

static void hexdump(const uint8_t *p, size_t n, const char *indent) {
    for (size_t i = 0; i < n; i += 16) {
        printf("%s%04zx  ", indent, i);
        for (size_t j = 0; j < 16; j++)
            if (i + j < n) printf("%02x ", p[i + j]); else printf("   ");
        printf(" |");
        for (size_t j = 0; j < 16 && i + j < n; j++) {
            uint8_t c = p[i + j];
            putchar(c >= 0x20 && c < 0x7f ? c : '.');
        }
        printf("|\n");
    }
}

/* Walk the tag stream independently of the parser so we can see every tag
 * present in a real file, including ones we do not yet handle. */
static void list_tags(const uint8_t *buf, size_t len) {
    if (len < 20) return;
    uint32_t full = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8)
                  | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    if (full > len) full = (uint32_t)len;

    printf("  tags present:\n");
    size_t off = 20;
    while (off + 12 <= full) {
        uint32_t magic = (uint32_t)buf[off] | ((uint32_t)buf[off+1] << 8)
                       | ((uint32_t)buf[off+2] << 16) | ((uint32_t)buf[off+3] << 24);
        uint32_t total = (uint32_t)buf[off+4] | ((uint32_t)buf[off+5] << 8)
                       | ((uint32_t)buf[off+6] << 16) | ((uint32_t)buf[off+7] << 24);
        uint32_t dlen  = (uint32_t)buf[off+8] | ((uint32_t)buf[off+9] << 8)
                       | ((uint32_t)buf[off+10] << 16) | ((uint32_t)buf[off+11] << 24);
        if (total < 12 || off + total > full) {
            printf("    <malformed tag at 0x%zx: total=%u>\n", off, total);
            break;
        }
        char name[5]; img3_ident_str(magic, name);
        printf("    %-6s at 0x%06zx  total %-8u data %-8u %s\n",
               name, off, total, dlen,
               (magic == IMG3_TAG_DATA || magic == IMG3_TAG_KBAG ||
                magic == IMG3_TAG_SHSH || magic == IMG3_TAG_VERS ||
                magic == IMG3_TAG_TYPE || magic == IMG3_TAG_CERT ||
                magic == IMG3_TAG_SEPO || magic == IMG3_TAG_BORD)
               ? "" : "<-- not handled by our parser");
        off += total;
    }
}

static int describe(const char *path, const char *keyhex, const char *ivhex, const char *outpath) {
    size_t len = 0;
    uint8_t *buf = slurp(path, &len);
    if (!buf) return 1;

    printf("file        : %s\n", path);
    printf("size        : %zu bytes\n", len);
    printf("first 32 bytes (the header as it really is on disk):\n");
    hexdump(buf, len < 32 ? len : 32, "  ");

    img3_t img;
    img3_status_t st = img3_parse(buf, len, &img);
    printf("parse       : %s\n", img3_strerror(st));
    if (st != IMG3_OK) {
        printf("\nOur parser rejected this file. If it is a genuine IMG3, our\n"
               "assumptions are wrong — compare the header bytes above against\n"
               "what img3_parse expects before changing anything else.\n");
        free(buf);
        return 2;
    }

    char id[5]; img3_ident_str(img.ident, id);
    printf("ident       : %s (0x%08x)\n", id, img.ident);
    printf("fullSize    : %u\n", img.full_size);
    printf("dataSize    : %u\n", img.data_size);
    printf("signedSize  : %u\n", img.signed_size);
    printf("DATA        : %u bytes\n", img.data_len);
    printf("SHSH        : %s\n", img.has_shsh ? "present" : "absent");
    if (img.vers[0]) printf("VERS        : %s\n", img.vers);
    if (img.kbag.malformed) {
        printf("KBAG        : present but NOT understood by our parser\n");
    } else if (img.kbag.present) {
        printf("KBAG        : cryptState %u, %u-bit key\n",
               img.kbag.crypt_state, img.kbag.key_bits);
        printf("  IV        : ");
        for (int i = 0; i < 16; i++) printf("%02x", img.kbag.iv[i]);
        printf("\n");
    } else {
        printf("KBAG        : absent (payload is plaintext)\n");
    }
    list_tags(buf, len);

    if (img.data && img.data_len) {
        printf("first 32 bytes of DATA%s:\n", img.kbag.present ? " (still encrypted)" : "");
        hexdump(img.data, img.data_len < 32 ? img.data_len : 32, "  ");
    }

    if (keyhex && img.data_len) {
        uint8_t key[32];
        unsigned bits = parse_hex_key(keyhex, key, sizeof key);
        if (!bits) {
            fprintf(stderr, "key must be 32, 48 or 64 hex characters\n");
            free(buf);
            return 1;
        }
        /* The KBAG's IV is wrapped on real firmware. Never silently use it as
         * an AES-CBC IV: that produces plausible-sized garbage. */
        uint8_t iv[16];
        if (!ivhex) {
            fprintf(stderr, "encrypted IMG3 decryption requires -iv with the "
                            "32-character unwrapped IV\n");
            free(buf);
            return 1;
        }
        if (parse_hex_key(ivhex, iv, sizeof iv) != 128) {
            fprintf(stderr, "iv must be 32 hex characters\n");
            free(buf);
            return 1;
        }
        uint8_t *plain = malloc(img.data_len);
        uint32_t n = 0;
        bool ok = plain && img3_decrypt_data_iv(&img, key, bits, iv, plain,
                                                 img.data_len, &n);
        if (ok) {
            printf("decrypted   : %u bytes with a %u-bit key\n", n, bits);
            printf("first 32 bytes of plaintext:\n");
            hexdump(plain, n < 32 ? n : 32, "  ");
            if (outpath) {
                FILE *o = fopen(outpath, "wb");
                if (o) {
                    fwrite(plain, 1, n, o);
                    fclose(o);
                    printf("wrote       : %s\n", outpath);
                } else {
                    fprintf(stderr, "cannot write %s\n", outpath);
                }
            }
        } else {
            printf("decrypt     : FAILED (wrong key, or no KBAG)\n");
        }
        free(plain);
    }

    free(buf);
    return 0;
}

/* Scan an arbitrary blob for IMG3 containers — for NOR dumps. */
static int scan(const char *path) {
    size_t len = 0;
    uint8_t *buf = slurp(path, &len);
    if (!buf) return 1;
    printf("scanning %s (%zu bytes) for IMG3 containers\n", path, len);

    unsigned found = 0;
    for (size_t off = 0; off + 20 <= len; off += 4) {
        uint32_t m = (uint32_t)buf[off] | ((uint32_t)buf[off+1] << 8)
                   | ((uint32_t)buf[off+2] << 16) | ((uint32_t)buf[off+3] << 24);
        if (m != IMG3_MAGIC) continue;
        img3_t img;
        if (img3_parse(buf + off, len - off, &img) != IMG3_OK) continue;
        char id[5]; img3_ident_str(img.ident, id);
        printf("  0x%08zx  %-6s  %u bytes  %s\n", off, id, img.full_size,
               img.kbag.present ? "encrypted" : "plaintext");
        found++;
        off += img.full_size - 4;
    }
    printf("found %u container(s)\n", found);
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s <file.img3> [-k <hexkey> -iv <hexiv>] [-o <out.bin>]\n"
            "       %s -s <blob>       scan a blob (e.g. a NOR dump)\n",
            argv[0], argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "-s") == 0) {
        if (argc < 3) { fprintf(stderr, "-s needs a file\n"); return 1; }
        return scan(argv[2]);
    }
    const char *keyhex = NULL, *ivhex = NULL, *outpath = NULL;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "-k") == 0) keyhex = argv[i + 1];
        else if (strcmp(argv[i], "-iv") == 0) ivhex = argv[i + 1];
        else if (strcmp(argv[i], "-o") == 0) outpath = argv[i + 1];
    }
    return describe(argv[1], keyhex, ivhex, outpath);
}
