#include "profile.h"
#include "cpu_tables_preflight.h"
#include <stdio.h>
#include <stdlib.h>

_Alignas(16) volatile uint64_t ist4_stack[TSS_IST_STACK_SIZE / 8];
_Noreturn void exception_dispatch(uint64_t v, uint64_t e)
{
    (void)v; (void)e;
    abort();
}

int main(void)
{
    unsigned checks = 0;
#define CHECK(x) do { if (!(x)) { puts(#x); return 1; } ++checks; } while (0)
    CHECK(bootstrap_cpu_tables_prepare() && bootstrap_idt_prepare());
    struct bootstrap_cpu_tables *cpu = (void *)bootstrap_cpu_tables_get();
    struct bootstrap_idt_state *irq = (void *)bootstrap_idt_get();
#define PROFILE() cpu_tables_preflight_check(cpu, irq)
    CHECK(PROFILE());
    CHECK(cpu->tss.ist[3] == ist4_top());
    CHECK(irq->table.entries[6].ist == 4);
    CHECK(irq->table.entries[8].ist == 1 && irq->table.entries[2].ist == 2 &&
          irq->table.entries[18].ist == 3);
    irq->table.entries[6].ist = 0;
    CHECK(!PROFILE());
    irq->table.entries[6].ist = 4;
    cpu->tss.ist[3] = 0;
    CHECK(!PROFILE());
    cpu->tss.ist[3] = ist4_top();
    uint64_t old = cpu->tss.ist[0];
    cpu->tss.ist[0] = ist4_top()-16;
    CHECK(!PROFILE());
    cpu->tss.ist[0] = old;
    CHECK(PROFILE());
    CHECK(!valid_bounds(0, 16384));
    CHECK(!valid_bounds(1, 16385));
    CHECK(!valid_bounds(1ULL<<48, (1ULL<<48)+16384));
    CHECK(!valid_bounds(UINTPTR_MAX-15, 16368));
    CHECK(!valid_bounds(0x10000, 0x10010));
    _Alignas(16) struct { uint64_t pad; struct ud_frame f; } image;
    struct ud_frame *f = &image.f;
    uintptr_t top = (uintptr_t)(f+1), base = top-16384;
    uintptr_t normal = top+8192, aligned = top-48, local = aligned-16;
    *f = (struct ud_frame){123,8,2,normal,16};
#define FRAME(v,p) ist_frame_ok(v,p,base,top,normal,123,aligned,local)
    CHECK(FRAME(6,f));
    CHECK(!FRAME(14,f));
    CHECK(!FRAME(6,NULL));
    CHECK(!FRAME(6,(const struct ud_frame *)(base-8)));
    CHECK(!FRAME(6,(const struct ud_frame *)(top+8)));
    CHECK(!FRAME(6,(const struct ud_frame *)(top-8)));
    ++aligned;
    CHECK(!FRAME(6,f));
    --aligned;
    ++f->rsp;
    CHECK(!FRAME(6,f));
    --f->rsp;
    ++f->rip;
    CHECK(!FRAME(6,f));
    --f->rip;
    ++f->cs;
    CHECK(!FRAME(6,f));
    --f->cs;
    ++f->ss;
    CHECK(!FRAME(6,f));
    --f->ss;
    f->flags |= 512;
    CHECK(!FRAME(6,f));
    f->flags = 2;
    normal = f->rsp = base+32;
    CHECK(!FRAME(6,f));
    normal = f->rsp = top+8192;
    local = base;
    CHECK(!FRAME(6,f));
    ist4_stack[0] = IST4_CANARY;
    CHECK(ist4_stack[0] == IST4_CANARY);
    ist4_stack[0] ^= 1;
    CHECK(ist4_stack[0] != IST4_CANARY);
    printf("%u IST checks passed\n", checks);
}
