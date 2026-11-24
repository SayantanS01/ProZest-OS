#include <stdio.h>
#include "fatal_frame.h"
int main(void)
{
    uint64_t raw[6] = {0};
    unsigned count = 0;
#define CHECK(x) do { if (!(x)) { puts(#x); return 1; } ++count; } while (0)
    const uint32_t error_mask = 0x60227d00u;
    for (unsigned v=0; v<32; ++v) {
        unsigned error = (error_mask >> v) & 1;
        CHECK(exception_has_error_code(v) == (int)error);
        CHECK((const void *)fatal_frame_at(v,raw) == (const void *)(raw+error));
    }
    CHECK(fatal_frame_at(32,raw) == NULL);
    CHECK(fatal_frame_at(6,NULL) == NULL);
    CHECK(fatal_frame_at(14,(void *)((char *)raw+1)) == NULL);
    printf("%u unified frame checks passed\n", count);
}
