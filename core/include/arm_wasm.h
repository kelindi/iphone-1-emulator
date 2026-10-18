/* Guarded 1 KiB ARM/Thumb region compiler producing a linked WebAssembly seed. */
#ifndef ARM_WASM_H
#define ARM_WASM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t pc, w; } arm_wasm_op;
typedef struct {
    uint32_t pc, index;
    bool thumb;
    const arm_wasm_op *ops;
    size_t nops;
} arm_wasm_region;
typedef struct {
    uint32_t pc, index, masks[16];
    bool thumb;
} arm_wasm_def;

/* offsets: the 16 values of emu_jit_offset(0..15). */
void arm_wasm_configure(const uint32_t offsets[16]);
/* Compiles regions into one module whose functions occupy env.table from
 * table_base. On success returns 0, sets *wasm and *len (malloc'd) and fills
 * defs[0..*ndefs) (defs needs room for n entries; index is the table slot). */
int arm_wasm_compile_pages(const arm_wasm_region *regions, size_t n, uint32_t table_base,
                           uint8_t **wasm, size_t *len,
                           arm_wasm_def *defs, size_t *ndefs);
#endif
