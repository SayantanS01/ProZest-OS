#!/usr/bin/env bash
# Interactive QEMU session, dedicated persistent test disk. Never attach physical disks.
set -euo pipefail
cd "$(dirname "$0")/.."
iso=dist/ProZestOS-v0.0.7-rc10-x86_64.iso
[[ -f "$iso" && ! -L "$iso" ]]
work=build/rc10/manual
mkdir -p "$work"
python3 - "$work" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0,'scripts')
from storage_fixture import create_disk,SIZE
p=Path(sys.argv[1]);assert not p.is_symlink() and not any(q.is_symlink() for q in p.parents)
d=p/'disk.img';assert not d.is_symlink()
if not d.exists():create_disk(d)
assert d.is_file() and d.stat().st_size==SIZE and d.stat().st_nlink==1
PY
run=$(mktemp -d "$work/session-XXXXXX")
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$run/vars.fd"
echo 'When prompted, press and release A. Then use the pz> prompt. Your test files persist in build/rc10/manual/disk.img.'
exec qemu-system-x86_64 -machine q35 -accel kvm -cpu host -smp 1 -m 512 -display gtk -vga std \
  -serial "file:$run/serial.log" -monitor none \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive "if=pflash,format=raw,unit=1,file=$run/vars.fd" \
  -cdrom "$iso" -boot order=d \
  -device isa-ide,id=pzide,iobase=0x1e8,iobase2=0x3ee,irq=11 \
  -drive "if=none,id=pzdisk,format=raw,cache=writeback,file=$work/disk.img" \
  -device ide-hd,bus=pzide.0,drive=pzdisk,serial=PROZEST-M17-LAB \
  -chardev "file,id=dbg,path=$run/kernel.log" -device isa-debugcon,iobase=0xe9,chardev=dbg \
  -no-reboot -no-shutdown
