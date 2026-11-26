# M18 — Interactive command-line shell

Result: PASS (four QEMU configurations and reproducible build).

Framebuffer command prompt, polled PS/2 set-1 input with Shift and Backspace, bounded line editor, command dispatch, memory and version information, screen clearing, PZFS listing/read/create/write/delete, independent raw-disk verification, 20 keyboard-driven commands per VM and cross-boot user-file persistence. Run bash scripts/run-rc10.sh for a manual session.

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54008 | 1 | `build/rc10/run-hv7zs548` |
| 512 MiB | kvm | 119555 | 1 | `build/rc10/run-8i64d8ck` |
| 1024 MiB | kvm | 250610 | 1 | `build/rc10/run-2w2_yhtq` |
| 256 MiB | tcg | 54008 | 1 | `build/rc10/run-e35f4mj3` |

ISO SHA-256: `7e143886f1019f8cf085e1a0bd2ec5ce0359880ebe22faa0e597a7f27d8c5a4d`

ELF SHA-256: `78247b3304585435672fde0ad8c354bf5cbbb82d2f6ed60f7d4387f502c288de`

## Limitations

Kernel-mode built-in shell, single CPU, polling with interrupts disabled, US ASCII keyboard subset, no process launch, pipes, quoting, history, directories, userspace filesystem calls or crash consistency. Five user-file slots (two reserved fixture files and one scratch slot), 512-byte PZFS files, 96-character input line. Dedicated synthetic 1 MiB disk only; never attach physical disks. Exit halts the kernel. Press and release A during the inherited keyboard self-test.
