# M13 — kernel heap

Result: PASS (four QEMU configurations and reproducible build).

Fixed 256 KiB contiguous PMM arena through HHDM, external extent metadata, 16-byte alignment, allocation/free, coalescing, calloc/realloc, overflow/OOM rejection and stress. Arena released after tests.

## Runtime evidence

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54028 | 1 | `build/rc5/run-clwt7k8x` |
| 512 MiB | kvm | 119562 | 1 | `build/rc5/run-rd7tgsyx` |
| 1024 MiB | kvm | 250630 | 1 | `build/rc5/run-4ttkubct` |
| 256 MiB | tcg | 54028 | 1 | `build/rc5/run-irjyjmbn` |

ISO SHA-256: `ed6e54820ac35b90984fdcd01119527caa2281dba96b15344fc729ddaac186e8`

ELF SHA-256: `16c32b1bed3de1ab29a63cbe90652a51ee86e90a8af65874c3f41739a38c8dd7`

## Limitations

Single BSP; shared kernel address space; no userspace, FPU/SIMD context switching, SMP, guard pages, persistent scheduler or physical hardware validation. Heap is bounded, not a general expanding allocator; stale pointer reuse is not detected. The kernel intentionally halts with IF=0 after validation.
