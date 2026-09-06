#include "import.h"

static int error(const struct photo_api *a) { return *a->error(); }

static int enter(const struct photo_api *a, const char *path, int create) {
    if (create && a->mkdir(path, 0755) < 0 && error(a) != 17) return 0;
    int fd = a->open(path, PHOTO_RDONLY | PHOTO_NOFOLLOW);
    if (fd < 0) return 0;
    int ok = a->fchdir(fd) == 0;
    if (a->close(fd) < 0) ok = 0; /* Never retry close on an ambiguous descriptor. */
    return ok;
}

/* 1 exact, 0 absent, -1 foreign content, invalid type or I/O failure. */
static int verify(const struct photo_api *a, const struct photo_asset *s,
                  photo_u32 *digest) {
    int fd = a->open(s->name, PHOTO_RDONLY | PHOTO_NOFOLLOW | PHOTO_NONBLOCK);
    if (fd < 0) return error(a) == 2 ? 0 : -1;
    unsigned char buf[1024];
    photo_u32 off = 0, h = 2166136261U;
    int ok = 0;
    for (unsigned calls = 0; calls < 1024; calls++) {
        photo_u32 wanted = s->size - off;
        if (wanted > sizeof buf) wanted = sizeof buf;
        /* After the expected bytes, require actual EOF. */
        int n = a->read(fd, buf, wanted ? wanted : 1);
        if (n < 0 && error(a) == 4) continue;
        if (n < 0 || (photo_u32)n > (wanted ? wanted : 1)) break;
        if (!wanted) { ok = n == 0; break; }
        if (!n) break;
        int same = 1;
        for (int i = 0; i < n; i++) {
            if (buf[i] != s->bytes[off + (photo_u32)i]) same = 0;
            h = (h ^ buf[i]) * 16777619U;
        }
        if (!same) break;
        off += (photo_u32)n;
    }
    if (a->close(fd) < 0) ok = 0;
    if (ok) *digest = h;
    return ok ? 1 : -1;
}

static int write_new(const struct photo_api *a, const struct photo_asset *s) {
    int fd = a->open(s->temporary,
                    PHOTO_WRONLY | PHOTO_CREAT | PHOTO_EXCL | PHOTO_NOFOLLOW, 0644);
    if (fd < 0) return 0; /* A foreign temporary name is never removed. */
    photo_u32 off = 0;
    for (unsigned calls = 0; calls < 1024 && off < s->size; calls++) {
        photo_u32 wanted = s->size - off;
        if (wanted > 1024) wanted = 1024;
        int n = a->write(fd, s->bytes + off, wanted);
        if (n < 0 && error(a) == 4) continue;
        if (n <= 0 || (photo_u32)n > wanted) break;
        off += (photo_u32)n;
    }
    int ok = off == s->size;
    if (ok && a->fsync(fd) < 0) ok = 0;
    if (a->close(fd) < 0) ok = 0;
    if (ok && a->link(s->temporary, s->name) < 0) {
        /* Another creator may have won. The caller still checks its exact bytes. */
        ok = error(a) == 17;
    }
    if (a->unlink(s->temporary) < 0) ok = 0;
    return ok;
}

int photo_import(const struct photo_api *a, const char *root,
                 const struct photo_asset *assets, photo_u32 count) {
    if (count != 5 || a->getuid() != 0) return 70;
    if (!enter(a, root, 0) || !enter(a, "Media", 1) ||
        !enter(a, "DCIM", 1) || !enter(a, "100APPLE", 1)) return 71;
    for (photo_u32 i = 0; i < count; i++) {
        photo_u32 hash = 0;
        const struct photo_asset *s = assets + i;
        if (!s->size || s->size > 65536) return 72;
        int state = verify(a, s, &hash);
        int created = state == 0;
        if (state < 0 || (created && !write_new(a, s)) ||
            (created && verify(a, s, &hash) != 1)) {
            a->receipt(i, -1, 0, 0);
            return 73;
        }
        a->receipt(i, created ? 1 : 0, s->size, hash);
    }
    return 0;
}
