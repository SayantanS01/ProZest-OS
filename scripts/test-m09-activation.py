"""Boot the isolated descriptor preparation kernel and verify its two snapshots."""
import asyncio
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/m09-activation"
ISO = ROOT / "dist/ProZestOS-M09-Descriptor-Active-UEFI-x86_64.iso"
FIELDS = ("GDTR_BASE GDTR_LIMIT IDTR_BASE IDTR_LIMIT CS SS DS ES FS GS TR "
          "FS_BASE GS_BASE KERNEL_GS_BASE CR3 CR4 IF").split()
FINISH = "DESCRIPTOR ACTIVATION: PASS"


def require(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def check_transcript(log):
    lines = iter(log.splitlines())

    def expect(expected):
        actual = next(lines, None)
        require(actual == expected, f"Expected {expected!r}, got {actual!r}")

    def snapshot(phase):
        result = {}
        for key in FIELDS:
            match = re.fullmatch(rf"{phase} {key} 0x([0-9A-F]{{16}})", next(lines, ""))
            require(match, f"Missing or malformed {phase} {key}")
            result[key] = int(match[1], 16)
        return result

    expect("PROZEST DEBUGCON: ONLINE")
    expect("Test value: 0x123456789ABCDEF0")
    expect("DEBUGCON PROBE: COMPLETE")
    expect("HANDOFF VERIFIED")
    expect("EARLY IDT ACTIVE")
    before = snapshot("BEFORE")
    for stage in ("FINAL TABLES", "TRANSITION TABLES", "BRIDGE GDT ACTIVE", "KERNEL SEGMENTS ACTIVE",
                  "SECOND TEMPORARY IDT ACTIVE", "FINAL GDT ACTIVE", "TSS ACTIVE", "FINAL IDT ACTIVE"):
        expect(stage)
    after = snapshot("AFTER")
    expect("TSS ACCESS 0x000000000000008B")
    expect("CPU STATE VERIFIED")
    expect(FINISH)
    require(next(lines, None) is None, "Unexpected trailing kernel output")
    for key in ("CR3", "CR4", "FS_BASE", "GS_BASE", "KERNEL_GS_BASE", "IF"):
        require(before[key] == after[key], f"{key} changed")
    for key, value in dict(CS=8, SS=16, DS=16, ES=16, FS=16, GS=16, TR=24,
                           GDTR_LIMIT=39, IDTR_LIMIT=4095).items():
        require(after[key] == value, f"Unexpected active {key}")
    inherited = dict(CS=0x28, SS=0x30, DS=0x30, ES=0x30, FS=0x30, GS=0x30, TR=0x38,
                     GDTR_LIMIT=0x47, IDTR_BASE=0, IDTR_LIMIT=0, IF=0)
    for key, value in inherited.items():
        require(before[key] == value, f"Unexpected inherited {key}")
    require(before["GDTR_BASE"] and before["CR4"] & (1 << 12) == 0, "Unexpected GDTR or LA57")
    return after


def check_registers(state, registers):
    def compare(key, value):
        require(state[key] == int(value, 16), f"QEMU {key} differs from guest snapshot")

    for name in ("CS", "SS", "DS", "ES", "FS", "GS", "TR"):
        match = re.search(rf"^{name}\s*=([0-9a-f]+) ([0-9a-f]+)", registers, re.M)
        require(match, f"QEMU omitted {name}")
        compare(name, match[1])
        if name in ("FS", "GS"):
            compare(name + "_BASE", match[2])
    symbols = subprocess.check_output(["nm", "-n", BUILD / "kernel.elf"], text=True)
    tss = re.search(r"^([0-9a-f]+) b bootstrap_storage$", symbols, re.M)[1]
    require(re.search(rf"^TR\s*=0018 {tss} 00000067 ", registers, re.M), "Wrong cached TSS")
    for reg in ("GDT", "IDT"):
        match = re.search(rf"^{reg}=\s*([0-9a-f]+) ([0-9a-f]+)", registers, re.M)
        require(match, f"QEMU omitted {reg}")
        compare(reg + "R_BASE", match[1])
        compare(reg + "R_LIMIT", match[2])
    for reg in ("CR3", "CR4"):
        match = re.search(rf"\b{reg}=([0-9a-f]+)", registers)
        require(match, f"QEMU omitted {reg}")
        compare(reg, match[1])
    flags = re.search(r"\bRFL=([0-9a-f]+)", registers)
    require(flags and (int(flags[1], 16) >> 9) & 1 == state["IF"], "RFLAGS.IF differs")
    require("CPL=0" in registers and "HLT=1" in registers, "CPU is not halted in ring zero")
    asm = subprocess.check_output(
        ["objdump", "-d", "--disassemble=transition_checkpoint_halt", BUILD / "kernel.elf"],
        text=True,
    )
    loop = re.search(r"([0-9a-f]+):\s+fa\s+cli\s+[0-9a-f]+:\s+f4\s+hlt\s+"
                     r"([0-9a-f]+):\s+eb fc\s+jmp\s+([0-9a-f]+)", asm)
    require(loop and loop[1] == loop[3] and f"RIP={loop[2]}" in registers,
            "RIP does not follow the checkpoint HLT")
    return loop[2]


def check_mappings(text, registers, state):
    # QEMU reports supervisor/user, read, write here. NX is absent.
    ranges = []
    for line in text.splitlines():
        match = re.fullmatch(r"([0-9a-f]+)-([0-9a-f]+)\s+[0-9a-f]+\s+([-u]r[-w])",
                             line.strip())
        if match:
            ranges.append((int(match[1], 16), int(match[2], 16), match[3]))

    def mapped(name, address, length, flags):
        end = address + length
        for start, limit, permissions in ranges:
            if start <= address < limit and permissions in flags:
                address = limit
                if address >= end:
                    return
        raise RuntimeError(f"Mapping incomplete or wrong permissions for {name}")

    # Require whole objects, including the low address of each 16 KiB stack.
    symbols = {}
    for line in subprocess.check_output(["nm", "-nS", BUILD / "kernel.elf"], text=True).splitlines():
        fields = line.split()
        if len(fields) == 4:
            symbols[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
    for name in ("bootstrap_storage", "bootstrap_idt_storage", "transition_storage",
                 "machine_check_stack", "nmi_stack", "double_fault_stack"):
        mapped(name, *symbols[name], {"-rw"})
    for vector in range(32):
        for stem in ("transition_fatal_", "exception_stub_"):
            name = f"{stem}{vector}"
            mapped(name, *symbols[name], {"-r-"})
    rsp = re.search(r"\bRSP=([0-9a-f]+)", registers)
    require(rsp, "QEMU omitted RSP")
    # Check backing for a maximum 64-bit exception frame plus alignment slack.
    mapped("current BSP stack", int(rsp[1], 16) - 64, 64, {"-rw"})
    mapped("active GDT", state["GDTR_BASE"], state["GDTR_LIMIT"] + 1, {"-rw", "-r-"})


async def qmp(vm, command, **arguments):
    message = {"execute": command, "arguments": arguments}
    vm.stdin.write(json.dumps(message).encode() + b"\n")
    await vm.stdin.drain()
    while True:
        line = await vm.stdout.readline()
        require(line, f"QMP closed during {command}")
        reply = json.loads(line)
        if "event" in reply:
            continue
        require("return" in reply, f"{command}: {reply}")
        return reply["return"]


async def inspect_vm(vm, run):
    greeting = await vm.stdout.readline()
    require(greeting and "QMP" in json.loads(greeting), "Missing QMP greeting")
    await qmp(vm, "qmp_capabilities")
    kvm = await qmp(vm, "query-kvm")
    (run / "kvm.json").write_text(json.dumps(kvm) + "\n")
    log_path = run / "kernel.log"
    while vm.returncode is None:
        log = log_path.read_text() if log_path.exists() else ""
        if log.endswith(FINISH + "\n") or ": FAIL" in log:
            break
        await asyncio.sleep(0.1)
    require(vm.returncode is None, "VM exited before checkpoint")
    regs = await qmp(vm, "human-monitor-command", **{"command-line": "info registers"})
    (run / "monitor.txt").write_text(regs)
    mem = await qmp(vm, "human-monitor-command", **{"command-line": "info mem"})
    (run / "mappings.txt").write_text(mem)
    state = check_transcript(log_path.read_text())
    rip = check_registers(state, regs)
    check_mappings(mem, regs, state)
    status = await qmp(vm, "query-status")
    require(status["running"], f"Unexpected VM shutdown/pause: {status}")
    result = {"result": "PASS", "accelerator": "KVM" if kvm["enabled"] else "TCG",
              "rip": rip, "iso_sha256": hashlib.sha256(ISO.read_bytes()).hexdigest()}
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)


async def main():
    run = Path(tempfile.mkdtemp(prefix="run-", dir=BUILD))
    print(run, flush=True)
    firmware = Path("/usr/share/OVMF")
    shutil.copyfile(firmware / "OVMF_VARS_4M.fd", run / "vars.fd")
    accel = "kvm:tcg" if os.access("/dev/kvm", os.R_OK | os.W_OK) else "tcg"
    args = ["qemu-system-x86_64", "-machine", "q35,accel=" + accel, "-cpu", "qemu64",
            "-smp", "1", "-m", "512", "-display", "none", "-nic", "none", "-no-reboot",
            "-device", "virtio-scsi-pci,id=scsi",
            "-drive", f"if=none,id=cd,media=cdrom,readonly=on,format=raw,file={ISO}",
            "-device", "scsi-cd,drive=cd,bootindex=1",
            "-drive", f"if=pflash,format=raw,readonly=on,file={firmware}/OVMF_CODE_4M.fd",
            "-drive", f"if=pflash,format=raw,file={run}/vars.fd",
            "-serial", f"file:{run}/serial.log", "-debugcon", f"file:{run}/kernel.log",
            "-global", "isa-debugcon.iobase=0xe9", "-d", "int,cpu_reset,guest_errors",
            "-D", str(run / "emulator.log"), "-qmp", "stdio"]
    (run / "command.json").write_text(json.dumps(args, indent=2) + "\n")
    with (run / "stderr.log").open("wb") as errors:
        vm = await asyncio.create_subprocess_exec(*args, stdin=asyncio.subprocess.PIPE,
                                                  stdout=asyncio.subprocess.PIPE, stderr=errors)
        try:
            await asyncio.wait_for(inspect_vm(vm, run), timeout=120)
        finally:
            if vm.returncode is None:
                vm.terminate()
                try:
                    await asyncio.wait_for(vm.wait(), timeout=5)
                except asyncio.TimeoutError:
                    vm.kill()
                    await asyncio.wait_for(vm.wait(), timeout=5)


if __name__ == "__main__":
    asyncio.run(main())
