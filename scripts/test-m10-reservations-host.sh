#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/m10-memory-reservations
for mode in normal sanitized; do
    flags=(-O2)
    if [[ $mode == sanitized ]]; then flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all); fi
    gcc -std=gnu11 -Wall -Wextra -Werror -fno-builtin "${flags[@]}" \
        -Ikernel/include -Iexternal/limine-protocol/source/include \
        tests/memory_reservations_test.c kernel/memory_reservations.c kernel/page_table_inventory.c \
        -o "build/m10-memory-reservations/host-$mode"
    ASAN_OPTIONS=detect_leaks=0 "build/m10-memory-reservations/host-$mode"
done
