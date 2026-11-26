# M20 — Custom ProZest flame cursor

Result: PASS (four QEMU configurations and reproducible build).

Original user-supplied flame arrow, cropped to alpha bounds and resampled to 32x32 RGBA. Click hotspot (6,4); software alpha blending, all-edge clipping and exact background restoration. Five QMP screenshots per VM, including full 1024-pixel sprite composition and restoration, mouse navigation and keyboard/shell regressions.

| RAM | Accelerator | Managed pages | Final allocated pages | Evidence |
|---|---|---:|---:|---|
| 256 MiB | kvm | 54002 | 1 | `build/rc12-cursor/run-nje77oxn` |
| 512 MiB | kvm | 119536 | 1 | `build/rc12-cursor/run-g8bsp8ey` |
| 1024 MiB | kvm | 250617 | 1 | `build/rc12-cursor/run-oe_4o8i1` |
| 256 MiB | tcg | 54002 | 1 | `build/rc12-cursor/run-7zurvr1k` |

ISO SHA-256: `670d7543e9596b145c2223d58bb8b95483eca3bf3016de8f1b3f6b0d39e7b853`

ELF SHA-256: `add6e8631efa4df479919687239ebd712cc9f696cc8775c55c84eb5b02601cb1`

## Limitations

Kernel-mode PS/2 prototype; static cursor, no USB or scroll wheel. Separate RC12-cursor ISO and manual synthetic disk; original RC12 unchanged. No physical disk or automatic Git push.
