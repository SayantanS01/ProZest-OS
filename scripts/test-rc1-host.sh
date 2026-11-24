#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/test-m09-ist-switch-host.sh
mkdir -p build/rc1
out=$(mktemp -d build/rc1/host-XXXXXX)
for mode in normal sanitized; do
    flags=(-O2)
    if [[ $mode == sanitized ]]; then
        flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
    fi
    gcc -Wall -Wextra -Werror -ffunction-sections -Wl,--gc-sections -no-pie "${flags[@]}" -Ikernel/include -Iexternal/limine-protocol/source/include tests/fatal_frame_test.c kernel/exception.c -o "$out/$mode"
    ASAN_OPTIONS=detect_leaks=0 "$out/$mode" | tee "$out/$mode.log"
done
