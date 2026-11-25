# M10.4–M10.10 — Physical memory management and RC2 validation

## Milestones

| Milestone | Capability | New host assertions per mode |
|---|---|---:|
| M10.4 | Live range-indexed bitmap initialization | 622844 |
| M10.5 | Single 4 KiB allocation | 590776 |
| M10.6 | Free and immediate reuse | 1232526 |
| M10.7 | Invalid operations, exhaustion and recovery | 688 |
| M10.8 | Aligned physically contiguous allocation/free | 448032 |
| M10.9 | Deterministic mixed workload and cleanup | 5524863 |
| M10.10 | Normal kernel entry and bounded boot smoke test | Regression suites |

The host suites ran in normal and ASan/UBSan modes. Assertion counts are not counts of independent test cases. The M10.9 runtime regression replayed 4096 workload iterations per boot, including full single-page exhaustion earlier in the test; all four boots passed. RC2 uses the same tested CPU activation and PMM core, but a separate normal entry `kernel/main_pmm.c` selected by `scripts/build-rc2.sh`. RC1 entry and historical experiment builds remain unchanged.

## RC2 runtime evidence

| RAM MiB | Accelerator | Managed pages | Free pages | Bitmap bytes | Evidence directory |
|---:|---|---:|---:|---:|---|
| 256 | kvm | 54080 | 54080 | 8192 | `build/rc2/run-bv7wydgp` |
| 512 | kvm | 119614 | 119614 | 16384 | `build/rc2/run-kul9q32e` |
| 1024 | kvm | 250669 | 250669 | 32768 | `build/rc2/run-q8hk2yma` |
| 256 | tcg | 54067 | 54067 | 8192 | `build/rc2/run-98bnpsio` |

Each RC2 boot allocated one page and an eight-page aligned block, released both, verified immediate single-page reuse and returned all managed pages. QMP verified the physical bitmap, intended halt, CPL0, IF=0, selectors and unchanged CR3/CR4. No stress workload or deliberate CPU exception is triggered in the normal RC2 path. Both ELF and ISO were reproduced byte-for-byte.

## Artifacts

- `dist/ProZestOS-v0.0.7-rc2-x86_64.iso` — 3749888 bytes; SHA-256 `ad43ce93f6fce037695a60d56a2cb9898e9f5e098d1a66548c07f08b6fb63554`
- `build/rc2/kernel.elf` — 63312 bytes; SHA-256 `2e982d24416c6ffd594fcfcd8ec8c4a4feeef382d27951b16a777521a4575224`

## Contract and limitations

Range-indexed LSB-first bitmap: 0 free, 1 allocated/unavailable. Bitmap backing is excluded from managed ranges. Single allocation uses next-fit; free updates the hint to the released index. Contiguous allocation uses physical first-fit with power-of-two alignment, within one managed range. PMM calls require trusted kernel callers, a persistent immutable plan, one BSP and interrupts disabled. No page payload zeroing, ownership tracking, SMP synchronization, bootloader-memory reclamation or new virtual mappings. Normal stack bounds are conservatively protected by excluding its bootloader-reclaimable region. Tested under QEMU/OVMF, not physical hardware. RC2 is a local release candidate, not a stable release or Git tag.
