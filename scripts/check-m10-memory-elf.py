"""Audit the preparation-only ELF and keep the inspected binary listings."""
import re
import subprocess
import sys
from pathlib import Path

elf = Path(sys.argv[1])


def listing(filename, *command):
    output = subprocess.check_output([*command, elf], text=True)
    (elf.parent / filename).write_text(output)
    return output


def require(ok, reason):
    if not ok:
        raise SystemExit("ELF audit failed: " + reason)


headers = listing("elf-layout.txt", "readelf", "-h", "-lW")
require("ELF64" in headers and "Advanced Micro Devices X86-64" in headers
        and re.search(r"Entry point address:\s+0xffffffff80001000", headers),
        "unexpected architecture or entry")
loads = []
for line in headers.splitlines():
    fields = line.split()
    if fields and fields[0] == "LOAD":
        start = int(fields[2], 16)
        limit = start + int(fields[5], 16)
        loads.append((start, limit, "".join(fields[6:-1])))
require(len(loads) == 3 and loads[0][0] == 0xffffffff80000000
        and [p[2] for p in loads] == ["RW", "RE", "RW"], "unexpected load segments")

symbols = {}
for line in listing("symbols.txt", "nm", "-nS").splitlines():
    fields = line.split()
    if len(fields) == 4:
        addr, size, kind, name = fields
        symbols[name] = (int(addr, 16), int(size, 16), kind)
require(not subprocess.check_output(["nm", "-u", elf]).strip(), "undefined symbol(s)")


def check_symbol(name, flags, size=None, alignment=1):
    require(name in symbols, f"missing symbol {name}")
    addr, length, _ = symbols[name]
    require(length > 0 and (size is None or length == size) and addr % alignment == 0
            and any(mode == flags and lo <= addr and addr + length <= hi
                    for lo, hi, mode in loads), f"bad size, location or alignment of {name}")


for vector in range(32):
    check_symbol(f"transition_fatal_{vector}", "RE")
    check_symbol(f"exception_stub_{vector}", "RE")
for name in ("double_fault_stack", "nmi_stack", "machine_check_stack"):
    check_symbol(name, "RW", size=16384, alignment=16)
for name in ("bootstrap_storage", "bootstrap_idt_storage", "transition_storage"):
    check_symbol(name, "RW", alignment=16)
for name in ("kernel_main", "cpu_state_capture", "transition_checkpoint_halt"):
    check_symbol(name, "RE")
require(symbols["kernel_main"][0] == 0xffffffff80001000, "entry symbol misplaced")
instructions = listing("kernel.asm", "objdump", "-d", "--no-show-raw-insn")
forbidden = re.search(r"^\s*[0-9a-f]+:\s+(sti)\b", instructions, re.M)
require(forbidden is None, "forbidden instruction present")
for instruction in ("sgdt", "sidt", "str", "rdmsr", "hlt"):
    require(re.search(rf"^\s*[0-9a-f]+:\s+{instruction}\w*\b", instructions, re.M),
            f"expected {instruction} instruction missing")
ops = re.findall(r"^\s*[0-9a-f]+:\s+(lidt|lgdt|ltr|lretq)\b", instructions, re.M)
require(ops == ["lidt", "lgdt", "lretq", "lidt", "lgdt", "ltr", "lidt"], "activation order")
profile = sys.argv[2] if len(sys.argv) > 2 else "normal"
require(len(re.findall(r"^\s*[0-9a-f]+:\s+ud2\b", instructions, re.M)) ==
        int(profile == "invalid-opcode"), "unexpected UD2")
if profile == "normal":
    require("trigger_pf" not in symbols and "trigger_ud" not in symbols, "test trigger linked")
for vector in range(32):
    body = instructions.split(f"<exception_stub_{vector}>:", 1)[1].split("\n\n", 1)[0]
    require("%rsp,%rsi" in body and f"$0x{vector:x},%edi" in body, "raw RSP or vector lost")
    require(("%cr2,%rdx" in body) == (vector == 14), "CR2 capture")
require(re.search(r"and\s+.*%rsp\n.*call.*<hardware_fatal>", instructions), "fatal call ABI")
require("memory_map_build" in symbols, "memory map validation missing")
print("M10 ELF audit passed")
