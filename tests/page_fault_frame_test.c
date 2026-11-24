#include "frame.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    /* Position the six words exactly where hardware places them. */
    _Alignas(16) struct pf_frame storage;
    struct pf_frame *f = &storage;
    uintptr_t rsp = (uintptr_t)(f + 1) + 8;
    uintptr_t aligned = (uintptr_t)f & ~(uintptr_t)15;
    *f = (struct pf_frame){0, 0x1234, 8, 2, rsp, 16};
    unsigned checks = 0;
#define CHECK(x) do { if (!(x)) { puts(#x); return EXIT_FAILURE; } ++checks; } while (0)
#define VALID() valid_pf_frame(14, f, 0x1234, rsp, aligned)
    CHECK(sizeof(*f) == 48);
    CHECK(offsetof(struct pf_frame, rip) == 8);
    CHECK(offsetof(struct pf_frame, cs) == 16);
    CHECK(offsetof(struct pf_frame, flags) == 24);
    CHECK(offsetof(struct pf_frame, rsp) == 32);
    CHECK(offsetof(struct pf_frame, ss) == 40);
    CHECK(VALID());
    CHECK(!valid_pf_frame(13, f, f->rip, rsp, aligned));
    CHECK(!valid_pf_frame(14, NULL, f->rip, rsp, aligned));
    CHECK(!valid_pf_frame(14, f, f->rip + 2, rsp, aligned));
    CHECK(!valid_pf_frame(14, f, f->rip, rsp + 8, aligned));
    CHECK(!valid_pf_frame(14, f, f->rip, rsp, aligned + 8));
    f->cs = 0x28;
    CHECK(!VALID());
    f->cs = 8;
    f->flags |= 0x200;
    CHECK(!VALID());
    f->flags = 2;
    f->ss = 0;
    CHECK(!VALID());
    CHECK(offsetof(struct pf_frame, error) == 0);
    CHECK(cr2_matches(FAULT_TARGET));
    CHECK(!cr2_matches(FAULT_TARGET + 1));
    unsigned bits[] = {0,1,2,3,4,5,6,15,31};
    for (unsigned i = 0; i < sizeof(bits)/sizeof(bits[0]); ++i) {
        CHECK(!pf_bit(0, bits[i]));
        CHECK(pf_bit(UINT64_C(1) << bits[i], bits[i]));
        CHECK(!pf_bit(~(UINT64_C(1) << bits[i]), bits[i]));
    }
    uint64_t root[512] = {0};
    CHECK(!root_excludes_target(NULL));
    CHECK(root_excludes_target(root));
    root[1] = 1;
    CHECK(!root_excludes_target(root));
    root[1] = 0x81;
    CHECK(!root_excludes_target(root));
    printf("%u frame checks passed\n", checks);
}
