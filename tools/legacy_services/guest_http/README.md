# Original Safari local demo service

> Historical recordings and publishing artifacts have been removed from Git history. The summaries below describe the original runs; remaining test reports and screenshots are retained. Historical hash receipts may predate this archival cleanup.

**2026-09-08 current isolated result:** the [combined gateway](WEATHER_GATEWAY.md)
and loopback DNS serve Apple's original Weather and Stocks. Weather displays
Cupertino 72°F with six days, New York 64°F after a swipe, a Home/revisit, and
Cupertino 22°C through its own unit control. Stocks displays four fixed quotes
on the same corrected build. [131 recording](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-stocks-local/README.md)
and [actual DNS/HTTP evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/weather-stocks-local/README.md)
retain the successful run. These are visibly labeled sample data. Live services,
charts, search/add-city workflows, default distribution and persistence of this
combined build remain unqualified. The separate port-18081 program below remains
available, with its earlier failures preserved.

**2026-09-06 qualification result:** the maintained quiet daemon boots from the actual pristine accelerated-default image and original Safari renders its landing page and Next. The first form stalls with guest graphics hangs; the final process list also lacks the daemon, with exit cause unobserved. Bookmark/reload/final Home remain unfinished. No default promotion or retry. [093 failure evidence](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-http-default/README.md).

This is independently authored guest software. Original OS 1.0 launchd and dyld
load the ARMv6 executable, and the original socket APIs serve fixed HTML at
`http://127.0.0.1:18081/` **inside the emulated phone**. Opening that address in
the surrounding host browser addresses a different machine.

The verbose qualification program passed actual Safari URL entry, link, form,
bookmark and autosave/reload in [milestone 091](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-safari/README.md).
The maintained persistent, quiet program here is the proposed successor. Its
default-image qualification and distribution are still pending; it is not yet
installed in the normal starting phone. See the
[integration plan](../../../plans/local-demo-integration.md).

## Service behavior

The service waits for the guest's existing loopback address, without changing
interfaces or routes. It listens only on `127.0.0.1:18081`, accepts one client at
a time, and admits only loopback peers. Each connection has a fixed 2 KiB input
buffer, 4 KiB output buffer, finite read/write attempts and finite poll waits.
It handles partial I/O, interrupted calls, EOF and temporary nonblocking errors.
It closes each client before accepting the next one. It has no filesystem,
proxy, external-fetch or arbitrary-command endpoint.

The healthy listener loop is persistent; there is no 64-request lifetime limit.
Production builds log startup and failures. `--diagnostic` additionally logs
request/response bytes and successful connection events, and is unsuitable for
the default image. The native logging comparison checks that both modes write
identical socket response bytes through sustained and failed-I/O scenarios.
The launchd job uses `RunAtLoad`, with no dependency on modern respawn behavior.

The three routes are `/`, `/next`, and `/echo?message=...`. Responses close the
connection, disable caching, escape the bounded form value, and visibly label
their content as a local demo. This provides useful original-browser behavior;
it does not claim internet access or live information.

## Build our executable

Supply existing tools and your extracted original OS 1.0 library explicitly:

```sh
python3 tools/legacy_services/guest_http/build.py \
  --output /path/to/new-build-directory \
  --clang /path/to/clang \
  --ld64 /path/to/classic-compatible-ld64 \
  --libsystem /path/to/original/libSystem.B.dylib
```

The output directory must not exist. Each compiler/linker invocation is bounded
to 60 seconds. `build.json` records commands, diagnostics, source/tool hashes,
the binary hash and its import audit; it also retains a failed invocation.
Nothing is downloaded, and no Apple executable or library is changed.

All own ARM translation units reserve `r9` with `-ffixed-r9`: original OS 1.0
libSystem uses it as the current pthread pointer, so saving/restoring it around
an own function is insufficient. Default builds also link `standalone_runtime.c`,
which exports `environ` pointing to an owned, empty, null-terminated environment.
These are narrow startup requirements for this fixed-config service, not a full
CRT implementation. The installer requires the runtime source pin. Custom builds
must explicitly include that source when they need this environment contract.

The qualified toolchain is Clang 15 plus the classic ARM-capable ld64 used by
[the own-program execution proof](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/guest-hello/README.md).
The executable uses ARMv6 ARM instructions, an `LC_UNIXTHREAD` entry and eager
32-bit data relocations. The build omits newer optional version, function-start,
data-in-code, encryption, UUID and compact-unwind metadata. No CRT implementation
or SDK implementation is copied. The audit binds the original library SHA-256,
exact dependency/load commands, original exported symbols, executable entry and
one zero-addend eager relocation per declared import. This is an import/layout
audit, not proof that an arbitrary new program executes correctly.

The same builder can compile another independently authored program:

```sh
python3 tools/legacy_services/guest_http/build.py \
  --output /path/to/new-own-program-build \
  --clang /path/to/clang --ld64 /path/to/classic-compatible-ld64 \
  --libsystem /path/to/original/libSystem.B.dylib \
  --name own-program --source /path/to/own.c \
  --import-symbol=__exit --import-symbol=_write
```

Repeat `--source`, `--header` and `--import-symbol` as needed. Source object stems
must be distinct. Declare exactly the imports used by the own program, including
the Mach-O leading underscore. Custom code must use the same eager-pointer
import convention as `server.c`; direct lazy imports fail the audit. The HTTP
installer below refuses custom programs and diagnostic builds.

## Install only on a new filesystem copy

`rootfs_work.py` already depends on Python `cryptography`; the tested dependency
is listed in `tools/firmware/requirements.txt`. The source image must be a known,
clean, unmounted HFSX image with the supported geometry and catalog capacity.

```sh
python3 tools/legacy_services/guest_http/install.py \
  --source /path/to/qualified-clean.img \
  --source-sha256 EXPECTED_COMPLETE_SOURCE_SHA256 \
  --build /path/to/passed-quiet-build \
  --output /path/to/new-local-demo.img
```

The tool adds only `/usr/libexec/iphone1-local-http` (root, mode 0755) and its
own launchd plist (root, mode 0644). It requires all parents to exist, both new
paths to be absent and all four new catalog records to fit without splitting
nodes or changing first keys. It preserves any admitted historical alternate
header distinction rather than repairing or normalizing the image.

Before publication it compares every existing data/resource fork and every
catalog record. Only the two parent directory valences may increase by one.
The new file/thread records, bytes, ownership and permissions must match exactly;
source and program hashes are checked again afterward. Mutation occurs on a
temporary copy. A new image and adjacent JSON audit are published after the
checks, without replacing any existing destination. This tool is not a general
mounted-image editor or filesystem repair utility.

## Focused tests

The normal native CMake build registers `test_guest_http` and
`test_guest_http_lifecycle_logging`. The latter executes both logging modes and
compares their complete socket-write digests. The tests inject partial reads and
writes, interrupted calls, transient errors, timeouts, invalid peers and fatal
listener errors, and check descriptor closure across 256 requests.

```sh
python3 -m unittest discover -s tools/firmware \
  -p test_rootfs_work_permissions.py
python3 -m unittest discover -s tools/legacy_services/guest_http \
  -p test_install.py
```

The Python tests use synthetic HFSX images: they check permission admission
before mutation, old default-output compatibility, preserved file forks and
catalog records, rejected malformed sources and discard of a corrupted temporary
copy. They do not need Apple firmware. The firmware dependency must be available
in the interpreter's import path.

## Default and saved-phone policy

The original YouTube challenge probe, explicit identity provisioning and
signature evidence requirements are documented in [YOUTUBE.md](YOUTUBE.md).
It deliberately refuses the final authentication step; it is not a catalog or
video service.

Provisioning must start with the current pristine default rootfs, not the 091
data-retention fixture. A real Safari action will create a **Local demo**
bookmark; the installer never edits Apple's bookmark template. Both the rootfs
and matching Home snapshot must contain the final daemon. Existing saves and
imports retain their contents and may lack this service. There is no automatic
reset or migration. Remote asset upload and manifest changes remain a separate
reviewed release step after actual final-binary qualification.
