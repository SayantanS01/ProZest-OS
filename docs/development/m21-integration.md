# M21 — Window manager and animated flame

Result: PASS (four QEMU configurations and reproducible build).

Two overlapping kernel windows, click focus, title drag, close, screen bounds; animated 12-frame flame at approximately 20 fps using polled PIT channel 2. Stable arrow and hotspot, alpha blending and background restoration. QMP idle animation, drag, focus, close and shell persistence checks.

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 53978 | 1 | `build/rc13/run-g3xd9ljs` |
| 512 MiB | kvm | 119512 | 1 | `build/rc13/run-vhhw1tlv` |
| 1024 MiB | kvm | 250580 | 1 | `build/rc13/run-xn_23nh1` |
| 256 MiB | tcg | 53978 | 1 | `build/rc13/run-j87vvxtn` |

ISO SHA-256: `fe9aabfd115b14126d4094efacd7cab28518c36f3233d0396579eef404da6fd0`

ELF SHA-256: `90580610344409d3604eab5b74aad7ce63e83ba730afbdc36fd0af3a4b2b6c53`

## Limitations

Kernel-mode fixed-size windows, read-only files, terminal one-way; no compositor, resize, USB, or independent userspace GUI apps. Dedicated synthetic disk. No physical disk or automatic Git push.
