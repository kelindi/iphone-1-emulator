#!/bin/bash
set -euo pipefail
cd /home/vercel-sandbox/iphone-1
base=.context/guest-safari
for source in server http; do
/usr/bin/clang -target armv6-apple-darwin8 -marm -Os -fno-stack-protector -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-pic -c "$base/$source.c" -o "$base/$source.o"
done
.context/guest-hello/cctools-src/cctools/ld64/src/ld/ld -arch armv6 -ios_version_min 1.0 -e _start -no_pie -no_uuid -no_compact_unwind -no_version_load_command -no_function_starts -no_data_in_code_info -no_encryption -o "$base/iphone1-local-http" "$base/server.o" "$base/http.o" .context/guest-hello/original-readonly/usr__lib__libSystem.B.dylib
