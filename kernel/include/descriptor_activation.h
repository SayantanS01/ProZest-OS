#ifndef DESCRIPTOR_ACTIVATION_H
#define DESCRIPTOR_ACTIVATION_H
#include "descriptor_transition.h"
enum state_field {
    GDTR_BASE, GDTR_LIMIT, IDTR_BASE, IDTR_LIMIT, CS, SS, DS, ES, FS, GS, TR,
    FS_BASE, GS_BASE, KERNEL_GS_BASE, CR3, CR4, INTERRUPT_FLAG, FIELD_COUNT
};

struct handoff_state {
    uint64_t value[FIELD_COUNT];
};

static inline int supported_handoff(const struct handoff_state *state)
{
    const uint64_t *v = state->value;
    return v[GDTR_BASE] != 0 && v[GDTR_LIMIT] == 0x47 &&
           v[IDTR_BASE] == 0 && v[IDTR_LIMIT] == 0 &&
           v[CS] == TRANSITION_INHERITED_CS && v[SS] == TRANSITION_INHERITED_DATA &&
           v[DS] == v[SS] && v[ES] == v[SS] && v[FS] == v[SS] && v[GS] == v[SS] &&
           v[TR] == 0x38 && v[FS_BASE] == 0 && v[GS_BASE] == 0 &&
           v[INTERRUPT_FLAG] == 0 && !(v[CR4] & ((1u << 12) | (1u << 23)));
}

static inline int active_state(const struct handoff_state *before, const struct handoff_state *after,
                               const struct bootstrap_cpu_tables *cpu, const struct bootstrap_idt_state *irq)
{
    const uint64_t *v = after->value;
    return v[CS] == 8 && v[SS] == 16 && v[DS] == 16 && v[ES] == 16 &&
           v[FS] == 16 && v[GS] == 16 && v[TR] == 24 && !v[INTERRUPT_FLAG] &&
           !v[FS_BASE] && !v[GS_BASE] && v[KERNEL_GS_BASE] == before->value[KERNEL_GS_BASE] &&
           v[GDTR_BASE] == (uintptr_t)&cpu->gdt && v[GDTR_LIMIT] == 39 &&
           v[IDTR_BASE] == (uintptr_t)&irq->table && v[IDTR_LIMIT] == 4095 &&
           v[CR3] == before->value[CR3] && v[CR4] == before->value[CR4];
}
void controlled_install(const struct gdt_pointer *, const struct idt_pointer *,
                        const struct gdt_pointer *, const struct idt_pointer *);
#endif
