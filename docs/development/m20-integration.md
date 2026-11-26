# M20 — PS/2 mouse and graphical cursor

Result: PASS (four QEMU configurations and reproducible build).

Polled PS/2 standard mouse, three-byte packet decoding, signed motion and edge clamping, clipped software cursor with saved background, click-on-release sidebar navigation, keyboard fallback, five QMP screenshots per VM with RGB and old-cursor restoration checks, inherited 20-command shell and cross-boot persistence. Run bash scripts/run-rc12.sh for a manual session.

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54006 | 1 | `build/rc12/run-q2vpe37k` |
| 512 MiB | kvm | 119553 | 1 | `build/rc12/run-hi4ol1m_` |
| 1024 MiB | kvm | 250608 | 1 | `build/rc12/run-17bsidjs` |
| 256 MiB | tcg | 54006 | 1 | `build/rc12/run-67_lac06` |

ISO SHA-256: `9cd3dd289712167201614fb232dfc2c1588c450c5fd671c2921d55523ae59b6a`

ELF SHA-256: `a32c6957b22388500c718a19c675ec61de06d52a3886083231de618364bb3dbd`

## Limitations

Kernel-mode prototype, single CPU, polled PS/2 relative mouse, no USB, scroll wheel, drag windows, compositor or userspace GUI. AUX controller port disabled on terminal entry; terminal selection remains one-way. Dedicated synthetic disk separate from RC11. Press and release A during inherited keyboard self-test. No physical disk attached.
