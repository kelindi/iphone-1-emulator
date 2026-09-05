/*
 * S5LBox — firmware loader.
 *
 * Parse -> decrypt -> place in guest RAM -> execute. Everything here operates
 * on user-supplied files, so failures are reported rather than assumed away:
 * a truncated, mis-keyed or oversized image must return a status, never
 * corrupt the machine.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "loader.h"
#include <string.h>

const char *fw_strerror(fw_status_t st) {
    switch (st) {
        case FW_OK:              return "ok";
        case FW_ERR_INVALID_ARGUMENT:return "invalid firmware loader argument";
        case FW_ERR_PARSE:       return "not a valid IMG3 container";
        case FW_ERR_NO_PAYLOAD:  return "IMG3 contains no DATA payload";
        case FW_ERR_DECRYPT:     return "decryption failed";
        case FW_ERR_NO_ROOM:     return "payload does not fit in guest RAM";
        case FW_ERR_KEY_REQUIRED:return "image is encrypted; a decryption key is required";
        case FW_ERR_IV_REQUIRED: return "image is encrypted; an explicit unwrapped IV is required";
        default:                 return "unknown error";
    }
}

static fw_status_t fw_load_img3_common(s5l8900_t *m, const uint8_t *buf,
                                       size_t len, const uint8_t *key,
                                       unsigned key_bits, const uint8_t *iv,
                                       uint32_t load_addr, fw_image_t *out) {
    if (!m || !m->ram || !buf) return FW_ERR_INVALID_ARGUMENT;
    img3_t img;
    if (img3_parse(buf, len, &img) != IMG3_OK) return FW_ERR_PARSE;
    if (!img.data || img.data_len == 0)        return FW_ERR_NO_PAYLOAD;

    /* Refuse before touching the machine if it cannot fit. */
    if (load_addr < m->ram_base) return FW_ERR_NO_ROOM;
    uint64_t off = (uint64_t)load_addr - m->ram_base;
    if (off + img.data_len > (uint64_t)m->ram_size) return FW_ERR_NO_ROOM;

    uint8_t *dst = &m->ram[off];

    /* A KBAG we could not parse means "encrypted, but we do not know how".
     * Refuse rather than copying ciphertext in and calling it success. */
    if (img.kbag.malformed) return FW_ERR_DECRYPT;

    if (img.kbag.present) {
        if (!key) return FW_ERR_KEY_REQUIRED;
        if (!iv)  return FW_ERR_IV_REQUIRED;
        uint32_t written = 0;
        if (!img3_decrypt_data_iv(&img, key, key_bits, iv, dst,
                                  (size_t)((uint64_t)m->ram_size - off),
                                  &written))
            return FW_ERR_DECRYPT;
        if (written != img.data_len) return FW_ERR_DECRYPT;
    } else {
        memcpy(dst, img.data, img.data_len);
    }

    if (out) {
        memset(out, 0, sizeof *out);
        out->ident = img.ident;
        img3_ident_str(img.ident, out->ident_str);
        memcpy(out->vers, img.vers, sizeof out->vers);
        out->vers[sizeof out->vers - 1] = '\0';
        out->load_addr     = load_addr;
        out->size          = img.data_len;
        out->was_encrypted = img.kbag.present;
        out->was_signed    = img.has_shsh;
    }
    return FW_OK;
}

fw_status_t fw_load_img3(s5l8900_t *m, const uint8_t *buf, size_t len,
                         const uint8_t *key, unsigned key_bits,
                         uint32_t load_addr, fw_image_t *out) {
    return fw_load_img3_common(m, buf, len, key, key_bits, NULL,
                               load_addr, out);
}

fw_status_t fw_load_img3_iv(s5l8900_t *m, const uint8_t *buf, size_t len,
                            const uint8_t *key, unsigned key_bits,
                            const uint8_t iv[16], uint32_t load_addr,
                            fw_image_t *out) {
    return fw_load_img3_common(m, buf, len, key, key_bits, iv,
                               load_addr, out);
}

fw_status_t fw_boot_img3(s5l8900_t *m, const uint8_t *buf, size_t len,
                         const uint8_t *key, unsigned key_bits,
                         uint32_t load_addr, fw_image_t *out) {
    fw_status_t st = fw_load_img3(m, buf, len, key, key_bits, load_addr, out);
    if (st != FW_OK) return st;

    /* Enter as the boot ROM would: SVC mode, interrupts masked, MMU off,
     * executing from the image's load address. */
    arm_reset(&m->cpu, &m->bus);
    m->cpu.r[15] = load_addr;
    return FW_OK;
}

fw_status_t fw_boot_img3_iv(s5l8900_t *m, const uint8_t *buf, size_t len,
                            const uint8_t *key, unsigned key_bits,
                            const uint8_t iv[16], uint32_t load_addr,
                            fw_image_t *out) {
    fw_status_t st = fw_load_img3_iv(m, buf, len, key, key_bits, iv,
                                     load_addr, out);
    if (st != FW_OK) return st;

    arm_reset(&m->cpu, &m->bus);
    m->cpu.r[15] = load_addr;
    return FW_OK;
}
