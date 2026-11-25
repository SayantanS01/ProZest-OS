#ifndef PROZEST_IRQ_PLATFORM_H
#define PROZEST_IRQ_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
/* Reader returns 1 only for a completely readable physical byte range. */
typedef int (*irq_phys_read)(void *,uint64_t,void *,size_t);
struct irq_topology { uint64_t lapic; uint32_t ioapic,gsi_base,pit_gsi; uint16_t pit_flags; unsigned ioapic_id; };
int irq_acpi_discover(irq_phys_read,void *,uint64_t,struct irq_topology *);
int irq_madt_parse(const uint8_t *,size_t,struct irq_topology *);
uint64_t irq_pit_ms(uint64_t ticks,unsigned divisor);
#endif
