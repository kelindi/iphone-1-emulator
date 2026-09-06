#!/bin/sh
set -eu
cd /home/vercel-sandbox/iphone-1
base=.context/guest-hello
clang -target armv6-apple-darwin8 -marm -Os -fno-stack-protector -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-pic -c "$base/hello.c" -o "$base/hello.o"
"$base/cctools-src/cctools/ld64/src/ld/ld" -arch armv6 -ios_version_min 1.0 -e _start -no_pie -no_uuid -no_compact_unwind -no_version_load_command -no_function_starts -no_data_in_code_info -no_encryption -o "$base/iphone1-own-program" "$base/hello.o" "$base/original-readonly/usr__lib__libSystem.B.dylib"
