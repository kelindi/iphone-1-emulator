/*
 * S5LBox — persistent storage backing (see storage.h for why this exists).
 *
 * The container is deliberately simple and self-describing: a magic, a version,
 * and the geometry the image was written with. Restoring into a device of a
 * different shape is refused rather than silently reinterpreted, because a
 * misread NAND image is exactly the kind of failure that looks like a guest
 * bug for days.
 *
 * Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 */
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define STORAGE_MAGIC   0x334f4956u   /* "VIO3" */
#define STORAGE_VERSION 1u
#define HEADER_WORDS    8u            /* magic, version, 4 geometry, 2 reserved */
/* v1 originally wrote zero in the reserved flag word and no trailer. New
 * writers set bit 0 and append an eight-byte hash. New readers accept exact
 * legacy files; old readers ignore both the flag and trailer, so this adds
 * integrity without making existing consumers unable to restore new images. */
#define STORAGE_FLAG_CHECKSUM 1u
#define STORAGE_KNOWN_FLAGS   STORAGE_FLAG_CHECKSUM

#define STORAGE_FNV64_OFFSET 14695981039346656037ull
#define STORAGE_FNV64_PRIME  1099511628211ull

const char *storage_strerror(storage_status_t st) {
    switch (st) {
        case STORAGE_OK:            return "ok";
        case STORAGE_ERR_IO:        return "could not read or write the file";
        case STORAGE_ERR_FORMAT:    return "not an S5LBox storage image";
        case STORAGE_ERR_VERSION:   return "image written by an incompatible version";
        case STORAGE_ERR_GEOMETRY:  return "image geometry does not match the device";
        case STORAGE_ERR_TRUNCATED: return "image is shorter than its header promises";
        case STORAGE_ERR_CHECKSUM:  return "storage image failed its integrity checksum";
        case STORAGE_ERR_TRAILING:  return "storage image has trailing or reserved data";
        case STORAGE_ERR_NOMEM:     return "out of memory while staging storage image";
        default:                    return "unknown error";
    }
}

static bool wr32(FILE *f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return fwrite(b, 1, 4, f) == 4;
}
static bool rd32(FILE *f, uint32_t *v) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8)
       | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static bool wr64(FILE *f, uint64_t v) {
    return wr32(f, (uint32_t)v) && wr32(f, (uint32_t)(v >> 32));
}

static bool rd64(FILE *f, uint64_t *v) {
    uint32_t lo, hi;
    if (!rd32(f, &lo) || !rd32(f, &hi)) return false;
    *v = (uint64_t)lo | ((uint64_t)hi << 32);
    return true;
}

static uint64_t hash_bytes(uint64_t h, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= STORAGE_FNV64_PRIME; }
    return h;
}

static storage_status_t require_eof(FILE *f) {
    int c = fgetc(f);
    if (c != EOF) return STORAGE_ERR_TRAILING;
    return ferror(f) ? STORAGE_ERR_IO : STORAGE_OK;
}

static char *storage_temp_path(const char *path, const void *tag) {
    size_t n = strlen(path);
    if (n > SIZE_MAX - 40u) return NULL;
    char *tmp = malloc(n + 40u);
    if (!tmp) return NULL;
    snprintf(tmp, n + 40u, "%s.tmp.%p", path, tag);
    return tmp;
}

static bool storage_replace_file(const char *tmp, const char *path) {
#ifdef _WIN32
    return MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING |
                                  MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(tmp, path) == 0;
#endif
}

static storage_status_t finish_atomic_save(FILE *f, char *tmp,
                                           const char *path, bool ok) {
    if (fclose(f) != 0) ok = false;
    if (ok && !storage_replace_file(tmp, path)) ok = false;
    if (!ok) remove(tmp);
    free(tmp);
    return ok ? STORAGE_OK : STORAGE_ERR_IO;
}

storage_status_t storage_save_nand(const nand_t *n, const char *path) {
    if (!n || !n->data || !n->bad ||
        (n->spare_size && !n->spare) || !path) return STORAGE_ERR_IO;
    for (uint32_t i = 0; i < n->block_count; i++)
        if (n->bad[i] > 1u) return STORAGE_ERR_FORMAT;

    char *tmp = storage_temp_path(path, n);
    if (!tmp) return STORAGE_ERR_NOMEM;
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(tmp); return STORAGE_ERR_IO; }

    uint32_t pages = nand_total_pages(n);
    size_t dbytes = (size_t)pages * n->page_size;
    size_t sbytes = (size_t)pages * n->spare_size;
    uint64_t hash = STORAGE_FNV64_OFFSET;
    hash = hash_bytes(hash, n->data, dbytes);
    if (sbytes) hash = hash_bytes(hash, n->spare, sbytes);
    hash = hash_bytes(hash, n->bad, n->block_count);

    bool ok = wr32(f, STORAGE_MAGIC) && wr32(f, STORAGE_VERSION)
           && wr32(f, n->page_size) && wr32(f, n->spare_size)
           && wr32(f, n->pages_per_block) && wr32(f, n->block_count)
           && wr32(f, STORAGE_FLAG_CHECKSUM) && wr32(f, 0);

    if (ok) ok = fwrite(n->data, 1, dbytes, f) == dbytes;
    if (ok && sbytes) ok = fwrite(n->spare, 1, sbytes, f) == sbytes;
    if (ok) ok = fwrite(n->bad, 1, n->block_count, f) == n->block_count;
    if (ok) ok = wr64(f, hash);

    /* Complete a sibling file before replacing the only good checkpoint. */
    return finish_atomic_save(f, tmp, path, ok);
}

storage_status_t storage_load_nand(nand_t *n, const char *path) {
    if (!n || !n->data || !n->bad ||
        (n->spare_size && !n->spare) || !path) return STORAGE_ERR_IO;

    FILE *f = fopen(path, "rb");
    if (!f) return STORAGE_ERR_IO;

    uint32_t hdr[HEADER_WORDS];
    for (unsigned i = 0; i < HEADER_WORDS; i++) {
        if (!rd32(f, &hdr[i])) {
            storage_status_t st = ferror(f) ? STORAGE_ERR_IO : STORAGE_ERR_TRUNCATED;
            fclose(f);
            return st;
        }
    }
    if (hdr[0] != STORAGE_MAGIC)   { fclose(f); return STORAGE_ERR_FORMAT; }
    if (hdr[1] != STORAGE_VERSION) { fclose(f); return STORAGE_ERR_VERSION; }
    if ((hdr[6] & ~STORAGE_KNOWN_FLAGS) != 0 || hdr[7] != 0) {
        fclose(f);
        return STORAGE_ERR_TRAILING;
    }

    /* Refuse a geometry mismatch instead of reading the bytes anyway. */
    if (hdr[2] != n->page_size || hdr[3] != n->spare_size ||
        hdr[4] != n->pages_per_block || hdr[5] != n->block_count) {
        fclose(f);
        return STORAGE_ERR_GEOMETRY;
    }

    uint32_t pages = nand_total_pages(n);
    size_t dbytes = (size_t)pages * n->page_size;
    size_t sbytes = (size_t)pages * n->spare_size;
    uint8_t *data = malloc(dbytes);
    uint8_t *spare = sbytes ? malloc(sbytes) : NULL;
    uint8_t *bad = malloc(n->block_count);
    if (!data || (sbytes && !spare) || !bad) {
        free(data); free(spare); free(bad); fclose(f);
        return STORAGE_ERR_NOMEM;
    }

    storage_status_t st = STORAGE_OK;
    if (fread(data, 1, dbytes, f) != dbytes ||
        (sbytes && fread(spare, 1, sbytes, f) != sbytes) ||
        fread(bad, 1, n->block_count, f) != n->block_count) {
        st = ferror(f) ? STORAGE_ERR_IO : STORAGE_ERR_TRUNCATED;
    }

    uint64_t recorded = 0;
    if (st == STORAGE_OK && (hdr[6] & STORAGE_FLAG_CHECKSUM)) {
        if (!rd64(f, &recorded))
            st = ferror(f) ? STORAGE_ERR_IO : STORAGE_ERR_TRUNCATED;
        if (st == STORAGE_OK) {
            uint64_t hash = STORAGE_FNV64_OFFSET;
            hash = hash_bytes(hash, data, dbytes);
            if (sbytes) hash = hash_bytes(hash, spare, sbytes);
            hash = hash_bytes(hash, bad, n->block_count);
            if (hash != recorded) st = STORAGE_ERR_CHECKSUM;
        }
    }
    if (st == STORAGE_OK) {
        for (uint32_t i = 0; i < n->block_count; i++) {
            if (bad[i] > 1u) {
                st = STORAGE_ERR_FORMAT;
                break;
            }
        }
    }
    if (st == STORAGE_OK) st = require_eof(f);
    if (fclose(f) != 0 && st == STORAGE_OK) st = STORAGE_ERR_IO;

    if (st == STORAGE_OK) {
        memcpy(n->data, data, dbytes);
        if (sbytes) memcpy(n->spare, spare, sbytes);
        memcpy(n->bad, bad, n->block_count);
    }
    free(data); free(spare); free(bad);
    return st;
}

/* --------------------------------------------------------------- NOR ---- */

#define STORAGE_NOR_MAGIC 0x524f4e56u   /* "VNOR" */

storage_status_t storage_save_nor(const s5l_nor_t *n, const char *path) {
    if (!n || !n->data || !path) return STORAGE_ERR_IO;

    char *tmp = storage_temp_path(path, n);
    if (!tmp) return STORAGE_ERR_NOMEM;
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(tmp); return STORAGE_ERR_IO; }

    uint64_t hash = hash_bytes(STORAGE_FNV64_OFFSET, n->data, n->size);
    bool ok = wr32(f, STORAGE_NOR_MAGIC) && wr32(f, STORAGE_VERSION)
           && wr32(f, n->size) && wr32(f, STORAGE_FLAG_CHECKSUM);
    if (ok) ok = fwrite(n->data, 1, n->size, f) == n->size;
    if (ok) ok = wr64(f, hash);

    return finish_atomic_save(f, tmp, path, ok);
}

storage_status_t storage_load_nor(s5l_nor_t *n, const char *path) {
    if (!n || !n->data || !path) return STORAGE_ERR_IO;

    FILE *f = fopen(path, "rb");
    if (!f) return STORAGE_ERR_IO;

    uint32_t hdr[4];
    for (unsigned i = 0; i < 4; i++) {
        if (!rd32(f, &hdr[i])) {
            storage_status_t st = ferror(f) ? STORAGE_ERR_IO : STORAGE_ERR_TRUNCATED;
            fclose(f);
            return st;
        }
    }
    if (hdr[0] != STORAGE_NOR_MAGIC) { fclose(f); return STORAGE_ERR_FORMAT; }
    if (hdr[1] != STORAGE_VERSION)   { fclose(f); return STORAGE_ERR_VERSION; }
    if (hdr[2] != n->size)           { fclose(f); return STORAGE_ERR_GEOMETRY; }
    if ((hdr[3] & ~STORAGE_KNOWN_FLAGS) != 0) {
        fclose(f);
        return STORAGE_ERR_TRAILING;
    }

    uint8_t *data = malloc(n->size);
    if (!data) { fclose(f); return STORAGE_ERR_NOMEM; }
    storage_status_t st = fread(data, 1, n->size, f) == n->size
                        ? STORAGE_OK
                        : (ferror(f) ? STORAGE_ERR_IO : STORAGE_ERR_TRUNCATED);

    uint64_t recorded = 0;
    if (st == STORAGE_OK && (hdr[3] & STORAGE_FLAG_CHECKSUM)) {
        if (!rd64(f, &recorded))
            st = ferror(f) ? STORAGE_ERR_IO : STORAGE_ERR_TRUNCATED;
        if (st == STORAGE_OK &&
            hash_bytes(STORAGE_FNV64_OFFSET, data, n->size) != recorded)
            st = STORAGE_ERR_CHECKSUM;
    }
    if (st == STORAGE_OK) st = require_eof(f);
    if (fclose(f) != 0 && st == STORAGE_OK) st = STORAGE_ERR_IO;
    if (st != STORAGE_OK) { free(data); return st; }

    /* Contents changed underneath the directory, so rebuild it. */
    memcpy(n->data, data, n->size);
    free(data);
    s5l_nor_scan(n);
    return STORAGE_OK;
}
