#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/rc3
out=$(mktemp -d build/rc3/host-XXXXXX)
for mode in normal sanitized; do
    flags=()
    if [[ $mode == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie); fi
    gcc -std=c11 -O2 -Wall -Wextra -Werror "${flags[@]}" -Ikernel/include kernel/vmm.c tests/vmm_test.c -o "$out/$mode"
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$out/$mode"
done
