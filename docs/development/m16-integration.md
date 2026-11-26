# M16 — Userspace and system calls

Result: PASS (four QEMU configurations and reproducible build).

Two sequential embedded Ring 3 programs with separate four-level roots, supervisor-only shared upper-half mappings, RO/X code, RW/NX data and stack, unmapped guard, TSS RSP0, DPL3 INT 0x80 gate, bounded getid/write/exit ABI, invalid pointer and unknown syscall rejection, five controlled hardware faults, restoration of original GDT/IDT/CR3/CR4 and release of fourteen PMM frames.

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54028 | 1 | `build/rc8/run-tzhbqo33` |
| 512 MiB | kvm | 119562 | 1 | `build/rc8/run-hyz2793o` |
| 1024 MiB | kvm | 250617 | 1 | `build/rc8/run-7p8xxruz` |
| 256 MiB | tcg | 54015 | 1 | `build/rc8/run-e3jpi46v` |

ISO SHA-256: `0a3664256d281cd6b26afec4ecfaf0f44a10e7507d3ddbeb08212dd7446641a2`

ELF SHA-256: `448bcf1140c839eae0518ce86e72455142a074f14cfa6145ca3e6df1ca9e9201`

## Limitations

Single BSP, interrupts disabled during user tests, trusted embedded integer-only programs, no arbitrary ELF loading, user task scheduling, syscall instruction, FPU/SIMD isolation, general fault recovery, timeout inside kernel, or physical hardware validation. This is not a production security sandbox. The kernel intentionally halts after validation.
