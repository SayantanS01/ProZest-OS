# M14 — initial device drivers

Result: PASS (four QEMU configurations and reproducible build).

16550 UART loopback and external TX, read-only PCI bus 0 enumeration, polled PS/2 A-key make/break. QMP injects A; manual boot requires pressing A at the prompt. No storage, network, USB, mouse, DMA or general keyboard layout driver.

## Runtime evidence

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54024 | 1 | `build/rc6/run-_e_f0fk9` |
| 512 MiB | kvm | 119571 | 1 | `build/rc6/run-dwo6j0rf` |
| 1024 MiB | kvm | 250639 | 1 | `build/rc6/run-od1u6uwr` |
| 256 MiB | tcg | 54024 | 1 | `build/rc6/run-_e2ccdht` |

ISO SHA-256: `5082fd32a901b555bb708c46b84efaa38861ad2f3d155da634df7dc7537867f3`

ELF SHA-256: `0738f2c48f9a2be9a0b7c85448d9ce634b565613db6893e19ee233e35825d79f`

## Limitations

Single BSP; shared kernel address space; no userspace, FPU/SIMD context switching, SMP, guard pages, persistent scheduler or physical hardware validation. Heap is bounded, not a general expanding allocator; stale pointer reuse is not detected. The kernel intentionally halts with IF=0 after validation.
