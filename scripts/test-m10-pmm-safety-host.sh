#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/m10-pmm-safety/host
flags=(-DPROZEST_PMM_SAFETY -DPROZEST_PMM_FREE -std=c11 -Wall -Wextra -Werror -g -Ikernel/include -Iexternal/limine-protocol/source/include)
sources=(tests/pmm_safety_test.c kernel/pmm_alloc.c kernel/pmm_bitmap.c kernel/pmm_plan.c)
gcc "${flags[@]}" -O2 "${sources[@]}" -o build/m10-pmm-safety/host/normal
build/m10-pmm-safety/host/normal
gcc "${flags[@]}" -O1 -fno-omit-frame-pointer -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all "${sources[@]}" -o build/m10-pmm-safety/host/sanitized
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/m10-pmm-safety/host/sanitized

# Exercise earlier allocation and reuse tests with safety guards enabled.
for suite in pmm_alloc_test pmm_free_test; do
    sources=("tests/$suite.c" kernel/pmm_alloc.c kernel/pmm_bitmap.c kernel/pmm_plan.c)
    gcc "${flags[@]}" -O2 "${sources[@]}" -o "build/m10-pmm-safety/host/$suite-normal"
    "build/m10-pmm-safety/host/$suite-normal"
    gcc "${flags[@]}" -O1 -fno-omit-frame-pointer -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all "${sources[@]}" -o "build/m10-pmm-safety/host/$suite-sanitized"
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "build/m10-pmm-safety/host/$suite-sanitized"
done
