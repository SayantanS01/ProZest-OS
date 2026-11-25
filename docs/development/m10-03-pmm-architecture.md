# M10.3 — Physical page allocator architecture and metadata plan

Date: 2026-11-25 (Asia/Kolkata)  
Result: **PASS, read-only plan only**. No live bitmap, page allocation, page free, metadata-frame write, page-table modification, or bootloader-memory reclamation occurred.

## Architecture decision

Use a **range-indexed bitmap**. Each sorted managed range stores `[physical_base,physical_end)` and its first dense bitmap index. One bit tracks each managed 4 KiB page: 0 free, 1 allocated. Holes and permanently unavailable memory receive no bits. Address-to-index searches a range and adds the page offset; index-to-address searches the matching index span. Both reject holes, metadata pages, misalignment and out-of-range inputs. The M10.3 functions are pure planning and mapping functions; `pmm.h` declares the future operational API without an implementation.

| Design | Lookup/operation | Metadata | Fragmentation | Fit here |
|---|---|---|---|---|
| Range-indexed bitmap | Range search O(R), bit scan O(P) initially; hints can improve scans | 1 bit per managed page plus O(R) descriptors | Single pages do not suffer external fragmentation; contiguous requests can | Selected: compact across sparse physical ranges. |
| Flat physical-frame bitmap | Direct O(1) index, bit scan for free page | 1 bit per addressable frame, including holes | Similar to bitmap | Wastes bits for Q35 MMIO and high firmware holes. |
| Free list | O(1) push/pop for single pages; contiguous search can be O(P) | Pointer per free page, often stored inside page | Contiguous search can fragment | Needs page writes and mapping assumptions during initialization. |
| Buddy | Usually O(log P) allocation/merge | Order lists/bitmaps and per-block metadata | Internal waste from power-of-two blocks | Good future option for contiguous allocations, more complex first implementation. |

`R` is range count; `P` is managed page count. Future SMP access needs a lock or per-CPU caches; current scope is one CPU with IF clear.

## Metadata layout and sizing

`struct pmm_plan` is **12,400 bytes in existing kernel BSS**: 512 range descriptors (each base, end, first index), range count, proposed counters, search hint, initialization flag, extent and sizing fields. It is already inside the M10.2 kernel-image reservation. The future **allocation bitmap** alone needs new physical backing. For `N` pre-metadata eligible pages:

- Required bytes = `N/8 + (N%8 != 0)`, avoiding `N+7` overflow.
- Bitmap storage pages = `required_bytes/4096 + (required_bytes%4096 != 0)`.
- Bitmap capacity = `storage_pages × 4096 × 8` bits.
- Total metadata storage reported = 12,400 BSS bytes + full page-rounded bitmap storage. This includes alignment and physical-page padding.
- Unused trailing bits = bitmap capacity minus pre-metadata pages. Metadata frames themselves have no managed indices, so total unused bitmap capacity after exclusion also includes the metadata-page count.

The planner sizes against the original eligible count, chooses the **first** eligible range with enough contiguous pages, proposes its leading pages as bitmap backing, then excludes them from final managed ranges. This avoids circular sizing. The proposed frames are not initialized or allocated. Future M10.4 must set unused trailing bits to allocated/invalid before exposing the bitmap, and initialize only final managed bits as free.

## Runtime plan

| QEMU RAM | Accel | Pre-metadata pages | Bitmap required bytes | Bitmap pages | Metadata total bytes | Proposed physical `[base,end)` | Final managed pages | Managed ranges | Active tables PML4/PDPT/PD/PT |
|---:|---|---:|---:|---:|---:|---|---:|---:|---|
| 256 MiB | KVM | 54,047 | 6,756 | 2 | 20,592 | `[0x1000,0x3000)` | 54,045 | 9 | 1/2/3/4 |
| 512 MiB | KVM | 119,583 | 14,948 | 4 | 28,784 | `[0x1000,0x5000)` | 119,579 | 9 | 1/2/3/4 |
| 1024 MiB | KVM | 250,655 | 31,332 | 8 | 45,168 | `[0x1000,0x9000)` | 250,647 | 9 | 1/2/3/4 |
| 4096 MiB | KVM | 1,037,084 | 129,636 | 32 | 143,472 | `[0x1000,0x21000)` | 1,037,052 | 10 | 1/2/6/4 |
| 256 MiB | TCG | 54,047 | 6,756 | 2 | 20,592 | `[0x1000,0x3000)` | 54,045 | 9 | 1/2/3/4 |

Every run independently recalculated M10.2 eligible ranges, bitmap sizing, deterministic first-fit placement, metadata exclusion, dense indices, all range boundaries and logged round-trip samples. The 4096 MiB case included physical address gaps and six PD frames. All configurations planned the bitmap at physical `0x1000` because the initial eligible interval is large enough; the extent length grows with RAM.

### Exact final managed ranges

Each line is `MANAGED base end first_bitmap_index` in hexadecimal. The metadata interval is absent from these ranges.

#### 256 MiB KVM — `run-lyx87z9f`

```text
MANAGED 0x0000000000003000 0x0000000000087000 0x0000000000000000
MANAGED 0x0000000000088000 0x00000000000A0000 0x0000000000000084
MANAGED 0x0000000000100000 0x0000000000800000 0x000000000000009C
MANAGED 0x0000000000808000 0x000000000080B000 0x000000000000079C
MANAGED 0x000000000080C000 0x0000000000814000 0x000000000000079F
MANAGED 0x0000000001780000 0x000000000A2D1000 0x00000000000007A7
MANAGED 0x000000000A326000 0x000000000A361000 0x00000000000092F8
MANAGED 0x000000000A36C000 0x000000000BB6C000 0x0000000000009333
MANAGED 0x000000000BBA4000 0x000000000E38E000 0x000000000000AB33
```

#### 512 MiB KVM — `run-6reue2kj`

```text
MANAGED 0x0000000000005000 0x0000000000087000 0x0000000000000000
MANAGED 0x0000000000088000 0x00000000000A0000 0x0000000000000082
MANAGED 0x0000000000100000 0x0000000000800000 0x000000000000009A
MANAGED 0x0000000000808000 0x000000000080B000 0x000000000000079A
MANAGED 0x000000000080C000 0x0000000000814000 0x000000000000079D
MANAGED 0x0000000001780000 0x000000001A2D1000 0x00000000000007A5
MANAGED 0x000000001A326000 0x000000001A361000 0x00000000000192F6
MANAGED 0x000000001A36C000 0x000000001BB6C000 0x0000000000019331
MANAGED 0x000000001BBA4000 0x000000001E38E000 0x000000000001AB31
```

#### 1024 MiB KVM — `run-50w2qlcs`

```text
MANAGED 0x0000000000009000 0x0000000000087000 0x0000000000000000
MANAGED 0x0000000000088000 0x00000000000A0000 0x000000000000007E
MANAGED 0x0000000000100000 0x0000000000800000 0x0000000000000096
MANAGED 0x0000000000808000 0x000000000080B000 0x0000000000000796
MANAGED 0x000000000080C000 0x0000000000814000 0x0000000000000799
MANAGED 0x0000000001780000 0x000000003A2D1000 0x00000000000007A1
MANAGED 0x000000003A326000 0x000000003A361000 0x00000000000392F2
MANAGED 0x000000003A36C000 0x000000003BB6C000 0x000000000003932D
MANAGED 0x000000003BBA4000 0x000000003E38E000 0x000000000003AB2D
```

#### 4096 MiB KVM — `run-7dnbqikb`

```text
MANAGED 0x0000000000021000 0x0000000000087000 0x0000000000000000
MANAGED 0x0000000000088000 0x00000000000A0000 0x0000000000000066
MANAGED 0x0000000000100000 0x0000000000800000 0x000000000000007E
MANAGED 0x0000000000808000 0x000000000080B000 0x000000000000077E
MANAGED 0x000000000080C000 0x0000000000814000 0x0000000000000781
MANAGED 0x0000000001780000 0x000000007A2D1000 0x0000000000000789
MANAGED 0x000000007A326000 0x000000007A35E000 0x00000000000792DA
MANAGED 0x000000007A36C000 0x000000007BB6C000 0x0000000000079312
MANAGED 0x000000007BBA4000 0x000000007E38E000 0x000000000007AB12
MANAGED 0x0000000100000000 0x0000000180000000 0x000000000007D2FC
```

#### 256 MiB TCG — `run-yhdv9krf`

```text
MANAGED 0x0000000000003000 0x0000000000087000 0x0000000000000000
MANAGED 0x0000000000088000 0x00000000000A0000 0x0000000000000084
MANAGED 0x0000000000100000 0x0000000000800000 0x000000000000009C
MANAGED 0x0000000000808000 0x000000000080B000 0x000000000000079C
MANAGED 0x000000000080C000 0x0000000000814000 0x000000000000079F
MANAGED 0x0000000001780000 0x000000000A2D1000 0x00000000000007A7
MANAGED 0x000000000A326000 0x000000000A361000 0x00000000000092F8
MANAGED 0x000000000A36C000 0x000000000BB6C000 0x0000000000009333
MANAGED 0x000000000BBA4000 0x000000000E38E000 0x000000000000AB33
```

## Read-only mapping evidence

The kernel reused the bounded M10.2 table resolver, which dereferences a table frame only after proving it lies in Limine bootloader-reclaimable memory. For each proposed bitmap page, `pmm_metadata_mapping_check` walked the active CR3 PML4→PDPT→PD→PT path, accepted valid large-page leaves, checked present and effective writable/supervisor permissions, and compared the translated physical address with the proposed page. It performed **no write probe**. QEMU `info mem` independently showed the HHDM virtual extent inside a writable mapping; monitor `xp` inspected the physical CR3 root and its reported child frames. This establishes access for the tested boot configurations, not every possible future bootloader or hardware mapping.

The first VM run exposed a paging-permission mistake: the PML4 entry had its user bit set even though a lower-level entry made the effective mapping supervisor-only. The checker now accumulates user permission across the complete path. Initial freestanding compilation emitted implicit `memset`/`memcpy` from a large struct initializer/copy; explicit byte loops removed those dependencies.

## Planned operational API and invariants

`kernel/include/pmm.h` declares `pmm_init`, `pmm_alloc_page`, `pmm_free_page`, `pmm_alloc_contiguous`, `pmm_free_contiguous`, `pmm_get_stats`, and membership query, with `enum pmm_status` and output parameters. None is linked into the M10.3 ELF. Before initialization, operations return `PMM_NOT_INITIALIZED`; repeat initialization returns `PMM_ALREADY_INITIALIZED`; exhaustion returns `PMM_OUT_OF_MEMORY`. Invalid addresses, counts or alignment fail without state change. Double free returns `PMM_DOUBLE_FREE`; reserved or metadata pages return `PMM_RESERVED`. Contiguous allocation cannot cross a physical-range boundary and must honor a power-of-two page alignment. Failure leaves bitmap and counters unchanged. M10.4 should choose zeroing policy explicitly; if zeroing is requested, it must use a verified writable mapping. Allocating a physical frame does **not** create a new virtual mapping.

Required invariants: only final managed pages may be returned; protected and metadata pages never may; a live page is never allocated twice; free + allocated = managed; bitmap bits and counters agree; returned addresses are page-aligned; failures are transactional; contiguous runs stay in one physical range; bitmap accesses stay within reserved storage; initialization follows reservation validation. Single-CPU IF=0 is the supported synchronization configuration; SMP needs locking or per-CPU design later.

## Tests and audit

- Previous M09/M10.1/M10.2 checks: **7,656/7,656 normal**, **7,656/7,656 ASan/UBSan**.
- New M10.3 checks: **7,647/7,647 normal**, **7,647/7,647 ASan/UBSan**; includes 200 deterministic randomized fragmented-range trials, bitmap edge counts, first-fit placement, full-range consumption, dense mapping round trips, invalid inputs, and synthetic 4 KiB/2 MiB page-table mapping cases.
- Total: **15,303/15,303** in each mode. LeakSanitizer remains disabled under the sandbox tracer; ASan and UBSan ran.
- QEMU: **4/4 KVM** (256, 512, 1024, 4096 MiB), **1/1 TCG** (256 MiB). Each uses Q35, one vCPU, fresh OVMF variables and optical boot. All reached `PMM METADATA PLAN: PASS` and halted intentionally with CPL=0, IF=0, HLT=1, RIP `0xFFFFFFFF80003BA2`; no unexpected reset or exception. Logs, register/mapping dumps, physical-root dumps, screenshots and result JSON are in the run directories.
- ELF audit: ELF64 entry `0xFFFFFFFF80001000`, expected Limine request/load sections and descriptor transition, no undefined symbols, no STI, no live `pmm_init`/alloc/free symbols. The planning code never dereferences the proposed metadata HHDM pointer and does not modify CR3 or page tables.
- A clean scratch copy excluding `.git`, `build`, and `dist` reproduced identical ELF and ISO hashes.
- Actual screenshots: `build/m10-pmm-plan/run-lyx87z9f/screen.png` (256 MiB) and `build/m10-pmm-plan/run-7dnbqikb/screen.png` (4096 MiB).

## Files and artifacts

Created `kernel/pmm_plan.c`, `kernel/include/pmm_plan.h`, `kernel/pmm_mapping.c`, `kernel/include/pmm_mapping.h`, `kernel/include/pmm.h`, `tests/pmm_plan_test.c`, `experiments/m10-pmm-plan/main.c`, `experiments/m10-pmm-plan/linker.ld`, `experiments/m10-pmm-plan/limine.conf`, `scripts/build-m10-pmm-plan.sh`, `scripts/check-m10-pmm-elf.py`, `scripts/test-m10-pmm-host.sh`, and `scripts/test-m10-pmm-plan.py`. Prior uncommitted work remains present.

- `build/m10-pmm-plan/kernel.elf`: 58,280 bytes; SHA-256 `b46239cb874c817e2a13cc1c7b72eea80b102e4414eb6aefbf5bd6bce7258fc4`.
- `dist/ProZestOS-M10-PMM-Plan-UEFI-x86_64.iso`: 3,745,792 bytes; SHA-256 `66f4394529900af831327080aa3db062e7aa5522b31ec0abd8c5279a490a886c`.
- Preserved `dist/ProZestOS-M10-Memory-Reservations-UEFI-x86_64.iso`: SHA-256 `4b99a3f60e2d4bcc2eaa747dab1a0910508a2bccda8ddd863c2d9b74ceace470`.
- Preserved `dist/ProZestOS-M10-Memory-Inventory-UEFI-x86_64.iso`: SHA-256 `c1118c9dae3582d22951a2ad083fdc247e02fa50b452fa2a19fc672e071b68f1`.
- Preserved `dist/ProZestOS-v0.0.7-rc1-x86_64.iso`: SHA-256 `0921e7aa98c8ffbd49cb0332eddc067257a6e00043614ada77be4df69e1dd0d3`.
- Preserved `dist/ProZestOS-v0.0.6-x86_64.iso`: SHA-256 `e96a8d689184953c7e61f906c95682ef6bc7c83875f54b88f517849bdc56ab1d`.

## M10.4 implementation sequence

1. Re-run the M10.2 reservation and M10.3 mapping checks at boot; fail closed if the layout differs.
2. Reserve the proposed metadata interval in the protected set and revalidate final managed ranges.
3. Map the bitmap backing through the verified HHDM pages; initialize only that storage. Set unused trailing bits invalid and managed bits free, with counters derived from the final ranges.
4. Implement single-page allocate/free with explicit status and output parameters. Check and update bitmap plus counters transactionally.
5. Add contiguous and alignment-constrained operations that stay within one managed range; test rollback on failed searches.
6. Add host reference-model tests, QEMU allocation stress, double-free and out-of-memory tests, then revisit locking before SMP.

Do not reclaim bootloader or ACPI memory as part of this sequence. The plan is ready for M10.4 **within the tested single-BSP Limine configuration**; the live allocator still requires its own implementation and runtime safety proof.

## Interview questions

1. **Why use dense bitmap indices?** They spend bits only on pages that can eventually be managed.
2. **How many bytes for N bits?** `N/8 + (N%8 != 0)`, which avoids overflow from adding seven.
3. **Why size before reserving bitmap pages?** It breaks the circular dependency between bitmap size and final managed-page count.
4. **What happens to unused trailing bits?** Future initialization must mark them unavailable so scans cannot return them.
5. **Why keep range descriptors in BSS?** Their bounded storage is already inside the reserved kernel image.
6. **Why is HHDM verification needed?** An eligible physical page is not automatically guaranteed to be writable through an arbitrary virtual address.
7. **Can a contiguous request cross adjacent bitmap indices?** Only if those indices also represent physically adjacent pages inside one range.
8. **What does allocation return?** A physical frame address through an output parameter and a separate status code.
9. **Does a physical allocation create a virtual mapping?** No; page-table work is a separate operation.
10. **What changes for SMP?** Bitmap and counters need synchronized updates or per-CPU caches to prevent duplicate allocation.
