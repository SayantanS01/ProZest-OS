#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build=build/m09-page-fault
experiment=experiments/m09-page-fault
limine=external/limine-bootloader/source
iso=dist/ProZestOS-M09-Page-Fault-UEFI-x86_64.iso
mkdir -p "$build/obj" dist

flags=(-std=gnu11 -ffreestanding -DPROZEST_QEMU_DEBUGCON -DPROZEST_PF_FRAME_TEST -fno-stack-protector
       -fno-pie -fno-pic -fcf-protection=none -fno-asynchronous-unwind-tables
       -fno-unwind-tables -m64 -mno-red-zone -mgeneral-regs-only -mcmodel=kernel
       -O2 -Wall -Wextra -Werror -Ikernel/include -Iexternal/limine-protocol/source/include)
sources=("$experiment/main.c" "$experiment/probe.S" kernel/{graphics,font,console,logger,gdt,tss,idt}.c
         kernel/{idt_exceptions,exception,exception_emergency,debugcon_probe}.c
         kernel/{bootstrap_tables,bootstrap_idt,cpu_state,cpu_tables_preflight}.c
         kernel/{exception_gate_identity,descriptor_transition}.c
         kernel/arch/x86_64/{exception_stubs,transition_fatal,controlled_install}.S)
objects=()
for src in "${sources[@]}"; do
    name=${src##*/}
    obj="$build/obj/${name%.*}.o"
    gcc "${flags[@]}" -Wa,--fatal-warnings -c "$src" -o "$obj"
    objects+=("$obj")
done
ld --fatal-warnings -m elf_x86_64 -nostdlib -z max-page-size=0x1000 \
    -T boot/linker.ld -o "$build/kernel.elf" "${objects[@]}"
python3 scripts/check-m09-page-fault-elf.py "$build/kernel.elf"

# Graft only declared inputs to avoid carrying stale staging files onto the ISO.
export SOURCE_DATE_EPOCH=1767225600
xorriso -as mkisofs -quiet -R -r -J -V PROZEST_OS \
    --modification-date=2026010100000000 --set_all_file_dates 2026010100000000 \
    -e boot/limine/limine-uefi-cd.bin -no-emul-boot -efi-boot-part \
    --efi-boot-image --protective-msdos-label -o "$iso" -graft-points \
    "boot/kernel.elf=$build/kernel.elf" \
    "boot/limine/limine.conf=$experiment/limine.conf" \
    "boot/limine/limine-uefi-cd.bin=$limine/limine-uefi-cd.bin" \
    "EFI/BOOT/BOOTX64.EFI=$limine/BOOTX64.EFI"
sha256sum "$build/kernel.elf" "$iso"
