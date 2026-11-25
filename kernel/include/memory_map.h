#ifndef PROZEST_MEMORY_MAP_H
#define PROZEST_MEMORY_MAP_H
#include <limine.h>
#include <stdint.h>
#define MEMORY_MAP_CAPACITY 256u
#define MEMORY_PAGE_SIZE 4096u
struct memory_region { uint64_t base, end, type, pages; };
struct memory_inventory {
    struct memory_region region[MEMORY_MAP_CAPACITY];
    uint64_t count, described, highest, usable, candidates, category[10], hhdm;
};
int memory_map_build(const struct limine_memmap_response *map,
                     const struct limine_hhdm_response *hhdm,
                     unsigned physical_bits, struct memory_inventory *out);
const char *memory_type_name(uint64_t type);
#endif
