#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/m10-pmm-plan
for mode in normal sanitized; do
    flags=(-O2)
    if [[ $mode == sanitized ]]; then flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all); fi
    gcc -std=gnu11 -Wall -Wextra -Werror -fno-builtin "${flags[@]}" \
        -Ikernel/include -Iexternal/limine-protocol/source/include \
        tests/pmm_plan_test.c kernel/pmm_plan.c kernel/pmm_mapping.c \
        -o "build/m10-pmm-plan/host-$mode"
    ASAN_OPTIONS=detect_leaks=0 "build/m10-pmm-plan/host-$mode"
done
