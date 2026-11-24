#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/m09-activation
out=$(mktemp -d build/m09-activation/host-XXXXXX)
printf 'Host test evidence in %s\n' "$out"

common=(-std=gnu11 -g -Wall -Wextra -Werror -fno-pie -no-pie -I kernel/include)
sources=(kernel/{gdt,tss,idt,idt_exceptions,bootstrap_tables,bootstrap_idt,cpu_tables_preflight,exception_gate_identity}.c
         kernel/arch/x86_64/exception_stubs.S)

for mode in normal sanitized; do
    flags=(-O2)
    if [[ $mode == sanitized ]]; then
        flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    fi
    for suite in cpu_tables_preflight exception_gate_identity descriptor_transition descriptor_activation; do
        extra=()
        if [[ $suite == descriptor_transition ]]; then
            extra=(kernel/descriptor_transition.c kernel/arch/x86_64/transition_fatal.S)
        fi
        binary="$out/$suite-$mode"
        gcc "${common[@]}" "${flags[@]}" "tests/${suite}_test.c" "${sources[@]}" "${extra[@]}" -o "$binary"
        # These tests use static tables. LeakSanitizer cannot run under the sandbox tracer.
        ASAN_OPTIONS=detect_leaks=0 "$binary" > "$binary.log" 2>&1 || { cat "$binary.log"; exit 1; }
        printf '%s %s: ' "$suite" "$mode"
        tail -n 2 "$binary.log"
    done
done
