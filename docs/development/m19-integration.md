# M19 — Graphical desktop foundations

Result: PASS (four QEMU configurations and reproducible build).

Framebuffer desktop with clipped drawing, Home, System and read-only PZFS Files panels, keyboard navigation, terminal launch, four QMP screenshots per VM with independent RGB pixel checks, inherited 20-command shell transcript and cross-boot persistence. Run bash scripts/run-rc11.sh for a manual session.

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54021 | 1 | `build/rc11/run-f23ewkoa` |
| 512 MiB | kvm | 119542 | 1 | `build/rc11/run-5g__cy95` |
| 1024 MiB | kvm | 250610 | 1 | `build/rc11/run-d2peejde` |
| 256 MiB | tcg | 54021 | 1 | `build/rc11/run-obgt9wjp` |

ISO SHA-256: `18da2505df348dfadfc835c08b45c3e416adfca744fd777b017e4890f992595c`

ELF SHA-256: `abe6bfd005c788a74d6a3bf80f232aa69c0023f9417b8cd77d2b4a3de41a9c06`

## Limitations

Kernel-mode prototype, single CPU, polled PS/2 input, no mouse, window manager, compositor, userspace GUI, or file editing in the desktop. Terminal selection is one-way until reboot. Dedicated synthetic disk, separate from RC10 manual disk. Existing shell and PZFS capacity limits remain. Press and release A during the inherited keyboard self-test.
