/*
 * S5LBox — machoinfo: describe the decompressed XNU kernel.
 *
 * With -k it also prints the prelinked kext load map, which is the table you
 * want in front of you when a profile says "66.9% of samples in one kext":
 * it is the same map bootkernel resolves hot PCs against, printed by a tool
 * that does not have to boot anything.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "ksyms.h"
#include "macho.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <kernel.macho> [-k] [-r <addr>]... "
                        "[-s <substring>]\n"
                        "  -k  dump the prelinked kext load map\n"
                        "  -r  resolve one address to a symbol or kext+offset\n"
                        "  -s  list kernel symbols containing substring\n",
                argv[0]);
        return 1;
    }
    bool     want_kexts = false;
    const char *symbol_search = NULL;
    unsigned nres = 0;
    uint32_t res[64];
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-k")) want_kexts = true;
        else if (!strcmp(argv[i], "-r") && i + 1 < argc && nres < 64)
            res[nres++] = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            symbol_search = argv[++i];
        else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 1; }
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(f);

    macho_t m;
    macho_status_t st = macho_parse(b, (size_t)n, &m);
    printf("parse      : %s\n", macho_strerror(st));
    if (st != MACHO_OK) return 2;

    printf("cputype    : %u  subtype %u  filetype %u\n", m.cputype, m.cpusubtype, m.filetype);
    printf("segments   : %u\n", m.segment_count);
    for (unsigned i = 0; i < m.segment_count; i++) {
        macho_segment_t *s = &m.segments[i];
        printf("  %-16s vm 0x%08x..0x%08x  file 0x%08x + %u\n",
               s->name, s->vmaddr, s->vmaddr + s->vmsize, s->fileoff, s->filesize);
    }
    printf("vm range   : 0x%08x .. 0x%08x (%.1f MB)\n",
           m.vm_low, m.vm_high, (m.vm_high - m.vm_low) / 1048576.0);
    if (m.has_uuid) {
        printf("uuid       : ");
        for (unsigned i = 0; i < sizeof m.uuid; i++)
            printf("%02x", m.uuid[i]);
        printf("\n");
    } else {
        printf("uuid       : NONE\n");
    }
    if (m.has_entry) printf("entry PC   : 0x%08x   (initial SP 0x%08x)\n", m.entry, m.entry_sp);
    else             printf("entry PC   : NOT FOUND (no LC_UNIXTHREAD)\n");
    if (m.has_symtab)
        printf("symtab     : %u nlist entries, %u bytes of strings\n", m.nsyms, m.strsize);
    else
        printf("symtab     : NONE (stripped)\n");

    if (!want_kexts && !nres && !symbol_search) { free(b); return 0; }

    ksyms_t ks;
    ksyms_load(&ks, b, (size_t)n);
    printf("\nkernel symbols : %u  (%s)\n", ks.nsym, ksyms_strerror(ks.sym_status));
    printf("prelinked kexts: %u total, %u with code  (%s)\n",
           ks.nkext, ks.nkext_exec, ksyms_strerror(ks.prelink_status));
    if (ks.detail[0]) printf("  ! %s\n", ks.detail);

    if (want_kexts && ks.nkext) {
        printf("\n=== PRELINKED KEXT LOAD MAP (__PRELINK_TEXT 0x%08x..0x%08x) ===\n",
               ks.prelink_lo, ks.prelink_hi);
        printf("    %-44s %-10s %-10s %-9s %s\n",
               "bundle identifier", "load addr", "end", "size", "kmod_info");
        for (unsigned i = 0; i < ks.nkext; i++) {
            const kext_t *k = &ks.kext[i];
            if (!k->has_exec) continue;
            printf("    %-44s 0x%08x 0x%08x %-9u 0x%08x\n",
                   k->bundle, k->addr, k->addr + k->size, k->size, k->kmod_info);
        }
        printf("    --- no executable (KPI pseudo-extensions, never a hot PC) ---\n");
        for (unsigned i = 0; i < ks.nkext; i++)
            if (!ks.kext[i].has_exec) printf("    %s\n", ks.kext[i].bundle);
    }

    if (symbol_search) {
        printf("\n=== KERNEL SYMBOLS CONTAINING %s ===\n", symbol_search);
        for (unsigned i = 0; i < ks.nsym; i++)
            if (strstr(ks.sym[i].name, symbol_search))
                printf("    0x%08x %s\n", ks.sym[i].value, ks.sym[i].name);
    }

    for (unsigned i = 0; i < nres; i++) {
        char nm[192];
        printf("resolve 0x%08x -> %s\n", res[i],
               ksyms_resolve(&ks, res[i], nm, sizeof nm));
    }

    ksyms_free(&ks);
    free(b);
    return 0;
}
