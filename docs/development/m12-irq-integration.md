# M12 — Interrupt controllers and timers

M12.1–M12.10 passed the single-BSP Q35 bring-up batch. RC3 source is unchanged.

| Stage | Implementation |
|---|---|
| M12.1 | CPUID APIC/TSC and IF diagnostics |
| M12.2 | IRQ stubs, all 15 GPRs, stack alignment, CLD and IRETQ; preserve exception gates |
| M12.3 | Vector dispatcher, 1024 software IRQ round trips per boot |
| M12.4 | Remapped and masked dual 8259 PIC |
| M12.5 | PIT channel 0 mode 2 via PIC virtual wire |
| M12.6 | Local APIC MMIO or inherited x2APIC MSR mode |
| M12.7 | ACPI MADT single IOAPIC and IRQ0 override; PIT delivery |
| M12.8 | PIT nominal elapsed milliseconds, calibrated periodic LAPIC timer |
| M12.9 | ISR/EOI checks, IF/DF checks, source masking, temporary page-table release |
| M12.10 | Normal RC4 entry, QMP IDT and PMM bitmap verification |

| RAM MiB | Accelerator | PIC ticks | IOAPIC ticks | LAPIC ticks | Nominal PIT ms | Evidence |
|---:|---|---:|---:|---:|---:|---|
| 256 | kvm | 16 | 80 | 64 | 960 | build/rc4/run-ssc_mafs |
| 512 | kvm | 16 | 80 | 64 | 960 | build/rc4/run-gi0l8ye3 |
| 1024 | kvm | 16 | 80 | 64 | 960 | build/rc4/run-k2iq_col |
| 256 | tcg | 16 | 80 | 64 | 960 | build/rc4/run-85pht7xe |

Host parser/time arithmetic assertions: 390191 per mode, normal and ASan/UBSan.
Previous host regression suites passed. RC3 baseline and RC4 candidate were reproduced byte-for-byte.

The new IDT preserves all 32 exception gates byte-for-byte. IRQs run only during
bounded tests; all configured sources are masked and IF=0 at the final halt.
Temporary MMIO/ACPI mappings and their three table frames are released. The
only retained PMM allocation is the existing kernel-owned PML4. PIT-derived
elapsed time is nominal delivered-tick accounting, not a wall clock or a
persistent uptime service. The LAPIC timer is calibrated against PIT delivery.
The tested platform is single-vCPU Q35/OVMF with one IOAPIC, legacy PIC/PIT,
firmware ACPI tables in reserved/reclaimable memory, and UC PAT index 3.
No SMP, arbitrary hardware routing, keyboard driver, IRQ nesting, interrupt
remapping, TSC-deadline mode, scheduler, physical hardware validation, or new
intentional fault test after IRQ installation. RC3 fault images are unchanged.

ISO: `dist/ProZestOS-v0.0.7-rc4-x86_64.iso`
SHA-256: `43effa3d7083189a3b9f1812267376cd0968c6f5c878020269745c6f531c2837`
ELF SHA-256: `33705f271ddbe728b7c4c2240206501ab0dec5a56138ed0579a3fde868656b59`
