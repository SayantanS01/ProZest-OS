#ifndef UD_FRAME_H
#define UD_FRAME_H
#include <stddef.h>
#include <stdint.h>

/* Starting at the original vector-6 RSP, with no error-code slot. */
struct ud_frame { uint64_t rip, cs, flags, rsp, ss; };
_Static_assert(sizeof(struct ud_frame) == 40 &&
    offsetof(struct ud_frame, rip) == 0 && offsetof(struct ud_frame, cs) == 8 &&
    offsetof(struct ud_frame, flags) == 16 && offsetof(struct ud_frame, rsp) == 24 &&
    offsetof(struct ud_frame, ss) == 32, "64-bit #UD frame");

static inline int valid_ud_frame(unsigned vector, const struct ud_frame *frame,
                                 uintptr_t rip, uintptr_t rsp, uintptr_t call_rsp)
{
    return vector == 6 && frame && frame->rip == rip && frame->cs == 8 &&
           frame->ss == 16 && frame->rsp == rsp && (frame->flags & 0x202) == 2 &&
           (uintptr_t)frame == (rsp & ~(uintptr_t)15) - sizeof(*frame) &&
           call_rsp == ((uintptr_t)frame & ~(uintptr_t)15);
}
#endif
