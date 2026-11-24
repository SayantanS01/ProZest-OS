#include <stdio.h>
#include "descriptor_activation.h"
#include "cpu_tables_preflight.h"

void exception_dispatch(uint64_t vector, uint64_t error)
{
    (void)vector;
    (void)error;
    __builtin_trap();
}

int main(void)
{
    if (!bootstrap_cpu_tables_prepare() || !bootstrap_idt_prepare())
        return 1;
    struct bootstrap_cpu_tables *cpu = (void *)bootstrap_cpu_tables_get();
    const struct bootstrap_idt_state *irq = bootstrap_idt_get();
    struct handoff_state before = {0};
    struct handoff_state after = {.value = {
        [CS]=8, [SS]=16, [DS]=16, [ES]=16, [FS]=16, [GS]=16, [TR]=24,
        [GDTR_BASE]=(uintptr_t)&cpu->gdt, [IDTR_BASE]=(uintptr_t)&irq->table,
        [GDTR_LIMIT]=39, [IDTR_LIMIT]=4095
    }};
    unsigned checks = 0;
#define CHECK(expr) do { if (!(expr)) { puts(#expr); return 1; } ++checks; } while (0)
    struct handoff_state incoming = {.value = {
        [CS]=0x28, [SS]=0x30, [DS]=0x30, [ES]=0x30, [FS]=0x30, [GS]=0x30,
        [TR]=0x38, [GDTR_BASE]=0xffff800000001000ULL, [GDTR_LIMIT]=0x47
    }};
    CHECK(supported_handoff(&incoming));
    unsigned fields[] = {CS, SS, FS_BASE, GS_BASE, INTERRUPT_FLAG, TR, IDTR_LIMIT};
    for (unsigned i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        incoming.value[fields[i]] ^= 1;
        CHECK(!supported_handoff(&incoming));
        incoming.value[fields[i]] ^= 1;
    }
    incoming.value[CR4] = 1u << 23;
    CHECK(!supported_handoff(&incoming));
    CHECK(active_state(&before, &after, cpu, irq));
    for (unsigned i = 0; i < FIELD_COUNT; ++i) {
        after.value[i] ^= 1;
        CHECK(!active_state(&before, &after, cpu, irq));
        after.value[i] ^= 1;
    }
    CHECK(!cpu_tables_active_check(cpu, irq));
    cpu->gdt.tss_descriptor.access = 0x8b;
    CHECK(!cpu_tables_preflight_check(cpu, irq));
    CHECK(cpu_tables_active_check(cpu, irq));
    cpu->gdt.kernel_code_descriptor |= UINT64_C(1) << 40;
    cpu->gdt.kernel_data_descriptor |= UINT64_C(1) << 40;
    CHECK(cpu_tables_active_check(cpu, irq));
    cpu->gdt.kernel_data_descriptor ^= UINT64_C(2) << 40;
    CHECK(!cpu_tables_active_check(cpu, irq));
    printf("%u activation checks passed\n", checks);
}
