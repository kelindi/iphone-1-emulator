/* A small WebAssembly expression tree and binary encoder for the guest-code
 * compiler. Nodes live in a per-compile arena; encoding follows the byte
 * layout WABT's wat2wasm produces for the equivalent folded text. */
#ifndef WASM_IR_H
#define WASM_IR_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct wn wn;
typedef struct { int depth; } wlabel;

typedef struct {
    uint8_t *buf;
    size_t len, cap;
    bool oom;
} wbuf;

typedef struct {
    uint8_t *mem;
    size_t used, cap;
    bool oom;
} warena;

enum {
    WN_OP,      /* opcode with children evaluated first, then immediates */
    WN_SEQ,     /* statements in order                                    */
    WN_BLOCK, WN_LOOP, WN_IF,
    WN_BR, WN_BR_IF, WN_BR_TABLE
};

struct wn {
    uint8_t kind, nk;
    uint16_t op;            /* opcode; 0xfcxx for prefixed                 */
    uint8_t imm_kind;       /* see WI_*                                    */
    int64_t imm;            /* const / local index / offset / func index   */
    uint32_t imm2;          /* memarg alignment (log2) / table index       */
    wn **k;                 /* children (WN_OP, WN_SEQ)                    */
    uint32_t cap;
    wlabel *label;          /* WN_BLOCK/WN_LOOP own it; WN_BR* target it   */
    wlabel **table;         /* WN_BR_TABLE targets                          */
    uint32_t ntable;
    wn *then_, *else_;      /* WN_IF                                        */
};

enum { WI_NONE, WI_I32, WI_I64, WI_U32, WI_MEM, WI_F32Z, WI_F64Z, WI_CALLI };

void warena_init(warena *a, size_t cap);
void warena_reset(warena *a);
void warena_free(warena *a);
void *walloc(warena *a, size_t n);

wn *w_op(warena *a, uint16_t op, int nk, ...);
wn *w_i32(warena *a, int32_t v);
wn *w_i64(warena *a, int64_t v);
wn *w_get(warena *a, uint32_t local);
wn *w_set(warena *a, uint32_t local, wn *v);
wn *w_global_get(warena *a, uint32_t g);
/* align < 0 selects the natural alignment of `op`. */
wn *w_load(warena *a, uint16_t op, uint32_t offset, int align, wn *addr);
wn *w_store(warena *a, uint16_t op, uint32_t offset, int align, wn *addr, wn *v);
wn *w_seq(warena *a);
void w_push(warena *a, wn *seq, wn *item);
wlabel *w_label(warena *a);
wn *w_block(warena *a, wlabel *l, wn *body);
wn *w_loop(warena *a, wlabel *l, wn *body);
wn *w_if(warena *a, wn *cond, wn *then_, wn *else_);
wn *w_br(warena *a, wlabel *l);
wn *w_br_if(warena *a, wlabel *l, wn *cond);
wn *w_br_table(warena *a, wlabel **t, uint32_t n, wlabel *def, wn *index);
wn *w_call(warena *a, uint32_t func, int nk, ...);

void wbuf_byte(wbuf *b, uint8_t v);
void wbuf_bytes(wbuf *b, const void *p, size_t n);
void wbuf_u32(wbuf *b, uint32_t v);
void wbuf_s64(wbuf *b, int64_t v);
void wbuf_free(wbuf *b);
/* Encodes `n` as an instruction sequence (no trailing `end`). */
void w_encode(wbuf *b, const wn *n);
#endif
