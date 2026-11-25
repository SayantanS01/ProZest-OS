# M15 — kernel multitasking

Result: PASS (four QEMU configurations and reproducible build).

Three ring-0 workers with independent heap-backed 16 KiB stacks, LAPIC timer preemption, software yield, round-robin selection and task exit. Workers must each receive four hardware timer preemptions before voluntary yield. Timer masked, original IRQ gates restored, stacks and temporary table frames released.

## Runtime evidence

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54024 | 1 | `build/rc7/run-2qkcdye9` |
| 512 MiB | kvm | 119571 | 1 | `build/rc7/run-5xoo0a2i` |
| 1024 MiB | kvm | 250626 | 1 | `build/rc7/run-790pl_50` |
| 256 MiB | tcg | 54024 | 1 | `build/rc7/run-ya8u13dq` |

ISO SHA-256: `258a25f43bd572e0e191f3dbe781bc5567aec7d703bcaeb075c8f471e5046bd4`

ELF SHA-256: `dbb0ae05fe33000c6bc768c1f75ac827266394a34c4e1484a762e452e670121b`

## Limitations

Single BSP; shared kernel address space; no userspace, FPU/SIMD context switching, SMP, guard pages, persistent scheduler or physical hardware validation. Heap is bounded, not a general expanding allocator; stale pointer reuse is not detected. The kernel intentionally halts with IF=0 after validation.
