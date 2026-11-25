#ifndef PROZEST_PAGE_TABLE_INVENTORY_H
#define PROZEST_PAGE_TABLE_INVENTORY_H
#include <stdint.h>
#include "memory_map.h"
#define PAGE_TABLE_CAPACITY 4096u
struct page_table_frame { uint64_t physical; uint8_t level; };
struct page_table_inventory {
    struct page_table_frame frame[PAGE_TABLE_CAPACITY];
    uint32_t count, by_level[5], large_1g, large_2m, shared;
    uint64_t root;
};
typedef const volatile uint64_t *(*page_table_resolver)(uint64_t physical, void *context);
int page_tables_walk(struct page_table_inventory *out, uint64_t cr3,
                     uint64_t cr0, uint64_t cr4, uint64_t efer,
                     unsigned physical_bits, page_table_resolver resolve, void *context);
struct hhdm_table_context { const struct memory_inventory *map; uint64_t offset; };
const volatile uint64_t *page_table_hhdm_resolve(uint64_t physical, void *context);
#endif
