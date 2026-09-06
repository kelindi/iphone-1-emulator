#!/usr/bin/env bash
set -euo pipefail
p=.context/jit-executed-work
for name in collector generated-storage; do
 cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined "$p/census.c" "$p/test-$name.c" -o "$p/test-$name"
 "$p/test-$name"
done
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DFC_CAPACITY=8 "$p/census.c" "$p/test-overflow.c" -o "$p/test-overflow"
"$p/test-overflow"
