#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/rc8
work=$(mktemp -d build/rc8/host-XXXXXX)
for mode in normal sanitized; do
    flags=()
    if [[ $mode == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie); fi
    gcc -std=gnu11 -O2 -Wall -Wextra -Werror "${flags[@]}" -Ikernel/include kernel/user_abi.c tests/user_abi_test.c -o "$work/$mode"
    "$work/$mode"
done
