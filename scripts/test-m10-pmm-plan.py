"""Boot the isolated descriptor preparation kernel and verify its two snapshots."""
import sys
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
BUILD = ROOT / "build/m10-pmm-plan"
ISO = ROOT / "dist/ProZestOS-M10-PMM-Plan-UEFI-x86_64.iso"
FIELDS = ("GDTR_BASE GDTR_LIMIT IDTR_BASE IDTR_LIMIT CS SS DS ES FS GS TR "
          "FS_BASE GS_BASE KERNEL_GS_BASE CR3 CR4 IF").split()
FINISH = "M10.3 COMPLETE"


PROFILE = "normal"
if PROFILE in ("invalid-opcode", "page-fault"):
    BUILD = ROOT / ("build/m10-pmm-plan-" + PROFILE)
    ISO = BUILD / "test.iso"
    FINISH = "EMERGENCY REPORT COMPLETE"
elif PROFILE != "normal":
    raise SystemExit("Unknown profile")

def require(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def check_inventory_prefix(log):
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
    expect("DESCRIPTOR ACTIVATION: PASS")
    if PROFILE != "normal":
        is_pf = PROFILE == "page-fault"
        def number(label):
            m = re.fullmatch(re.escape(label) + r": 0x([0-9A-F]{16})", next(lines, ""))
            require(m, label)
            return int(m[1], 16)
        if is_pf:
            expect("PAGE TABLE INSPECTION: PASS")
            expect("TARGET ADDRESS UNMAPPED: PASS")
            number("PRE-FAULT CR2")
            target = number("EXPECTED FAULT ADDRESS")
        site = number("EXPECTED FAULT RIP")
        rsp = number("PRE-FAULT RSP")
        expect("HARDWARE EXCEPTION TEST: " + ("PAGE FAULT" if is_pf else "UD2"))
        expect("FATAL CPU EXCEPTION")
        require(number("Vector") == (14 if is_pf else 6), "vector")
        require(number("Handler entry RSP") == (rsp & ~15) - (48 if is_pf else 40), "entry stack")
        expect("Page fault (#PF)" if is_pf else "Invalid opcode (#UD)")
        if is_pf:
            require(number("CPU error code") == 0 and number("CR2") == target, "page fault evidence")
        else:
            expect("CPU error code: not provided")
        require(number("Saved RIP") == site and number("Saved CS") == 8, "fault site")
        require(number("Saved RFLAGS") & 0x202 == 2, "flags")
        require(number("Saved RSP") == rsp and number("Saved SS") == 16, "interrupted stack")
    expect("KERNEL READY - M09")
    expect("LIMINE MEMORY MAP: FOUND")
    expect("HHDM RESPONSE: FOUND")
    expect("EXECUTABLE ADDRESS: FOUND")
    expect("PHYSICAL WIDTH: PASS")
    expect("MEMORY MAP VALIDATION: PASS")
    expect("PROZEST OS - M10 MEMORY INVENTORY")
    def number(label):
        m = re.fullmatch(re.escape(label) + r" 0x([0-9A-F]{16})", next(lines, ""))
        require(m, label)
        return int(m[1], 16)
    bits = number("Physical address bits:")
    count = number("Memory map entries:")
    described = number("Total described memory:")
    highest = number("Highest described physical address:")
    usable = number("Raw usable memory:")
    unavailable = number("Reserved and unavailable memory:")
    bootloader = number("Bootloader-reclaimable memory:")
    kernel = number("Kernel and module memory:")
    framebuffer = number("Framebuffer memory:")
    pages = number("Usable 4 KiB page candidates:")
    hhdm = number("HHDM offset:")
    require(32 <= bits <= 63 and 0 < count <= 256 and hhdm % 4096 == 0, "invalid summary")
    categories = {}
    for name in ("usable", "reserved", "ACPI reclaimable", "ACPI NVS", "bad",
                 "bootloader reclaimable", "kernel/modules", "framebuffer",
                 "reserved mapped", "unknown"):
        categories[name] = number("CATEGORY " + name)
    require(sum(categories.values()) == described and categories["usable"] == usable,
            "category totals")
    require((unavailable, bootloader, kernel, framebuffer) ==
            (described - usable, categories["bootloader reclaimable"],
             categories["kernel/modules"], categories["framebuffer"]), "reservation totals")
    total = 0
    calculated_pages = 0
    previous_end = 0
    type_totals = [0] * 10
    for i in range(count):
        match = re.fullmatch(r"REGION (?:0x[0-9A-F]{16} ){4}0x[0-9A-F]{16}", next(lines, ""))
        require(match, f"entry {i}")
        _, *fields = match[0].split()
        index, base, end, kind, candidate = [int(v, 16) for v in fields]
        require(index == i and base >= previous_end and end > base and end <= 1 << bits, "invalid region")
        expected = ((end & ~4095) - ((base+4095)&~4095)) // 4096 if kind == 0 and end >= ((base+4095)&~4095) else 0
        require(candidate == expected, "page count")
        total += end-base
        calculated_pages += candidate
        type_totals[kind if kind <= 8 else 9] += end-base
        previous_end = end
    require((total, previous_end, calculated_pages) == (described, highest, pages), "aggregate")
    require(type_totals == list(categories.values()), "type totals")
    for marker in ("LIMINE MEMORY MAP: FOUND", "HHDM RESPONSE: FOUND",
                   "PHYSICAL WIDTH: PASS", "MEMORY MAP VALIDATION: PASS"):
        require(marker in log.splitlines(), marker)
    expect("MEMORY CLASSIFICATION: PASS")
    reservation_lines = []
    line = next(lines, None)
    while line is not None and line.startswith("RESERVATION "):
        reservation_lines.append(line)
        line = next(lines, None)
    require(len(reservation_lines) >= 12 and
            any("kernel ELF load image including BSS" in x and " physical " in x for x in reservation_lines) and
            any("stack bounds UNRESOLVED" in x for x in reservation_lines) and
            any("tree bounds UNRESOLVED" in x for x in reservation_lines), "reservation inventory")
    require(line == "PRELIMINARY RESERVATION INVENTORY: COMPLETE", "inventory completion")
    expect("PHYSICAL MEMORY ALLOCATOR: NOT INITIALIZED")
    expect("MEMORY INVENTORY: PASS")
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
    after["usable"] = usable
    after["pages"] = pages
    return after


def check_reservations_prefix(log):
    marker="MEMORY INVENTORY: PASS\n"
    require(marker in log,"M10.1 inventory missing")
    prefix, rest=log.split(marker,1)
    state=check_inventory_prefix(prefix+marker)
    lines=rest.splitlines()
    require(lines[-1]=="M10.2 COMPLETE","M10.2 completion missing")
    stages=("PHYSICAL WIDTH RECHECK: PASS","KERNEL RESERVATION: PASS",
            "KERNEL IMAGE OWNERSHIP: PASS","RESERVATION PAGE ZERO: PASS",
            "KERNEL IMAGE RANGE: PASS","DESCRIPTOR RESERVATIONS: PASS",
            "IST STACK RESERVATIONS: PASS","NORMAL STACK PROTECTION: PASS",
            "ACTIVE PAGE TABLE INVENTORY: PASS","BOOTLOADER MEMORY PROTECTION: PASS",
            "FRAMEBUFFER RESPONSE PROTECTED","FRAMEBUFFER RESERVATION: PASS",
            "FRAMEBUFFER RANGE: PASS","RESERVATION ENGINE STORAGE: PASS",
            "RESERVATION NORMALIZATION: PASS","ELIGIBLE RANGE VALIDATION: PASS",
            "ALLOCATOR-SAFE INVENTORY: PASS","M10.2 COMPLETE")
    for stage in stages: require(stage in lines,stage)
    require(not any(x.endswith(": FAIL") for x in lines),"guest protection failure")
    def val(label):
        m=re.search(r"^"+re.escape(label)+r" 0x([0-9A-F]{16})$",rest,re.M)
        require(m,label); return int(m[1],16)
    counts=[val("PAGE TABLE FRAMES "+n+":") for n in ("PML4","PDPT","PD","PT")]
    tables=[tuple(int(x,16) for x in line.split()[1:]) for line in lines if line.startswith("TABLE ")]
    require(len(tables)==sum(counts) and counts[0]==1 and len({p for p,l in tables})==len(tables),"table frames")
    for level,count in zip((4,3,2,1),counts): require(sum(l==level for p,l in tables)==count,"table level")
    original=val("Original usable page candidates:")
    excluded=val("Unique candidate pages excluded:")
    eligible=val("Allocator-eligible pages:")
    bytes_=val("Allocator-eligible bytes:")
    count=val("Eligible range count:")
    largest=val("Largest eligible range bytes:")
    require(original==state["pages"] and original==excluded+eligible and bytes_==eligible*4096,"eligible totals")
    protected=[]
    for line in lines:
        if line.startswith("PROTECTED "):
            _,a,b,why=line.split(); protected.append((int(a,16),int(b,16),int(why,16)))
    require(protected and all(a<b and a%4096==b%4096==0 for a,b,w in protected),"protected ranges")
    require({1,2,3,4,5,6,7,8,9,10}.issubset({w for a,b,w in protected}),
            "incomplete reservation provenance")
    merged=[]
    for a,b,w in protected:
        if merged and a<=merged[-1][1]: merged[-1]=(merged[-1][0],max(b,merged[-1][1]))
        else: merged.append((a,b))
    ranges=[]
    for line in lines:
        if line.startswith("ELIGIBLE 0x"):
            _,a,b=line.split(); ranges.append((int(a,16),int(b,16)))
    require(len(ranges)==count and sum((b-a)//4096 for a,b in ranges)==eligible and
            sum(b-a for a,b in ranges)==bytes_ and
            max((b-a for a,b in ranges),default=0)==largest,"eligible ranges")
    for i,(a,b) in enumerate(ranges):
        require(a<b and a%4096==b%4096==0 and (i==0 or ranges[i-1][1]<=a),"eligible order")
        require(all(b<=x or a>=y for x,y in merged),"protected overlap")
    usable_regions=[]
    for line in prefix.splitlines():
        if line.startswith("REGION "):
            _,idx,a,b,kind,pages=line.split()
            if int(kind,16)==0:
                start=(int(a,16)+4095)&~4095
                end=int(b,16)&~4095
                if start<end: usable_regions.append((start,end))
    expected=[]
    for a,b in usable_regions:
        cursor=a
        for x,y in merged:
            if y<=cursor: continue
            if x>=b: break
            if x>cursor: expected.append((cursor,min(x,b)))
            cursor=min(b,max(cursor,y))
        if cursor<b: expected.append((cursor,b))
    require(ranges==expected,"eligible ranges differ from independent subtraction")
    require(all(any(a<=p and p+4096<=b for a,b,w in protected) for p,l in tables),"unprotected table")
    require(any(w==1 and a==0 for a,b,w in protected),"page zero")
    state.update(eligible=eligible,excluded=excluded,tables=counts)
    return state

def check_transcript(log):
    marker="M10.2 COMPLETE\n"
    require(marker in log,"M10.2 prefix missing")
    prefix,rest=log.split(marker,1)
    state=check_reservations_prefix(prefix+marker)
    lines=rest.splitlines()
    require(lines and lines[-1]==FINISH,"M10.3 completion missing")
    for label in ("PMM ARCHITECTURE: RANGE-INDEXED BITMAP",
                  "PMM PLAN BSS PROTECTION: PASS","PMM METADATA SIZING: PASS",
                  "METADATA EXCLUSION VALIDATION: PASS","METADATA MAPPING VALIDATION: PASS",
                  "PHYSICAL ADDRESS/INDEX VALIDATION: PASS","PMM METADATA PLAN: PASS",
                  "PHYSICAL MEMORY ALLOCATOR: NOT INITIALIZED",FINISH):
        require(label in lines,label)
    require(not any(x.endswith(": FAIL") for x in lines),"PMM guest failure")
    def val(label):
        m=re.search(r"^"+re.escape(label)+r" 0x([0-9A-F]{16})$",rest,re.M)
        require(m,label); return int(m[1],16)
    page=val("Physical page size:")
    pre=val("Pre-metadata eligible pages:")
    capacity=val("Bitmap capacity:")
    required=val("Bitmap required bytes:")
    storage=val("Bitmap storage bytes:")
    bss=val("Plan BSS bytes:")
    total=val("Metadata total bytes:")
    metadata_pages=val("Metadata reserved pages:")
    unused=val("Unused trailing bitmap bits:")
    base=val("Proposed metadata physical base:")
    end=val("Proposed metadata physical end:")
    virtual=val("Metadata HHDM virtual base:")
    mapped=val("Metadata mapping pages verified:")
    count=val("Final managed ranges:")
    final=val("Final managed pages:")
    bytes_=val("Final managed bytes:")
    require(page==4096 and pre==state["eligible"] and required==(pre+7)//8 and
            metadata_pages==(required+4095)//4096 and storage==metadata_pages*4096 and
            capacity==storage*8 and unused==capacity-pre and
            total==storage+bss and end-base==storage and mapped==metadata_pages and
            virtual==base+int(re.search(r"^HHDM offset: 0x([0-9A-F]{16})$",prefix,re.M)[1],16) and
            final==pre-metadata_pages and bytes_==final*4096,"PMM arithmetic")
    source=[]
    for line in prefix.splitlines():
        if line.startswith("ELIGIBLE 0x"):
            _,a,b=line.split(); source.append((int(a,16),int(b,16)))
    placement=next(((a,b) for a,b in source if b-a>=storage),None)
    require(placement and base==placement[0] and end<=placement[1],"non-deterministic placement")
    expected=[]; next_index=0
    for a,b in source:
        if (a,b)==placement: a=end
        if a<b:
            expected.append((a,b,next_index)); next_index+=(b-a)//4096
    managed=[]
    for line in lines:
        if line.startswith("MANAGED 0x"):
            _,a,b,index=line.split(); managed.append((int(a,16),int(b,16),int(index,16)))
    require(managed==expected and count==len(expected) and next_index==final,"managed ranges")
    samples=[]
    for line in lines:
        if line.startswith("MAP_SAMPLE "):
            _,a,index=line.split(); samples.append((int(a,16),int(index,16)))
    want=[]
    for a,b,index in expected:
        want.extend(((a,index),(b-4096,index+(b-a)//4096-1)))
    require(samples==want,"physical/index round trips")
    state.update(pre=pre,final=final,bitmap_bytes=required,metadata_pages=metadata_pages,
                 metadata_total=total,metadata_base=base,metadata_end=end,
                 metadata_virtual=virtual,metadata_storage=storage)
    return state

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
        ["objdump", "-d", "--disassemble=" + ("transition_checkpoint_halt" if PROFILE == "normal" else "hardware_fatal"), BUILD / "kernel.elf"],
        text=True,
    )
    loops = re.finditer(r"([0-9a-f]+):\s+fa\s+cli\s+[0-9a-f]+:\s+f4\s+hlt\s+"
                     r"([0-9a-f]+):\s+eb fc\s+jmp\s+([0-9a-f]+)", asm)
    loop = next((m for m in loops if m[1] == m[3] and f"RIP={m[2]}" in registers), None)
    require(loop,
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
    root_match = re.search(r"\bCR3=([0-9a-f]+)", regs)
    require(root_match, "QEMU omitted CR3")
    root = int(root_match[1], 16) & ~4095
    root_dump = await qmp(vm, "human-monitor-command",
                          **{"command-line": f"xp /4gx 0x{root+0x800:x}"})
    root_dump += await qmp(vm, "human-monitor-command",
                           **{"command-line": f"xp /2gx 0x{root+0xff0:x}"})
    (run / "root-table.txt").write_text(root_dump)
    require("0x" in root_dump.lower() and
            any(int(x,16)&1 for x in re.findall(r"0x([0-9a-f]{16})",root_dump.lower())),
            "physical CR3 root could not be inspected")
    mem = await qmp(vm, "human-monitor-command", **{"command-line": "info mem"})
    (run / "mappings.txt").write_text(mem)
    state = check_transcript(log_path.read_text())
    reported_tables={int(p,16) for p in re.findall(r"^TABLE 0x([0-9A-F]{16}) ",
                                                   log_path.read_text(), re.M)}
    root_children={int(value,16)&0x000ffffffffff000 for value in
                   re.findall(r"0x([0-9a-f]{16})",root_dump.lower())
                   if int(value,16)&1}
    require(root_children and root_children.issubset(reported_tables),
            "QEMU physical PML4 children differ from guest table inventory")
    rip = check_registers(state, regs)
    check_mappings(mem, regs, state)
    require(any((match := re.fullmatch(r"([0-9a-f]+)-([0-9a-f]+)\s+[0-9a-f]+\s+(-rw)",
                                     line.strip())) and
                int(match[1],16)<=state["metadata_virtual"] and
                state["metadata_virtual"]+state["metadata_storage"]<=int(match[2],16)
                for line in mem.splitlines()), "metadata HHDM mapping missing")
    await qmp(vm, "screendump", filename=str(run / "screen.ppm"))
    status = await qmp(vm, "query-status")
    require(status["running"], f"Unexpected VM shutdown/pause: {status}")
    result = {"result": "PASS", "accelerator": "KVM" if kvm["enabled"] else "TCG",
              "rip": rip, "iso_sha256": hashlib.sha256(ISO.read_bytes()).hexdigest()}
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    result["usable"] = state["usable"]
    result["pages"] = state["pages"]
    result["eligible"] = state["eligible"]
    result["excluded"] = state["excluded"]
    result["tables"] = state["tables"]
    result.update({key: state[key] for key in ("pre","final","bitmap_bytes","metadata_pages","metadata_total","metadata_base","metadata_end")})
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)


async def main():
    run = Path(tempfile.mkdtemp(prefix="run-", dir=BUILD))
    print(run, flush=True)
    firmware = Path("/usr/share/OVMF")
    shutil.copyfile(firmware / "OVMF_VARS_4M.fd", run / "vars.fd")
    accel = "tcg" if os.getenv("M10_ACCEL") == "tcg" else ("kvm:tcg" if os.access("/dev/kvm", os.R_OK | os.W_OK) else "tcg")
    args = ["qemu-system-x86_64", "-machine", "q35,accel=" + accel, "-cpu", "qemu64",
            "-smp", "1", "-m", sys.argv[1] if len(sys.argv) > 1 else "512", "-display", "none", "-nic", "none", "-no-reboot",
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
