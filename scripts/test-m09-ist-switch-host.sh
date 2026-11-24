#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/test-m09-page-fault-host.sh
mkdir -p build/m09-ist-switch
out=$(mktemp -d build/m09-ist-switch/host-XXXXXX)
for mode in normal sanitized; do
    flags=(-O2)
    if [[ $mode == sanitized ]]; then
        flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
    fi
    gcc -Wall -Wextra -Werror -no-pie "${flags[@]}" -DPROZEST_IST4_TEST -Ikernel/include -Iexperiments/m09-ist-switch tests/ist_switch_test.c \
        kernel/{bootstrap_tables,bootstrap_idt,gdt,tss,idt,idt_exceptions,cpu_tables_preflight}.c kernel/arch/x86_64/exception_stubs.S -o "$out/$mode"
    ASAN_OPTIONS=detect_leaks=0 "$out/$mode" | tee "$out/$mode.log"
done
