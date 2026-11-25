#ifndef PROZEST_MEMORY_RESERVATIONS_H
#define PROZEST_MEMORY_RESERVATIONS_H
#include <stdint.h>
#include "memory_map.h"
#define RESERVATION_CAPACITY 2048u
#define ELIGIBLE_CAPACITY 512u
struct physical_range { uint64_t base, end; };
struct reservation_item { struct physical_range range; uint32_t reason; };
struct reservation_inventory {
    struct reservation_item item[RESERVATION_CAPACITY];
    struct physical_range merged[RESERVATION_CAPACITY];
    struct physical_range eligible[ELIGIBLE_CAPACITY];
    uint32_t item_count, merged_count, eligible_count;
    uint64_t excluded_pages, eligible_pages, eligible_bytes, largest_bytes;
};
enum reservation_reason { RES_PAGE_ZERO=1, RES_NONUSABLE, RES_KERNEL,
    RES_DESCRIPTOR, RES_IST, RES_STACK, RES_PAGE_TABLE, RES_RESPONSE,
    RES_FRAMEBUFFER, RES_ENGINE };
int reservation_add(struct reservation_inventory *r, uint64_t base, uint64_t end,
                    uint32_t reason, unsigned physical_bits);
int reservations_build(struct reservation_inventory *r, const struct memory_inventory *map);
#endif
