"""Run the built nested ISO in a private, bounded QEMU session."""
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
os.chdir(root)
build = Path("build/m09-panic")
run = Path(tempfile.mkdtemp(prefix="run-", dir=build))
print(run, flush=True)
elf = build / "kernel.elf"
iso = "dist/ProZestOS-M09-Panic-Test-UEFI-x86_64.iso"
expected = """PROZEST DEBUGCON: ONLINE
Test value: 0x123456789ABCDEF0
DEBUGCON PROBE: COMPLETE
SYNTHETIC PANIC TEST: START

FATAL CPU EXCEPTION
Vector: 0x000000000000000E
Exception: Page fault (#PF)
CPU error code: 0x0000000000000005
EMERGENCY REPORT COMPLETE
"""
firmware = Path("/usr/share/OVMF")
shutil.copyfile(firmware / "OVMF_VARS_4M.fd", run / "vars.fd")
accelerators = "kvm:tcg" if os.access("/dev/kvm", os.R_OK | os.W_OK) else "tcg"
monitor = run / "monitor.sock"
args = ["qemu-system-x86_64", "-machine", "q35,accel=" + accelerators,
        "-smp", "1", "-m", "512", "-display", "none", "-nic", "none",
        "-cpu", "max" if accelerators == "tcg" else "qemu64", "-boot", "order=d", "-serial", f"file:{run}/serial.log", "-no-reboot", "-device", "virtio-scsi-pci,id=scsi",
        "-drive", f"if=none,id=cd,media=cdrom,readonly=on,format=raw,file={iso}",
        "-device", "scsi-cd,drive=cd,bootindex=1",
        "-drive", f"if=pflash,format=raw,readonly=on,file={firmware}/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={run}/vars.fd",
        "-debugcon", f"file:{run}/kernel.log", "-global", "isa-debugcon.iobase=0xe9",
        "-d", "cpu_reset,guest_errors", "-D", str(run / "emulator.log"),
        "-monitor", f"unix:{monitor},server=on,wait=off"]
with (run / "stderr.log").open("w") as errors:
    vm = subprocess.Popen(args, stderr=errors)
    try:
        deadline = time.monotonic() + 120
        log = run / "kernel.log"
        while not (log.exists() and log.read_bytes().endswith(b"EMERGENCY REPORT COMPLETE\n")):
            if vm.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError(f"Boot failed or timed out. See {run}")
            time.sleep(.1)
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(3)
            sock.connect(str(monitor))

            def reply():
                data = bytearray()
                while not data.endswith(b"(qemu) "):
                    chunk = sock.recv(8192)
                    if not chunk:
                        raise EOFError("Monitor closed")
                    data.extend(chunk)
                return data.decode()

            reply()
            sock.sendall(b"info kvm\n")
            kvm = reply()
            sock.sendall(b"info registers\n")
            regs = reply()
            while "HLT=1" not in regs and time.monotonic() < deadline:
                time.sleep(.05)
                sock.sendall(b"info registers\n")
                regs = reply()
            (run / "monitor.txt").write_text(kvm + regs)
        assert log.read_bytes() == expected.encode()
        code = subprocess.check_output(["objdump", "-d", "--disassemble=exception_dispatch", elf], text=True)
        (run / "reporter.asm").write_text(code)
        halt = re.search(r"([0-9a-f]+):\s+fa\s+cli\s+[0-9a-f]+:\s+f4\s+hlt\s+([0-9a-f]+):\s+eb fc\s+jmp\s+([0-9a-f]+)", code)
        assert halt and halt[1] == halt[3] and "RIP=" + halt[2] in regs
        assert "CPL=0" in regs and "HLT=1" in regs
        assert int(re.search(r"RFL=([0-9a-f]+)", regs)[1], 16) & 0x200 == 0
        for reg, selector in dict(CS="0028", SS="0030", DS="0030", ES="0030", TR="0038").items():
            assert re.search(rf"^{reg}\s*={selector}", regs, re.M)
        assert re.search(r"GDT=\s+[0-9a-f]+ 00000047", regs)
        assert re.search(r"IDT=\s+0+ 00000000", regs)
        assert vm.poll() is None
        print("PASS", kvm.splitlines()[-2], halt[2])
    finally:
        if vm.poll() is None:
            vm.terminate()
            try:
                vm.wait(timeout=5)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait(timeout=5)
