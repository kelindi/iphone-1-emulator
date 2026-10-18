/* Builds a linked seed with the C region compiler from a text dump:
 *   offsets o0 .. o15
 *   region pc thumb index nops   followed by nops lines "pc word" */
#include "arm_wasm.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: wasmgen seed.wasm regions.json < dump\n"); return 2; }
    uint32_t off[16];
    if (scanf(" offsets") != 0) return 2;
    for (int i = 0; i < 16; i++) if (scanf("%u", &off[i]) != 1) return 2;
    arm_wasm_configure(off);
    size_t cap = 1024, n = 0;
    arm_wasm_region *r = calloc(cap, sizeof *r);
    unsigned pc, thumb, index;
    size_t nops;
    while (scanf(" region %u %u %u %zu", &pc, &thumb, &index, &nops) == 4) {
        if (n == cap) return 2;
        arm_wasm_op *ops = calloc(nops ? nops : 1, sizeof *ops);
        for (size_t i = 0; i < nops; i++)
            if (scanf("%u %u", &ops[i].pc, &ops[i].w) != 2) return 2;
        r[n++] = (arm_wasm_region){pc, index, thumb != 0, ops, nops};
    }
    arm_wasm_def *defs = calloc(n ? n : 1, sizeof *defs);
    uint8_t *wasm;
    size_t len, nd;
    if (arm_wasm_compile_pages(r, n, 0, &wasm, &len, defs, &nd)) { fprintf(stderr, "compile failed\n"); return 1; }
    FILE *f = fopen(argv[1], "wb");
    fwrite(wasm, 1, len, f);
    fclose(f);
    f = fopen(argv[2], "w");
    fputc('[', f);
    for (size_t i = 0; i < nd; i++) {
        fprintf(f, "%s{\"pc\":%u,\"thumb\":%s,\"index\":%u,\"masks\":[", i ? "," : "", defs[i].pc,
                defs[i].thumb ? "true" : "false", defs[i].index);
        for (int k = 0; k < 16; k++) fprintf(f, "%s%u", k ? "," : "", defs[i].masks[k]);
        fputs("]}", f);
    }
    fputs("]\n", f);
    fclose(f);
    printf("%zu regions, %zu bytes\n", nd, len);
    return 0;
}
