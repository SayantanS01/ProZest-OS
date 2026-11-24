# M09 final integration and v0.0.7-rc1

**2026-11-24 · PASS on the tested virtual hardware.** Normal boot owns the CPU tables, displays initialization results, and reaches a CLI/HLT loop. All deliberate fault triggers are isolated from the normal ELF.

## Development history

| Checkpoint | Result |
|---|---|
| Initial M09 | Built and inspected GDT/TSS, IDT, bootstrap storage and CPU state. |
| Preparation, structural preflight and identity | Checked descriptor layouts and all 32 linked handlers before activation. |
| Debugcon and synthetic panic | Established independent port output and fatal halt evidence. |
| 2.17G | Demonstrated synthetic software re-entry guard. |
| 2.17H | Built the compatible bridge GDT and two temporary IST0 IDTs. |
| 2.17I | Activated permanent tables and independently checked the busy TSS. |
| 2.17J | Captured a real five-word #UD frame and the exact fault RIP. |
| 2.17K | Captured a six-word #PF frame and verified CR2 against a checked non-present address. |
| 2.17L | Demonstrated same-privilege stack switching with experimental IST4. |
| Integration | Restored graphical boot, integrated one fatal entry ABI, rebuilt historical tests and verified two real exceptions with the release reporter. |

## Architecture

```text
GDT (40 bytes): 00 null | 08 code | 10 data | 18..27 TSS descriptor
TSS (104 bytes): RSP0=0 | IST1 #DF | IST2 NMI | IST3 #MC | IST4..7=0
IDT (4096 bytes): vectors 0..31 → 32 executable fatal stubs; 32..255 absent
```

Each production IST has a distinct aligned, permanently allocated 16 KiB stack. The early IDT is installed before framebuffer work. Its gates select inherited code 0x28. LGDT bridge preserves both code/data selector pairs. LRETQ changes CS, segment loads change DS/ES/FS/GS/SS, then LIDT selects the second temporary IST0 IDT using CS 0x08. LGDT permanent, LTR 0x18 and final LIDT follow in order. Only the last table refers to the three IST stacks. Preflight checks require an available TSS; postchecks require a busy descriptor and allow accessed-bit changes. Validation gates the ready message. See Step H and I reports for the per-instruction handoff contract.

Each production stub supplies vector in RDI, the untouched hardware entry RSP in RSI and, for #PF, CR2 in RDX before stack alignment. CLD and alignment before CALL satisfy the C ABI. Vector classification in C determines the saved-frame offset. Vectors 8, 10–14, 17, 21, 29 and 30 have an error slot for hardware delivery (#CP, #VC and #SX depend on processor features/vendor). Other covered vectors begin with RIP. Software INT to an exception gate is outside this ABI. Raw offsets from RIP are CS +8, flags +16, old RSP +24 and SS +32. A supplied error word precedes RIP by eight bytes. The reporter takes the atomic panic guard before reading the frame, checks vector range and pointer alignment, prints to port 0xE9 and halts. An aligned pointer alone cannot establish readable memory. No exception handler returns. The production and experiment stub objects are linked separately.

## Nested IST safety review

| Risk | Disposition within the supported single-BSP configuration |
|---|---|
| Ordinary maskable IRQ interruption | Addressed by keeping IF clear. |
| Re-entry after acquiring the software guard | Partially addressed. The nested branch emits a short message and halts. Synthetic re-entry tests this branch in the original emergency interface. |
| Reuse of the same IST | Unresolved. Hardware reloads the same top and can overwrite the original frame before either C invocation examines the flag. |
| NMI and faults during NMI | Partially addressed by the dedicated IST. NMI blocking after delivery does not exclude ordinary exceptions inside that handler. No IRET-based recovery exists. |
| Double-fault escalation | A dedicated TSS stack can help delivery when the previous stack fails. A fault during #DF delivery can shut the CPU down. |
| Machine check | A separate stack is available; machine state can already be unusable. CR4.MCE was clear in these boots. |
| Failed emergency stack or diagnostics | Independent port output avoids graphics and logger state, but entry pushes, C stack and mapped code/data are still required. Another #PF can replace architectural CR2; the stub samples it immediately. |
| Exhaustion | Unresolved. Separate stack extents and alignment are validated. No guard pages or guarantee of sufficient depth. The Step L canary and 184-byte observation were experimental only. |
| Empty inherited IDT and early IST0 period | Remain vulnerable before first LIDT and while early delivery still requires a working normal stack. |
| SMP | Deferred. The global guard and bootstrap tables do not implement per-CPU fatal state. |

Retain fatal-only semantics and defer advanced nesting until independently designed and tested. [Intel SDM, interrupt and exception handling](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) and [AMD64 APM, system programming](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf).

## QA and artifacts

942/942 checks passed normally and with ASan/UBSan (875 earlier plus 67 integrated checks testing all vector classifications and raw frame offsets and rejecting missing, misaligned or out-of-range inputs). LeakSanitizer was disabled under the sandbox tracer. Real #UD and #PF tests additionally exercised the new release reporter, including both raw layouts and the CR2 branch.

The normal ELF has three load segments RW/RX/RW, entry 0xffffffff80001000, 32 fatal stubs, writable permanent descriptor storage, the audited activation instruction order and no unresolved symbols, STI, UD2 or test-trigger symbols. The build uses freestanding GCC with warnings as errors. A clean source-only tree with the same external Limine checkouts produced byte-identical ELF and ISO. All 19 pre-existing ISO hashes matched afterward. build/rc1/historical.sha256 records the full manifest. The v0.0.6 and IST-switch hashes matched the requested values.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| dist/ProZestOS-v0.0.7-rc1-x86_64.iso | 3,723,264 | 0921e7aa98c8ffbd49cb0332eddc067257a6e00043614ada77be4df69e1dd0d3 |
| build/rc1/kernel.elf | 36,392 | b5be8c9ea8f3bad081f7e2234b5f0f751ecdbff16c4c7373bc8fb98b673d1d32 |

## Runtime matrix

All rows built from current sources and passed under KVM. Each row reached the listed acceptance condition with zero unexpected resets. Prefixed evidence paths are relative to build/rc1/regressions unless stated otherwise. Each run contains kernel.log, monitor.txt and emulator.log.

| Case | Expected and observed | Successful KVM evidence |
|---|---|---|
| Normal graphical | Console ready, private GDT/IDT, busy TR, intentional HLT | build/rc1/run-6y8htam_ |
| CPU table preparation | Probe passed | m09-tables/run-l1x2c51a |
| Structural and handler validation | Both layers passed | m09-validation/run-bb_i22n5 |
| Debug console | Exact test pattern and completion | m09-debugcon/run-92r7wfq1 |
| Synthetic panic | First report completed, halted in dispatcher | m09-panic/run-8jak_mgk |
| Synthetic nested guard | Vector 13 re-entry, halted in emergency reporter | m09-nested/run-50j7zyvj |
| Transition preparation | Temporary tables validated, inherited active state preserved | m09-transition/run-3vhe1l2p |
| Activation | Permanent registers and selectors verified | m09-activation/run-jep52n57 |
| Genuine #UD | Saved RIP matched UD2, no error slot | m09-invalid-opcode/run-eaif88g_ |
| Genuine #PF | CR2 matched target, hardware error zero | m09-page-fault/run-xrsrybey |
| IST4 | Frame at alternate top minus 40, final canary intact | m09-ist-switch/run-7causjvg |
| Release reporter #UD | Actual vector, fault site, old stack and absent error verified | build/rc1-invalid-opcode/run-0hnwcfm0 |
| Release reporter #PF | Actual vector, hardware error, CR2 and saved frame verified | build/rc1-page-fault/run-ngeewiyu |

TCG passed normal boot, #UD, #PF and IST4. TCG exposed live SS zero after IST delivery while saved SS remained 0x10; KVM exposed live SS 0x10. TCG's monitor labels the cached TR available while the guest reads the hardware-updated busy descriptor in memory. Actual screenshot build/rc1/run-6y8htam_/screen.png shows the original four-color framebuffer and complete readable status block without clipping. The screenshot was converted losslessly from QEMU screendump.

## Problems resolved

Earlier scripts consumed ignored generated entry points and configurations. Their inputs now live under experiments. The v0.0.6 rebuild uses its original graphical entry separately. The first panic test sampled registers while graphical reporting was still running. Polling for HLT within the deadline fixed the test. The first IST4 TCG run compared live SS to the pre-fault snapshot; the test now validates saved and handler SS independently. The integrated host test needed the Limine include path even though it never boots the kernel. Generated outputs, captures and logs remain ignored by existing rules.

## Interview review

1. **Who starts the kernel?** Limine transfers control from UEFI to its higher-half entry.
2. **Why install a temporary IDT early?** The inherited IDT is empty here.
3. **Why use a bridge GDT?** Both old and new selector pairs must resolve during the switch.
4. **What changes CS?** A far control transfer using the new selector.
5. **Why check FS/GS bases?** Selector values alone do not determine their effective bases.
6. **What does LTR do?** Loads the task register and marks the selected TSS descriptor busy.
7. **What is the accessed bit?** Hardware may mark a loaded code/data descriptor as accessed.
8. **Why keep IF clear?** Hardware IRQ handlers are not ready.
9. **Which stack does IST zero use here?** Same-privilege entry uses the interrupted stack.
10. **How does IST select another stack?** The IDT gate indexes one of the active TSS pointers.
11. **Why save entry RSP?** It locates the untouched hardware frame.
12. **Why align before calling C?** Generated code expects the System V stack convention.
13. **What does saved RIP identify for #UD?** The exact offending instruction.
14. **What does CR2 contain?** The virtual address causing the last page fault.
15. **Why can an error word be zero?** A non-present supervisor data read sets none of the low fault bits.
16. **How can a guard help re-entry?** Atomic exchange identifies a prior caller without depending on a logger lock.
17. **Why can that guard be too late?** The processor pushes the new frame before software runs.
18. **What does the stack watermark prove?** Only the deepest observed changed byte in that run.
19. **How was halt verified?** Compare monitor HLT and RIP to the linked instruction following HLT.
20. **What makes a build reproducible?** Explicit source inputs, fixed packaging metadata and matching bytes from a clean build.

**M10 readiness:** sufficient to begin a read-only memory-map inventory and reservation design. First account for kernel segments, firmware, framebuffer, boot stack, page tables and all active CPU structures before allocating any physical page. Real hardware and multi-CPU validation are separate work.

## Proposed commits and exact changed-file inventory


### 1. Preserve baseline builds and emergency diagnostics

- boot/linker.ld
- kernel/exception.c
- scripts/build-cpu-diag.sh
- scripts/build-m09-state.sh
- scripts/build-m09-tables.sh
- scripts/build-m09-validation.sh
- scripts/build-repro-v006.sh
- docs/development/m09-2.17g-nested-guard.md
- experiments/cpu-diag/limine.conf
- experiments/cpu-diag/main_diag.c
- experiments/m09-debugcon/limine.conf
- experiments/m09-debugcon/main_m09_debugcon.c
- experiments/m09-nested/limine.conf
- experiments/m09-nested/main.c
- experiments/m09-panic/limine.conf
- experiments/m09-panic/main_m09_panic.c
- experiments/m09-state/limine.conf
- experiments/m09-state/main_m09_state.c
- experiments/m09-tables/limine.conf
- experiments/m09-tables/main_m09.c
- experiments/m09-validation/limine.conf
- experiments/m09-validation/main_m09_validation.c
- experiments/v006/main.c
- kernel/debugcon_probe.c
- kernel/exception_emergency.c
- kernel/include/debugcon_probe.h
- kernel/include/exception_emergency.h
- kernel/include/qemu_debugcon.h
- scripts/build-m09-debugcon.sh
- scripts/build-m09-nested.sh
- scripts/build-m09-panic.sh
- scripts/test-m09-graphical-probes.py
- scripts/test-m09-nested.py
- scripts/test-m09-panic.py

### 2. Prepare and activate permanent descriptors

- kernel/cpu_tables_preflight.c
- kernel/include/cpu_tables_preflight.h
- docs/development/m09-2.17h-descriptor-transition.md
- docs/development/m09-2.17i-descriptor-activation.md
- experiments/m09-activation/limine.conf
- experiments/m09-activation/main.c
- experiments/m09-transition/limine.conf
- experiments/m09-transition/main.c
- kernel/arch/x86_64/controlled_install.S
- kernel/arch/x86_64/transition_fatal.S
- kernel/descriptor_transition.c
- kernel/include/descriptor_activation.h
- kernel/include/descriptor_transition.h
- scripts/build-m09-activation.sh
- scripts/build-m09-transition.sh
- scripts/check-m09-activation-elf.py
- scripts/check-m09-transition-elf.py
- scripts/test-m09-activation-host.sh
- scripts/test-m09-activation.py
- scripts/test-m09-transition-host.sh
- scripts/test-m09-transition.py
- tests/descriptor_activation_test.c
- tests/descriptor_transition_test.c

### 3. Add genuine fault and alternate-stack experiments

- kernel/arch/x86_64/exception_stubs.S
- kernel/idt_exceptions.c
- kernel/tss.c
- docs/development/m09-2.17j-invalid-opcode.md
- docs/development/m09-2.17k-page-fault.md
- docs/development/m09-2.17l-ist-switch.md
- experiments/m09-invalid-opcode/frame.h
- experiments/m09-invalid-opcode/limine.conf
- experiments/m09-invalid-opcode/main.c
- experiments/m09-invalid-opcode/probe.S
- experiments/m09-ist-switch/limine.conf
- experiments/m09-ist-switch/main.c
- experiments/m09-ist-switch/profile.h
- experiments/m09-page-fault/frame.h
- experiments/m09-page-fault/limine.conf
- experiments/m09-page-fault/main.c
- experiments/m09-page-fault/probe.S
- scripts/build-m09-invalid-opcode.sh
- scripts/build-m09-ist-switch.sh
- scripts/build-m09-page-fault.sh
- scripts/check-m09-invalid-opcode-elf.py
- scripts/check-m09-ist-switch-elf.py
- scripts/check-m09-page-fault-elf.py
- scripts/test-m09-invalid-opcode-host.sh
- scripts/test-m09-invalid-opcode.py
- scripts/test-m09-ist-switch-host.sh
- scripts/test-m09-ist-switch.py
- scripts/test-m09-page-fault-host.sh
- scripts/test-m09-page-fault.py
- tests/invalid_opcode_frame_test.c
- tests/ist_switch_test.c
- tests/page_fault_frame_test.c

### 4. Integrate graphical M09 release candidate

- kernel/main.c
- boot/rc1.conf
- docs/development/m09-final-integration.md
- docs/releases/v0.0.7-rc1.md
- kernel/arch/x86_64/fatal_entry.S
- kernel/include/fatal_frame.h
- scripts/build-rc1.sh
- scripts/check-rc1-elf.py
- scripts/test-rc1-host.sh
- scripts/test-rc1.py
- tests/fatal_frame_test.c

These groups describe the final accumulated diff. Split shared-file hunks as needed for buildable intermediate commits, particularly the IST4 preflight and unified reporter additions. Keep build/, dist/, external dependencies and all VM captures out of source commits. No files were staged.
