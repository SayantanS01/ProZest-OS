#ifndef PROZEST_FATAL_FRAME_H
#define PROZEST_FATAL_FRAME_H
#include <stddef.h>
#include <stdint.h>
#include "exception.h"

struct fatal_frame { uint64_t rip, cs, flags, rsp, ss; };
_Static_assert(sizeof(struct fatal_frame) == 40 &&
    offsetof(struct fatal_frame,rip) == 0 && offsetof(struct fatal_frame,cs) == 8 &&
    offsetof(struct fatal_frame,flags) == 16 && offsetof(struct fatal_frame,rsp) == 24 &&
    offsetof(struct fatal_frame,ss) == 32, "long-mode interrupt frame");

static inline const struct fatal_frame *fatal_frame_at(uint64_t vector, const uint64_t *raw)
{
    if (!raw || vector >= 32 || ((uintptr_t)raw & 7))
        return NULL;
    return (const struct fatal_frame *)(raw + exception_has_error_code(vector));
}
#endif
