#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/m10-pmm-free/host
flags=(-DPROZEST_PMM_FREE -std=c11 -Wall -Wextra -Werror -g -Ikernel/include -Iexternal/limine-protocol/source/include)
sources=(tests/pmm_free_test.c kernel/pmm_alloc.c kernel/pmm_bitmap.c kernel/pmm_plan.c)
gcc "${flags[@]}" -O2 "${sources[@]}" -o build/m10-pmm-free/host/normal
build/m10-pmm-free/host/normal
gcc "${flags[@]}" -O1 -fno-omit-frame-pointer -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all "${sources[@]}" -o build/m10-pmm-free/host/sanitized
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/m10-pmm-free/host/sanitized
