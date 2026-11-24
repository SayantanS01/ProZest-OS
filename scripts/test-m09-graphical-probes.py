"""Boot restored early M09 probe entry points using the release QMP harness."""
import asyncio
import importlib.util
import sys
from pathlib import Path

profile = sys.argv.pop()
images = {"tables": "Tables-Prepared", "validation": "Descriptor-Validation", "debugcon": "DebugCon"}
spec = importlib.util.spec_from_file_location("rc", Path(__file__).with_name("test-rc1.py"))
rc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rc)
rc.BUILD = rc.ROOT / ("build/m09-" + profile)
rc.ISO = rc.ROOT / f"dist/ProZestOS-M09-{images[profile]}-UEFI-x86_64.iso"

async def inspect(vm, run):
    await vm.stdout.readline()
    await rc.qmp(vm, "qmp_capabilities")
    expected = []
    if profile == "debugcon":
        expected.extend(["PROZEST DEBUGCON: ONLINE", "Test value: 0x123456789ABCDEF0", "DEBUGCON PROBE: COMPLETE"])
    expected.append("CPU TABLE PROBE: PASS")
    if profile != "tables":
        expected.append("DESCRIPTOR VALIDATION: PASS")
    log = run / "kernel.log"
    while not log.exists() or log.read_text().splitlines() != expected:
        rc.require(vm.returncode is None, "unexpected VM shutdown")
        await asyncio.sleep(.1)
    regs = await rc.qmp(vm, "human-monitor-command", **{"command-line": "info registers"})
    (run / "monitor.txt").write_text(regs)
    rc.require("CPL=0" in regs and "CS =0028" in regs, "unsupported inherited state")
    print(profile, "PASS", await rc.qmp(vm, "query-kvm"))

rc.inspect_vm = inspect
asyncio.run(rc.main())
