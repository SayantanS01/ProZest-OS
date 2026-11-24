#include "frame.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    /* Position the five words exactly where hardware places them. */
    struct { _Alignas(16) uint64_t pad; struct ud_frame frame; } storage;
    struct ud_frame *f = &storage.frame;
    uintptr_t rsp = (uintptr_t)(f + 1) + 8;
    uintptr_t aligned = (uintptr_t)f & ~(uintptr_t)15;
    *f = (struct ud_frame){0x1234, 8, 2, rsp, 16};
    unsigned checks = 0;
#define CHECK(x) do { if (!(x)) { puts(#x); return EXIT_FAILURE; } ++checks; } while (0)
#define VALID() valid_ud_frame(6, f, 0x1234, rsp, aligned)
    CHECK(sizeof(*f) == 40);
    CHECK(offsetof(struct ud_frame, rip) == 0);
    CHECK(offsetof(struct ud_frame, cs) == 8);
    CHECK(offsetof(struct ud_frame, flags) == 16);
    CHECK(offsetof(struct ud_frame, rsp) == 24);
    CHECK(offsetof(struct ud_frame, ss) == 32);
    CHECK(VALID());
    CHECK(!valid_ud_frame(13, f, f->rip, rsp, aligned));
    CHECK(!valid_ud_frame(6, NULL, f->rip, rsp, aligned));
    CHECK(!valid_ud_frame(6, f, f->rip + 2, rsp, aligned));
    CHECK(!valid_ud_frame(6, f, f->rip, rsp + 8, aligned));
    CHECK(!valid_ud_frame(6, f, f->rip, rsp, aligned + 8));
    f->cs = 0x28;
    CHECK(!VALID());
    f->cs = 8;
    f->flags |= 0x200;
    CHECK(!VALID());
    f->flags = 2;
    f->ss = 0;
    CHECK(!VALID());
    printf("%u frame checks passed\n", checks);
}
