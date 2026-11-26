# M17 — Storage and filesystem

Result: PASS (four QEMU configurations and reproducible build).

Dedicated QEMU ISA IDE channel, ATA IDENTIFY with exact test-disk serial and capacity, bounded LBA28 PIO, explicit cache flush, custom PZFS metadata and data CRC32, flat eight-file directory, create/read/update/delete, remount, independent raw-image verification and persistence across two separate QEMU boots.

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54013 | 1 | `build/rc9/run-n1hwtygn` |
| 512 MiB | kvm | 119547 | 1 | `build/rc9/run-53_9q_0c` |
| 1024 MiB | kvm | 250615 | 1 | `build/rc9/run-00yarium` |
| 256 MiB | tcg | 54026 | 1 | `build/rc9/run-fq1kkikx` |

ISO SHA-256: `e80f66e6845e07491f34e00fdbc83c28e4d37e6cd14044805e9972821679db04`

ELF SHA-256: `52e0bade5d31e0b88a6f7c0e1af5ff3f42d22fe5bb9af5c23f0885b993197fab`

## Limitations

Only a dedicated synthetic 1 MiB disk image, eight files of at most 512 bytes each, no directories, partitions, standard filesystem, journaling, crash consistency, arbitrary hardware support, userspace file syscalls, or general storage discovery. Kernel writes only sectors 0 through 8 after verifying the test disk and seed. The kernel halts after validation. Never attach a physical disk.
