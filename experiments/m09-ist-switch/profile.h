#ifndef IST_TEST_PROFILE_H
#define IST_TEST_PROFILE_H
#include "tss.h"
#include "../m09-invalid-opcode/frame.h"

extern volatile uint64_t ist4_stack[TSS_IST_STACK_SIZE / 8];
#define IST4_CANARY UINT64_C(0x6f48de137ca9b052)
#define IST4_PAINT UINT64_C(0xa5a5a5a5a5a5a5a5)
static inline uintptr_t ist4_top(void)
{
    return (uintptr_t)(ist4_stack + TSS_IST_STACK_SIZE / 8);
}
static inline int valid_bounds(uintptr_t base, uintptr_t top)
{
    return base && top > base && top-base == TSS_IST_STACK_SIZE &&
           !(base & 15) && (top <= (UINT64_C(1)<<47) ||
                             base >= UINT64_C(0xffff800000000000)) &&
           top != (UINT64_C(1)<<47);
}
static inline int ist_frame_ok(unsigned vector, const struct ud_frame *f,
                              uintptr_t base, uintptr_t top, uintptr_t normal,
                              uintptr_t site, uintptr_t aligned, uintptr_t local)
{
    return valid_bounds(base, top) && (uintptr_t)f == top-sizeof(*f) &&
           (normal < base || normal >= top) && normal != 0 &&
           vector == 6 && f->rip == site && f->rsp == normal && f->cs == 8 &&
           f->ss == 16 && (f->flags & 0x202) == 2 &&
           aligned == ((uintptr_t)f & ~(uintptr_t)15) &&
           local >= base+8 && local < aligned;
}
#endif
