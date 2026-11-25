# M11 — Virtual memory management and RC3 validation

M11.1–M11.10 passed the guarded implementation batch. RC3 is a single-CPU,
four-level x86-64 UEFI kernel. The old RC2 entry and PMM are unchanged.

| Stage | Implementation |
|---|---|
| M11.1 | CR0, CR3, CR4, EFER and physical-width diagnostics |
| M11.2 | Canonical addresses and PTE flags |
| M11.3 | Read-only 4 KiB, 2 MiB and 1 GiB translation |
| M11.4 | PMM-backed zeroed page-table frames |
| M11.5 | Supervisor-only private 4 KiB mappings |
| M11.6 | Unmap, INVLPG, empty-table pruning |
| M11.7 | RO/NX, RW/NX, RO/X; hardware execution and permission faults |
| M11.8 | Clone PML4, share inherited subtrees, switch CR3, release test root |
| M11.9 | 12000 host and 512 runtime randomized operations per workload; three genuine page-fault scenarios |
| M11.10 | Normal RC3 entry, active kernel-owned PML4, reproducible build |

Host tests passed in normal and ASan/UBSan modes. QEMU independently checked
CR3, physical PML4 and bitmap, and expected fatal vector/error code/CR2 for
three separate fault images. Mode 0 is normal; 1 is unmapped read, 2 is RO
write, 3 is NX instruction fetch. Normal boot retains exactly one PMM page
for the active root; intentional fatal boots retain their test mappings.

| Mode | RAM MiB | Accelerator | Managed pages | Allocated pages |
|---:|---:|---|---:|---:|
| 0 | 256 | kvm | 54074 | 1 |
| 0 | 512 | kvm | 119595 | 1 |
| 0 | 1024 | kvm | 250663 | 1 |
| 0 | 256 | tcg | 54061 | 1 |
| 1 | 256 | kvm | 54061 | 1 |
| 1 | 256 | tcg | 54061 | 1 |
| 2 | 256 | kvm | 54061 | 5 |
| 2 | 256 | tcg | 54061 | 5 |
| 3 | 256 | kvm | 54061 | 5 |
| 3 | 256 | tcg | 54061 | 5 |

ISO SHA-256: `cc1d9dd3a6c51a2b21aa519e6c53106ebf10f7616124b2c180a3c918b3fb7d84`

ELF SHA-256: `eda94e0925806f8e7bb22b90c319b72efd1abded5b54572c75bd213614682f2a`

Evidence: `build/rc3/runtime-summary.json`, `build/rc3/fault-*/runtime-summary.json`,
and corresponding run directories. Fault ISOs are not release artifacts.

Limitations: no SMP, user mappings, PCID, LA57, demand paging, copy-on-write,
full replacement of Limine subtrees, arbitrary physical alias mapping, or
recoverable page faults. The HHDM alias is outside the private-slot W^X policy.
Trusted backend and immutable inherited mappings are required. Real hardware
is not validated. No bootloader-reclaimable memory is reclaimed.
