#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD="build/m09-validation"
OBJ="$BUILD/obj"
STAGE="$BUILD/iso-root"

KERNEL="$BUILD/kernel.elf"
ISO="dist/ProZestOS-M09-Descriptor-Validation-UEFI-x86_64.iso"

LIMINE="external/limine-bootloader/source"

echo "======================================"
echo "ProZest OS - M09 Descriptor Validation Build"
echo "======================================"

# Ensure required tools are installed.
for tool in gcc ld xorriso readelf; do
    command -v "$tool" >/dev/null || {
        echo "ERROR: Missing tool: $tool"
        exit 1
    }
done

# Ensure Limine boot resources exist.
for file in \
    "$LIMINE/limine-uefi-cd.bin" \
    "$LIMINE/BOOTX64.EFI" \
    boot/limine.conf \
    boot/linker.ld
do
    test -f "$file" || {
        echo "ERROR: Missing file: $file"
        exit 1
    }
done

mkdir -p \
    "$OBJ" \
    "$STAGE/boot/limine" \
    "$STAGE/EFI/BOOT" \
    dist

# Compile freestanding x86-64 kernel code.
CFLAGS=(
    -std=gnu11
    -ffreestanding
    -fno-stack-protector
    -fno-pie
    -fno-pic
    -fcf-protection=none
    -fno-asynchronous-unwind-tables
    -fno-unwind-tables
    -mno-red-zone
    -mgeneral-regs-only
    -mcmodel=kernel
    -O2
    -Wall
    -Wextra
    -Werror
    -I kernel/include
    -I external/limine-protocol/source/include
)

SOURCES=(
    experiments/m09-validation/main_m09_validation.c
    kernel/graphics.c
    kernel/font.c
    kernel/console.c
    kernel/logger.c
    kernel/gdt.c
    kernel/tss.c
    kernel/idt.c
    kernel/idt_exceptions.c
    kernel/exception.c
    kernel/bootstrap_tables.c
    kernel/bootstrap_idt.c
    kernel/cpu_tables_probe.c
    kernel/cpu_tables_preflight.c
    kernel/exception_gate_identity.c
    kernel/cpu_tables_validation_report.c
)

OBJECTS=()

echo
echo "[1/4] Compiling kernel source files"

for src in "${SOURCES[@]}"; do
    name="$(basename "${src%.c}")"
    obj="$OBJ/$name.o"

    echo "  GCC: $src"

    gcc "${CFLAGS[@]}" \
        -c "$src" \
        -o "$obj"

    OBJECTS+=("$obj")
done

echo

echo "  GAS: kernel/arch/x86_64/exception_stubs.S"

gcc \
    -c \
    -m64 \
    -mno-red-zone \
    -fcf-protection=none \
    kernel/arch/x86_64/exception_stubs.S \
    -o "$OBJ/exception_stubs.o"

OBJECTS+=("$OBJ/exception_stubs.o")

echo "[2/4] Linking kernel ELF"

ld \
    -m elf_x86_64 \
    -nostdlib \
    -z max-page-size=0x1000 \
    -T boot/linker.ld \
    -o "$KERNEL" \
    "${OBJECTS[@]}"

echo
echo "[3/4] Staging UEFI boot files"

cp "$KERNEL" \
   "$STAGE/boot/kernel.elf"

cp experiments/m09-validation/limine.conf \
   "$STAGE/boot/limine/limine.conf"

cp "$LIMINE/limine-uefi-cd.bin" \
   "$STAGE/boot/limine/limine-uefi-cd.bin"

cp "$LIMINE/BOOTX64.EFI" \
   "$STAGE/EFI/BOOT/BOOTX64.EFI"

echo
echo "[4/4] Packaging UEFI ISO"

xorriso -as mkisofs \
    -R -r -J \
    -V "PROZEST_OS" \
    -e boot/limine/limine-uefi-cd.bin \
    -no-emul-boot \
    -efi-boot-part \
    --efi-boot-image \
    --protective-msdos-label \
    "$STAGE" \
    -o "$ISO"

echo
echo "======================================"
echo "BUILD COMPLETED"
echo "======================================"
echo "Kernel: $KERNEL"
echo "ISO:    $ISO"
echo

readelf -h "$KERNEL" |
    grep -E 'Machine:|Entry point address:'

ls -lh "$KERNEL" "$ISO"
