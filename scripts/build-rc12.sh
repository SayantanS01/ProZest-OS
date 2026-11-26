#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build=${BUILD_DIR:-build/rc12}
iso=${ISO_PATH:-build/rc12/test.iso}
boot=boot/rc12
limine=external/limine-bootloader/source
mkdir -p "$build/obj" "$(dirname "$iso")"
flags=(-DPROZEST_STAGE=20 -DPROZEST_VMM_FAULT=0 -std=gnu11 -ffreestanding -DPROZEST_PMM_CONTIGUOUS -DPROZEST_PMM_SAFETY -DPROZEST_PMM_FREE -DPROZEST_PMM_ALLOC -DPROZEST_UNIFIED_FATAL -DPROZEST_QEMU_DEBUGCON -fno-stack-protector
       -fno-pie -fno-pic -fcf-protection=none -fno-asynchronous-unwind-tables
       -fno-unwind-tables -m64 -mno-red-zone -mgeneral-regs-only -mcmodel=kernel
       -fno-builtin -O2 -Wall -Wextra -Werror -Ikernel/include -Iexternal/limine-protocol/source/include)
sources=(kernel/main_mouse.c kernel/shell_mouse.c kernel/desktop_mouse.c kernel/mouse.c kernel/pzfs.c kernel/user_abi.c kernel/kheap.c kernel/device_core.c kernel/scheduler.c kernel/vmm.c kernel/irq_platform.c kernel/console_shell.c kernel/{graphics,font,logger,gdt,tss,idt}.c
         kernel/{idt_exceptions,exception,exception_emergency,debugcon_probe}.c
         kernel/{bootstrap_tables,bootstrap_idt,cpu_state,cpu_tables_preflight}.c
         kernel/{exception_gate_identity,descriptor_transition,memory_map,memory_reservations,page_table_inventory,pmm_plan,pmm_mapping,pmm_bitmap,pmm_alloc,pmm}.c
         kernel/arch/x86_64/{fatal_entry,transition_fatal,controlled_install,irq_entry,scheduler_entry,user_entry}.S)
objects=()
for src in "${sources[@]}"; do
    name=${src##*/}
    obj="$build/obj/${name%.*}.o"
    gcc "${flags[@]}" -Wa,--fatal-warnings -c "$src" -o "$obj"
    objects+=("$obj")
done
ld --fatal-warnings -m elf_x86_64 -nostdlib -z max-page-size=0x1000 \
    -T "$boot/linker.ld" -o "$build/kernel.elf" "${objects[@]}"
python3 scripts/check-mouse-elf.py 20 "$build/kernel.elf"
# Never overwrite an existing ISO with different bytes.
tmpiso=$(mktemp -d "$build/iso-XXXXXX")/test.iso
export SOURCE_DATE_EPOCH=1767225600
xorriso -as mkisofs -quiet -R -r -J -V PROZEST_OS \
    --modification-date=2026010100000000 --set_all_file_dates 2026010100000000 \
    -e boot/limine/limine-uefi-cd.bin -no-emul-boot -efi-boot-part \
    --efi-boot-image --protective-msdos-label -o "$tmpiso" -graft-points \
    "boot/kernel.elf=$build/kernel.elf" \
    "boot/limine/limine.conf=$boot/limine.conf" \
    "boot/limine/limine-uefi-cd.bin=$limine/limine-uefi-cd.bin" \
    "EFI/BOOT/BOOTX64.EFI=$limine/BOOTX64.EFI"
if [[ -e "$iso" ]]; then cmp "$tmpiso" "$iso"; else cp --update=none "$tmpiso" "$iso"; cmp "$tmpiso" "$iso"; fi
sha256sum "$build/kernel.elf" "$iso"
