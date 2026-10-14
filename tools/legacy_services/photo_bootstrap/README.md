# Fixed original-Photos fixture importer

This independently authored, one-shot guest program creates two visible test
photos and their thumbnail companions through original OS1 libSystem calls.
It is an isolated qualification tool. Original Photos discovers both images and
renders the first, but image navigation fails; **it is not a qualified Photos feature**.
No Apple executable or mounted ramdisk is edited by the host.

The original [PhotoLibrary discovery](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/photo-library-contract/README.md)
suggests `Media/DCIM/100APPLE` under the Photos process home. `/private/var/root`
is now verified by actual original Photos enumeration. The first image has a yellow circle on blue with one white marker;
the second has a mint square on red with two markers. A third gray image named
`BAD.JPG` is the invalid DCF filename control. These are test patterns, not
camera captures. Full JPEGs are 640×480, THM JPEGs 64×48; the original decoder
renders both thumbnails and the first full image; the remaining workflow fails.

The offline HFS writer cannot fit all directory and file-thread records without
splitting a catalog node. The proposed installation therefore adds only this
own executable and launchd plist on a new image copy. The original guest HFS
implementation performs the subsequent directory/file creation itself.

The importer requires root, enters each directory with `open(O_NOFOLLOW)` and
`fchdir`, and never replaces existing content. A matching file must pass complete
readback and EOF. New bytes go to an exclusively created temporary name, then
file `fsync`, one close and `link` publish without replacement. Only the temp
created by this invocation is removed. Existing foreign files or temp names,
symlinks and I/O errors stop the process; successful earlier files can remain
after a later failure. A complete rerun is idempotent. There is no general path
or upload endpoint. Reads and writes have finite call/chunk bounds; close is
never retried on an ambiguous descriptor. Receipts contain each completed
file's length and FNV-1a computed from actual readback bytes.

`assets.h` embeds only the independently generated JPEGs, with full hashes in
`assets.json`. `embed_assets.py` reproduces the embedding from those five fixed
files, refusing an existing output. `generate_patterns.py` reproduces the source PPM patterns with the pinned encoder. No Apple asset or
SDK implementation is included.

Host qualification uses a real disposable filesystem with explicit translation
of Darwin flags and injected partial I/O, interruption, collisions and errors:

```sh
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  tools/legacy_services/photo_bootstrap/import.c \
  tools/legacy_services/photo_bootstrap/test_import.c -o /tmp/photo-import-test
python3 tools/legacy_services/photo_bootstrap/test_import.py /tmp/photo-import-test
```

Build the own ARM program with the generic sibling
[`guest_http/build.py`](../guest_http/build.py), passing both `import.c` and
`guest.c`, both headers and the exact twelve original imports declared in
`guest.c`. The builder accepts explicit ARM-capable Clang, classic ld64 and the
user's extracted original libSystem. Its audit must verify the entire classic
ARMv6 executable and all eager relocations before any installation or guest run.
