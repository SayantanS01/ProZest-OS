#ifndef PROZEST_KHEAP_H
#define PROZEST_KHEAP_H
#include <stddef.h>
#include <stdint.h>
#define KHEAP_EXTENTS 256u
#define KHEAP_ALIGN 16u
struct heap_extent { size_t off,len; unsigned used; };
struct kheap { unsigned char *base; size_t bytes; unsigned count; struct heap_extent e[KHEAP_EXTENTS]; };
struct heap_stats { size_t total,used,free,largest; unsigned live,extents; };
int kheap_init(struct kheap *,void *,size_t);
int kheap_check(const struct kheap *,struct heap_stats *);
void *kheap_alloc(struct kheap *,size_t);
int kheap_free(struct kheap *,void *);
void *kheap_calloc(struct kheap *,size_t,size_t);
void *kheap_realloc(struct kheap *,void *,size_t);
/* Kernel wrappers require CPL0 and IF=0; no IRQ allocation. */
void *kmalloc(size_t);
int kfree(void *);
void *kcalloc(size_t,size_t);
void *krealloc(void *,size_t);
#endif
