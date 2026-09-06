#!/usr/bin/env bash
set -euo pipefail
cd /home/vercel-sandbox/iphone-1
p=.context/jit-fallback-census
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined "$p/census.c" "$p/test-collector.c" -o "$p/test-collector"
"$p/test-collector"
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DFC_CAPACITY=8 "$p/census.c" "$p/test-overflow.c" -o "$p/test-overflow"
"$p/test-overflow"
