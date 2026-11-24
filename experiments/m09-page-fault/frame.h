#ifndef PF_FRAME_H
#define PF_FRAME_H
#include <stddef.h>
#include <stdint.h>

/* Starting at the original vector-14 entry RSP, including the hardware error code. */
struct pf_frame { uint64_t error, rip, cs, flags, rsp, ss; };
_Static_assert(sizeof(struct pf_frame) == 48 &&
    offsetof(struct pf_frame, error) == 0 && offsetof(struct pf_frame, rip) == 8 && offsetof(struct pf_frame, cs) == 16 &&
    offsetof(struct pf_frame, flags) == 24 && offsetof(struct pf_frame, rsp) == 32 &&
    offsetof(struct pf_frame, ss) == 40, "64-bit #PF frame");

static inline int valid_pf_frame(unsigned vector, const struct pf_frame *frame,
                                 uintptr_t rip, uintptr_t rsp, uintptr_t call_rsp)
{
    return vector == 14 && frame && frame->rip == rip && frame->cs == 8 &&
           frame->ss == 16 && frame->rsp == rsp && (frame->flags & 0x202) == 2 &&
           (uintptr_t)frame == (rsp & ~(uintptr_t)15) - sizeof(*frame) &&
           call_rsp == ((uintptr_t)frame & ~(uintptr_t)15);
}
static inline int pf_bit(uint64_t raw, unsigned bit)
{
    return (raw >> bit) & 1u;
}

#define FAULT_TARGET (UINT64_C(1) << 39)
static inline int cr2_matches(uint64_t cr2) { return cr2 == FAULT_TARGET; }

/* A present root entry is inconclusive, regardless of lower-level page size. */
static inline int root_excludes_target(const volatile uint64_t *root)
{
    return root && !(root[FAULT_TARGET >> 39] & 1u);
}
#endif
