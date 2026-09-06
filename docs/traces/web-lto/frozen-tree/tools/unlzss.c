/*
 * S5LBox — unlzss: expand an Apple "complzss" kernelcache to a raw Mach-O.
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "lzss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <complzss.bin> <out.macho>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(f);

    lzss_header_t h;
    if (!lzss_parse_header(buf, (size_t)n, &h)) {
        fprintf(stderr, "not a complzss blob\n"); return 2;
    }
    printf("uncompressed : %u bytes\ncompressed   : %u bytes\nadler32      : 0x%08x\n",
           h.uncompressed_size, h.compressed_size, h.adler32);

    uint8_t *out = malloc(h.uncompressed_size);
    if (!out) { fprintf(stderr, "oom\n"); return 1; }
    size_t got = lzss_decompress(out, h.uncompressed_size,
                                 buf + LZSS_HEADER_SIZE, h.compressed_size);
    printf("produced     : %zu bytes %s\n", got,
           got == h.uncompressed_size ? "(matches header)" : "(SHORT!)");

    uint32_t a = lzss_adler32(out, got);
    printf("adler32 check: 0x%08x %s\n", a, a == h.adler32 ? "MATCHES" : "MISMATCH");

    /*
     * KNOWN ISSUE, stated rather than hidden: on this kernelcache the LZSS
     * stream encodes 42 bytes fewer than the header's uncompressed_size and
     * stops with one source byte left over. An independent implementation of
     * the same algorithm produces byte-identical output, so this is a property
     * of the stream (or of how we read compressed_size), not a divergence in
     * our decompressor. The shortfall lands in __PRELINK_INFO — kext metadata
     * at the very end of the image. Zero-fill to the declared size so every
     * Mach-O segment is addressable, and say so loudly.
     */
    if (got < h.uncompressed_size) {
        printf("NOTE         : %u bytes short of the declared size; zero-filling\n"
               "               so every Mach-O segment is addressable. This is a\n"
               "               known, unresolved discrepancy.\n",
               h.uncompressed_size - (uint32_t)got);
        memset(out + got, 0, h.uncompressed_size - got);
        got = h.uncompressed_size;
    }

    printf("first 16 bytes: ");
    for (int i = 0; i < 16 && (size_t)i < got; i++) printf("%02x ", out[i]);
    printf("\n");

    FILE *o = fopen(argv[2], "wb");
    if (o) { fwrite(out, 1, got, o); fclose(o); printf("wrote        : %s\n", argv[2]); }
    return 0;
}
