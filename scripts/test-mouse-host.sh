#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/rc12
work=$(mktemp -d build/rc12/host-XXXXXX)
for mode in normal sanitized; do
    flags=()
    if [[ $mode == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie); fi
    gcc -std=gnu11 -O2 -Wall -Wextra -Werror "${flags[@]}" -Ikernel/include kernel/desktop_mouse.c kernel/mouse.c tests/mouse_test.c -o "$work/$mode"
    "$work/$mode"
done
