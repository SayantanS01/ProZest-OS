#include <stddef.h>
#include <stdint.h>
#include "memory_map.h"

const char *memory_type_name(uint64_t type)
{
    static const char *const names[] = {
        "usable", "reserved", "ACPI reclaimable", "ACPI NVS",
        "bad", "bootloader reclaimable", "kernel/modules",
        "framebuffer", "reserved mapped", "unknown"
    };
    return names[type <= 8 ? type : 9];
}

int memory_map_build(const struct limine_memmap_response *map,
                     const struct limine_hhdm_response *hhdm,
                     unsigned physical_bits, struct memory_inventory *out)
{
    if (!map || !hhdm || !out || !map->entries ||
        !map->entry_count || map->entry_count > MEMORY_MAP_CAPACITY ||
        physical_bits < 32 || physical_bits > 63 ||
        (hhdm->offset & (MEMORY_PAGE_SIZE - 1)))
        return 0;
    struct memory_inventory result;
    for (size_t i=0; i<sizeof(result); ++i) ((unsigned char *)&result)[i]=0;
    uint64_t previous_base = 0;
    uint64_t usable_end = 0;
    uint64_t maximum = UINT64_C(1) << physical_bits;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (!entry || !entry->length || entry->base >= maximum ||
            entry->length > maximum - entry->base ||
            (i && entry->base < previous_base) ||
            (entry->type == LIMINE_MEMMAP_USABLE && entry->base < usable_end))
            return 0;
        uint64_t end = entry->base + entry->length;
        if (entry->type == LIMINE_MEMMAP_USABLE && i) {
            for (uint64_t j = 0; j < i; ++j) {
                const struct limine_memmap_entry *prior = map->entries[j];
                if (prior->base < end && entry->base < prior->base + prior->length)
                    return 0;
            }
        } else if (entry->type != LIMINE_MEMMAP_USABLE) {
            for (uint64_t j = 0; j < i; ++j) {
                const struct limine_memmap_entry *prior = map->entries[j];
                if (prior->type == LIMINE_MEMMAP_USABLE &&
                    prior->base < end && entry->base < prior->base + prior->length)
                    return 0;
            }
        }
        if (UINT64_MAX - result.described < entry->length)
            return 0;
        result.described += entry->length;
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t aligned = entry->base;
            if (aligned & 4095) {
                if (aligned > UINT64_MAX - 4095)
                    return 0;
                aligned = (aligned + 4095) & ~UINT64_C(4095);
            }
            uint64_t pages = end > aligned ? ((end & ~UINT64_C(4095)) - aligned) / 4096 : 0;
            if (UINT64_MAX - result.candidates < pages ||
                UINT64_MAX - result.usable < entry->length)
                return 0;
            result.candidates += pages;
            result.usable += entry->length;
            result.region[i].pages = pages;
        }
        unsigned category = entry->type <= 8 ? (unsigned)entry->type : 9;
        if (UINT64_MAX - result.category[category] < entry->length)
            return 0;
        result.category[category] += entry->length;
        result.region[i].base = entry->base;
        result.region[i].end = end;
        result.region[i].type = entry->type;
        previous_base = entry->base;
        if (entry->type == LIMINE_MEMMAP_USABLE) usable_end = end;
        if (end > result.highest) result.highest = end;
    }
    result.count = map->entry_count;
    result.hhdm = hhdm->offset;
    for (size_t i=0; i<sizeof(result); ++i) ((unsigned char *)out)[i]=((const unsigned char *)&result)[i];
    return 1;
}
