#ifndef PROZEST_VMM_H
#define PROZEST_VMM_H
#include <stdint.h>
#define VMM_PAGE UINT64_C(4096)
#define VMM_ADDR UINT64_C(0x000ffffffffff000)
#define VMM_P UINT64_C(1)
#define VMM_W UINT64_C(2)
#define VMM_U UINT64_C(4)
#define VMM_PS UINT64_C(128)
#define VMM_NX (UINT64_C(1)<<63)
#define VMM_READ_ONLY VMM_NX
#define VMM_READ_WRITE (VMM_NX|VMM_W)
#define VMM_EXECUTE UINT64_C(0)
enum vmm_status { VMM_OK=0, VMM_INVALID, VMM_ABSENT, VMM_EXISTS, VMM_OOM, VMM_BROKEN };
/* Trusted BSP-only backend. alloc returns an exclusively owned 4 KiB frame;
 * release must succeed for an owned frame. resolve must return a stable mapping.
 * invalidate runs after page-table writes, before detached frames are released. */
struct vmm_backend {
    void *ctx;
    volatile uint64_t *(*resolve)(void *,uint64_t);
    enum vmm_status (*alloc)(void *,uint64_t *);
    int (*release)(void *,uint64_t);
    void (*invalidate)(void *,uint64_t);
};
struct vmm_space {
    struct vmm_backend io;
    uint64_t root, window, mapped, table_pages;
    unsigned bits, nx, huge1g;
};
struct vmm_translation { uint64_t physical, page_size, flags; };
int vmm_canonical(uint64_t va);
unsigned vmm_index(uint64_t va,unsigned level);
enum vmm_status vmm_translate(const struct vmm_space *,uint64_t va,struct vmm_translation *);
/* Clone only the PML4. Inherited subtrees remain shared and immutable to this API.
 * The selected empty upper-half slot is exclusively owned by the new space. */
enum vmm_status vmm_clone(struct vmm_space *,const struct vmm_backend *,uint64_t root,unsigned bits,unsigned nx,unsigned huge1g);
/* Allocate a zeroed data page and map it. No arbitrary physical aliases accepted.
 * Only aligned addresses in the private slot, supervisor, no W+X. */
enum vmm_status vmm_map_page(struct vmm_space *,uint64_t va,uint64_t flags,uint64_t *pa);
enum vmm_status vmm_unmap_page(struct vmm_space *,uint64_t va);
enum vmm_status vmm_protect_page(struct vmm_space *,uint64_t va,uint64_t flags);
/* Only after switching away from this root and removing all private mappings. */
enum vmm_status vmm_destroy_empty(struct vmm_space *);
#endif
