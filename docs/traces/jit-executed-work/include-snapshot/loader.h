/*
 * S5LBox — firmware loader.
 *
 * Turns a user-supplied IMG3 file into running guest code: parse the container,
 * decrypt the payload with the user's key, copy it into guest RAM, and point
 * the CPU at it. This is the bridge between the firmware format and the
 * emulated machine, and it is the path Apple's real LLB/iBoot/kernelcache will
 * take at M3/M4.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#ifndef S5LBOX_LOADER_H
#define S5LBOX_LOADER_H

#include "img3.h"
#include "soc.h"

typedef enum {
    FW_OK = 0,
    FW_ERR_PARSE,        /* not a valid IMG3 (see the img3_status_t)       */
    FW_ERR_NO_PAYLOAD,   /* container held no DATA tag                     */
    FW_ERR_DECRYPT,      /* encrypted, but the key material was rejected   */
    FW_ERR_NO_ROOM,      /* payload does not fit in guest RAM at load_addr */
    FW_ERR_KEY_REQUIRED, /* image has a KBAG but no key was supplied       */
    FW_ERR_INVALID_ARGUMENT,
    FW_ERR_IV_REQUIRED   /* encrypted image needs an explicit unwrapped IV */
} fw_status_t;

typedef struct {
    uint32_t ident;          /* 'ibot', 'krnl', 'dtre', ...   */
    char     ident_str[5];
    char     vers[64];
    uint32_t load_addr;
    uint32_t size;           /* bytes placed in guest RAM     */
    bool     was_encrypted;
    bool     was_signed;
} fw_image_t;

/*
 * Load an IMG3 into guest RAM at `load_addr`.
 *
 * `key` may be NULL for an unencrypted image. For an encrypted image this
 * compatibility entry point refuses to guess an IV: real KBAG bytes are
 * wrapped. Call fw_load_img3_iv() with the separately published, unwrapped IV.
 *
 * On success the payload is resident in guest RAM and `out` describes it; the
 * CPU is not started (call fw_boot() or set PC yourself).
 */
fw_status_t fw_load_img3(s5l8900_t *m, const uint8_t *buf, size_t len,
                         const uint8_t *key, unsigned key_bits,
                         uint32_t load_addr, fw_image_t *out);

/* Load encrypted DATA with the separately published, unwrapped AES IV. */
fw_status_t fw_load_img3_iv(s5l8900_t *m, const uint8_t *buf, size_t len,
                            const uint8_t *key, unsigned key_bits,
                            const uint8_t iv[16], uint32_t load_addr,
                            fw_image_t *out);

/* Load, then reset the CPU and begin execution at the load address. */
fw_status_t fw_boot_img3(s5l8900_t *m, const uint8_t *buf, size_t len,
                         const uint8_t *key, unsigned key_bits,
                         uint32_t load_addr, fw_image_t *out);

fw_status_t fw_boot_img3_iv(s5l8900_t *m, const uint8_t *buf, size_t len,
                            const uint8_t *key, unsigned key_bits,
                            const uint8_t iv[16], uint32_t load_addr,
                            fw_image_t *out);

const char *fw_strerror(fw_status_t st);

#endif /* S5LBOX_LOADER_H */
