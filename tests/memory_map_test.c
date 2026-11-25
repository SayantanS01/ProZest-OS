#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <limine.h>
#include "memory_map.h"
static struct limine_memmap_entry entry[3];
static struct limine_memmap_entry *ptr[3];
static struct limine_memmap_response map;
static struct limine_hhdm_response hhdm;
static struct memory_inventory out;
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static void reset(void) {
    entry[0]=(struct limine_memmap_entry){.base=0x1000,.length=0x2000,.type=LIMINE_MEMMAP_USABLE};
    entry[1]=(struct limine_memmap_entry){.base=0x3000,.length=0x1000,.type=LIMINE_MEMMAP_RESERVED};
    entry[2]=(struct limine_memmap_entry){.base=0x5000,.length=0x2000,.type=LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE};
    for (int i=0;i<3;i++) ptr[i]=&entry[i];
    map=(struct limine_memmap_response){.entry_count=3,.entries=ptr};
    hhdm=(struct limine_hhdm_response){.offset=0xffff800000000000ULL};
}
int main(void) {
    reset(); CHECK(memory_map_build(&map,&hhdm,36,&out));
    CHECK(out.count==3); CHECK(out.described==0x5000); CHECK(out.highest==0x7000);
    CHECK(out.usable==0x2000); CHECK(out.candidates==2); CHECK(out.category[1]==0x1000);
    CHECK(out.category[5]==0x2000); CHECK(out.region[1].pages==0);
    CHECK(!memory_map_build(0,&hhdm,36,&out)); CHECK(!memory_map_build(&map,0,36,&out));
    CHECK(!memory_map_build(&map,&hhdm,0,&out)); CHECK(!memory_map_build(&map,&hhdm,64,&out));
    map.entry_count=0; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    map.entry_count=257; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    map.entry_count=3; map.entries=0; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    reset(); ptr[1]=0; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    reset(); entry[1].length=0; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    reset(); entry[1].base=0x2000; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    reset(); entry[1].base=0; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    reset(); entry[2].base=(1ULL<<36)-0x1000; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    reset(); hhdm.offset++; CHECK(!memory_map_build(&map,&hhdm,36,&out));
    reset(); entry[0].base=0x1001; entry[0].length=0x1ffe;
    CHECK(memory_map_build(&map,&hhdm,36,&out)); CHECK(out.candidates==0);
    reset(); entry[0].type=42; CHECK(memory_map_build(&map,&hhdm,36,&out));
    CHECK(out.candidates==0); CHECK(out.category[9]==0x2000);
    CHECK(memory_type_name(42)[0]=='u');
    reset(); entry[0].base=0x1001; entry[0].length=0x2fff; entry[1].base=0x4000;
    CHECK(memory_map_build(&map,&hhdm,36,&out)); CHECK(out.candidates==2);
    reset(); entry[1].base=0x3000; entry[1].length=0x3000;
    CHECK(memory_map_build(&map,&hhdm,36,&out));
    CHECK(out.highest==0x7000);
    printf("%u memory-map checks passed\n",checks);
}
