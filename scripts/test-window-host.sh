#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/rc13
work=$(mktemp -d build/rc13/host-XXXXXX)
for mode in normal sanitized; do
    flags=()
    if [[ $mode == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie); fi
    gcc -std=gnu11 -O2 -Wall -Wextra -Werror "${flags[@]}" -Ikernel/include kernel/desktop_window.c kernel/mouse_flame.c kernel/window.c tests/window_test.c -o "$work/$mode"
    "$work/$mode"
done
