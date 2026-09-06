/*
 * S5LBox — IMG3 firmware container parser.
 *
 * This is the first code to touch untrusted, user-supplied files, so it is
 * written defensively: every declared size is validated against the real buffer
 * length before any read, and all arithmetic is done in 64-bit to make overflow
 * impossible. A malformed image must produce an error, never an out-of-bounds
 * read.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "img3.h"
#include "aes.h"
#include <string.h>

/* On-disk header: magic, fullSize, sizeNoPack, sigCheckArea, ident. */
#define IMG3_HEADER_SIZE 20u
#define IMG3_TAG_HEADER  12u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void img3_ident_str(uint32_t ident, char *out) {
    /* Idents read naturally when byte-reversed (0x69626f74 -> "ibot"). */
    out[0] = (char)((ident >> 24) & 0xff);
    out[1] = (char)((ident >> 16) & 0xff);
    out[2] = (char)((ident >> 8)  & 0xff);
    out[3] = (char)( ident        & 0xff);
    out[4] = '\0';
}

const char *img3_strerror(img3_status_t st) {
    switch (st) {
        case IMG3_OK:            return "ok";
        case IMG3_ERR_INVALID_ARGUMENT: return "invalid IMG3 parser argument";
        case IMG3_ERR_TOO_SMALL: return "buffer too small for an IMG3 header";
        case IMG3_ERR_BAD_MAGIC: return "not an IMG3 container";
        case IMG3_ERR_BAD_SIZE:  return "declared size does not fit the buffer";
        case IMG3_ERR_BAD_TAG:   return "a tag runs past the end of the container";
        default:                 return "unknown error";
    }
}

bool img3_decrypt_data_iv(const img3_t *img, const uint8_t *key, unsigned key_bits,
                          const uint8_t iv[16], uint8_t *out, size_t out_cap,
                          uint32_t *out_len) {
    if (!img || !img->data || !img->kbag.present || img->kbag.malformed ||
        !key || !out || !iv || key_bits != img->kbag.key_bits ||
        out_cap < img->data_len) return false;

    aes_ctx_t ctx;
    if (!aes_init(&ctx, key, key_bits)) return false;

    /* Real payloads are not always a whole number of blocks; CBC covers the
     * aligned prefix and the remainder is passed through unchanged. */
    uint32_t whole = img->data_len & ~(AES_BLOCK_SIZE - 1u);
    uint32_t tail  = img->data_len - whole;

    if (whole && !aes_cbc_decrypt(&ctx, iv, img->data, out, whole))
        return false;
    if (tail) memcpy(out + whole, img->data + whole, tail);

    if (out_len) *out_len = img->data_len;
    return true;
}

static void parse_kbag(const uint8_t *d, uint32_t len, img3_kbag_t *k) {
    /* cryptState(4) keyBits(4) IV(16) key(keyBits/8)
     *
     * Every early return marks the bag malformed rather than merely absent. The
     * distinction matters: the loader decides whether to decrypt from this, and
     * silently treating an unparseable KBAG as "not encrypted" would copy
     * ciphertext into guest RAM and report success. */
    k->malformed = true;
    if (len < 24) return;
    k->crypt_state = rd32(d);
    k->key_bits    = rd32(d + 4);
    memcpy(k->iv, d + 8, 16);

    if (k->key_bits != 128u && k->key_bits != 192u && k->key_bits != 256u)
        return;
    uint32_t key_len = k->key_bits / 8u;
    if ((uint64_t)24 + key_len > (uint64_t)len) return;
    memcpy(k->key, d + 24, key_len);
    k->present   = true;
    k->malformed = false;
}

img3_status_t img3_parse(const uint8_t *buf, size_t len, img3_t *out) {
    if (!out) return IMG3_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    if (!buf || len < IMG3_HEADER_SIZE) return IMG3_ERR_TOO_SMALL;

    /*
     * A real IMG3 begins with the bytes 33 67 6d 49, which read as "3gmI" in a
     * hex dump; a little-endian rd32 of those bytes is 0x496d6733 == IMG3_MAGIC.
     * This is the same convention the tags use (a DATA tag is stored "ATAD" and
     * reads back as 0x44415441), so the header must follow it too. Comparing
     * against the byte-swapped value instead would reject every genuine Apple
     * image while our own synthetic test images still parsed.
     */
    if (rd32(buf) != IMG3_MAGIC) return IMG3_ERR_BAD_MAGIC;

    out->full_size   = rd32(buf + 4);
    out->data_size   = rd32(buf + 8);
    out->signed_size = rd32(buf + 12);
    out->ident       = rd32(buf + 16);

    /* fullSize covers the whole container; it must fit what we were given. */
    if (out->full_size < IMG3_HEADER_SIZE || (uint64_t)out->full_size > (uint64_t)len)
        return IMG3_ERR_BAD_SIZE;
    /* dataSize counts the tag area that follows the header. */
    if ((uint64_t)out->data_size + IMG3_HEADER_SIZE > (uint64_t)out->full_size)
        return IMG3_ERR_BAD_SIZE;

    uint64_t off = IMG3_HEADER_SIZE;
    const uint64_t end = out->full_size;

    while (off + IMG3_TAG_HEADER <= end) {
        const uint8_t *t = buf + off;
        uint32_t magic     = rd32(t);
        uint32_t total_len = rd32(t + 4);
        uint32_t data_len  = rd32(t + 8);

        /* A tag must be at least its header, must not exceed the container,
         * and its payload must fit inside the tag. */
        if (total_len < IMG3_TAG_HEADER)              return IMG3_ERR_BAD_TAG;
        if (off + (uint64_t)total_len > end)          return IMG3_ERR_BAD_TAG;
        if ((uint64_t)data_len + IMG3_TAG_HEADER > (uint64_t)total_len)
            return IMG3_ERR_BAD_TAG;

        const uint8_t *d = t + IMG3_TAG_HEADER;

        switch (magic) {
            case IMG3_TAG_DATA:
                out->data     = d;
                out->data_len = data_len;
                break;
            case IMG3_TAG_KBAG:
                if (!out->kbag.present) parse_kbag(d, data_len, &out->kbag);
                break;
            case IMG3_TAG_SHSH:
                out->has_shsh = (data_len > 0);
                break;
            case IMG3_TAG_VERS: {
                /* VERS holds a length-prefixed string. */
                if (data_len >= 4) {
                    uint32_t slen = rd32(d);
                    if ((uint64_t)slen + 4 <= (uint64_t)data_len) {
                        uint32_t n = slen < sizeof out->vers - 1
                                   ? slen : (uint32_t)(sizeof out->vers - 1);
                        memcpy(out->vers, d + 4, n);
                        out->vers[n] = '\0';
                    }
                }
                break;
            }
            default: break;   /* TYPE/CERT/SEPO/BORD and anything else: skip */
        }

        off += total_len;
    }

    /* A declared container must be exactly a sequence of complete tags.
     * Accepting an unexplained 1..11-byte suffix makes a malformed fullSize
     * look valid and weakens every caller's framing assumptions. */
    if (off != end) return IMG3_ERR_BAD_TAG;
    return IMG3_OK;
}
