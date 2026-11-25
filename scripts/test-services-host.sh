#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/services
work=$(mktemp -d build/services/host-XXXXXX)
for mode in normal sanitized; do
    flags=()
    if [[ $mode == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie); fi
    gcc -std=c11 -Wall -Wextra -Werror -O2 -Ikernel/include "${flags[@]}" kernel/{kheap,device_core,scheduler}.c tests/services_test.c -o "$work/$mode"
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$work/$mode"
done
