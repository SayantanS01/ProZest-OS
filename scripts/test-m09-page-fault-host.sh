#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/test-m09-invalid-opcode-host.sh
mkdir -p build/m09-page-fault
out=$(mktemp -d build/m09-page-fault/host-XXXXXX)
for mode in normal sanitized; do
    flags=(-O2)
    if [[ $mode == sanitized ]]; then
        flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
    fi
    gcc -Wall -Wextra -Werror -no-pie "${flags[@]}" -Iexperiments/m09-page-fault tests/page_fault_frame_test.c -o "$out/$mode"
    ASAN_OPTIONS=detect_leaks=0 "$out/$mode" | tee "$out/$mode.log"
done
