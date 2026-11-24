#include "descriptor_transition.h"
#include "cpu_tables_preflight.h"
#include "exception_gate_identity.h"

/* The bridge never loads TR. The final TSS becomes busy only in the final
 * GDT. The incoming TR cache may outlive its selector's valid GDT slot. */
static const uint64_t bridge_slots[7] = {
    [GDT_KERNEL_CODE_SELECTOR / 8] = GDT_KERNEL_CODE_DESCRIPTOR,
    [GDT_KERNEL_DATA_SELECTOR / 8] = GDT_KERNEL_DATA_DESCRIPTOR,
    [TRANSITION_INHERITED_CS / 8] = GDT_KERNEL_CODE_DESCRIPTOR,
    [TRANSITION_INHERITED_DATA / 8] = GDT_KERNEL_DATA_DESCRIPTOR,
};

static struct descriptor_transition transition_storage;
static int early_idt_published;
static int published;

/* Values used here are kernel symbol addresses or addresses supplied by
 * the caller. The supported handoff uses four-level paging. */
static int canonical48(uint64_t address)
{
    return address <= UINT64_C(0x00007fffffffffff) ||
           address >= UINT64_C(0xffff800000000000);
}

static void prepare_idt(struct bootstrap_idt_state *idt, uint16_t selector)
{
    idt_table_init(&idt->table);
    for (unsigned int i = 0; i < TRANSITION_EXCEPTION_COUNT; ++i) {
        idt_encode_gate(&idt->table.entries[i],
                        (uintptr_t)transition_fatal_handlers[i], selector,
                        0, IDT_INTERRUPT_GATE);
    }
    idt->idtr.base = (uintptr_t)&idt->table;
    idt->idtr.limit = sizeof(idt->table) - 1;
}

static int valid_idt(const struct bootstrap_idt_state *idt, uint16_t selector)
{
    if (idt->idtr.base != (uintptr_t)&idt->table ||
        !canonical48(idt->idtr.base) || idt->idtr.base % 16 != 0 ||
        idt->idtr.limit != sizeof(idt->table) - 1) {
        return 0;
    }
    for (unsigned int i = 0; i < IDT_MAX_ENTRIES; ++i) {
        const struct idt_entry *gate = &idt->table.entries[i];
        uint64_t target = ((uint64_t)gate->offset_high << 32) |
                          ((uint64_t)gate->offset_middle << 16) |
                          gate->offset_low;
        if (gate->reserved || gate->ist) {
            return 0;
        }
        if (i < TRANSITION_EXCEPTION_COUNT) {
            if (target != (uintptr_t)transition_fatal_handlers[i] ||
                !target || !canonical48(target) ||
                gate->selector != selector ||
                gate->type_attributes != IDT_INTERRUPT_GATE) {
                return 0;
            }
        } else if (target || gate->selector || gate->type_attributes) {
            return 0;
        }
    }
    return 1;
}

static int valid_bootstrap_tss(const struct tss64 *tss)
{
    /* Obtain the actual stack tops from their owner, using a private TSS.
     * Preflight already checks alignment, separation and unused IST slots. */
    struct tss64 reference;
    tss_prepare_bootstrap(&reference);
    for (unsigned int i = 0; i < 3; ++i) {
        if (tss->rsp[i] || tss->ist[i] != reference.ist[i]) {
            return 0;
        }
    }
    return !tss->reserved0 && !tss->reserved1 &&
           !tss->reserved2 && !tss->reserved3;
}

const struct bootstrap_idt_state *descriptor_transition_prepare_early_idt(void)
{
    struct bootstrap_idt_state *early = &transition_storage.inherited_idt;
    if (!early_idt_published) {
        prepare_idt(early, TRANSITION_INHERITED_CS);
    }
    if (!valid_idt(early, TRANSITION_INHERITED_CS)) {
        return NULL;
    }
    if (!early_idt_published) {
        early_idt_published = 1;
    }
    return early;
}

int descriptor_transition_validate(const struct descriptor_transition *tables,
                                   const struct bootstrap_cpu_tables *cpu,
                                   const struct bootstrap_idt_state *irq)
{
    if (!tables || !cpu_tables_preflight_check(cpu, irq) ||
        !exception_gate_identity_check(&irq->table) ||
        !valid_bootstrap_tss(&cpu->tss)) {
        return 0;
    }
    if (tables->gdtr.base != (uintptr_t)tables->gdt ||
        !canonical48(tables->gdtr.base) || tables->gdtr.base % 8 != 0 ||
        tables->gdtr.limit != sizeof(tables->gdt) - 1) {
        return 0;
    }
    for (unsigned int i = 0; i < sizeof(bridge_slots) / sizeof(bridge_slots[0]); ++i) {
        if (tables->gdt[i] != bridge_slots[i]) {
            return 0;
        }
    }
    return valid_idt(&tables->inherited_idt, TRANSITION_INHERITED_CS) &&
           valid_idt(&tables->target_idt, GDT_KERNEL_CODE_SELECTOR);
}

int descriptor_transition_prepare(void)
{
    const struct bootstrap_cpu_tables *cpu = bootstrap_cpu_tables_get();
    const struct bootstrap_idt_state *irq = bootstrap_idt_get();
    if (!cpu || !irq || !descriptor_transition_prepare_early_idt()) {
        return 0;
    }
    if (!published) {
        for (unsigned int i = 0; i < sizeof(bridge_slots) / sizeof(bridge_slots[0]); ++i) {
            transition_storage.gdt[i] = bridge_slots[i];
        }
        transition_storage.gdtr.base = (uintptr_t)transition_storage.gdt;
        transition_storage.gdtr.limit = sizeof(transition_storage.gdt) - 1;
        prepare_idt(&transition_storage.target_idt, GDT_KERNEL_CODE_SELECTOR);
    }
    if (!descriptor_transition_validate(&transition_storage, cpu, irq)) {
        return 0;
    }
    published = 1;
    return 1;
}

const struct descriptor_transition *descriptor_transition_get(void)
{
    return published ? &transition_storage : NULL;
}
