#include "kheap.h"
static int overlap(uintptr_t a,size_t n,uintptr_t b,size_t m){return a<=b?b-a<n:a-b<m;}
int kheap_init(struct kheap *h,void *base,size_t n){
    if(!h||!base||((uintptr_t)base%KHEAP_ALIGN)||!n||n%KHEAP_ALIGN||n>UINTPTR_MAX-(uintptr_t)base||overlap((uintptr_t)h,sizeof(*h),(uintptr_t)base,n)||h->base)return 0;
    h->base=base;h->bytes=n;h->count=1;h->e[0]=(struct heap_extent){0,n,0};return 1;
}
int kheap_check(const struct kheap *h,struct heap_stats *out){
    if(!h||!h->base||!h->bytes||h->bytes%KHEAP_ALIGN||((uintptr_t)h->base%KHEAP_ALIGN)||h->bytes>UINTPTR_MAX-(uintptr_t)h->base||!h->count||h->count>KHEAP_EXTENTS)return 0;
    if(out&&(overlap((uintptr_t)out,sizeof(*out),(uintptr_t)h,sizeof(*h))||overlap((uintptr_t)out,sizeof(*out),(uintptr_t)h->base,h->bytes)))return 0;
    struct heap_stats s={.total=h->bytes,.extents=h->count};size_t off=0;
    for(unsigned i=0;i<h->count;i++){
        const struct heap_extent *e=&h->e[i];
        if(e->off!=off||!e->len||e->len%KHEAP_ALIGN||e->len>h->bytes-off||e->used>1||(!e->used&&i&&!h->e[i-1].used))return 0;
        off+=e->len;
        if(e->used){s.used+=e->len;s.live++;}else {s.free+=e->len;if(e->len>s.largest)s.largest=e->len;}
    }
    if(off!=h->bytes)return 0;
    if(out)*out=s;
    return 1;
}
static size_t rounded(size_t n){return n&&n<=SIZE_MAX-(KHEAP_ALIGN-1)?(n+KHEAP_ALIGN-1)&~(size_t)(KHEAP_ALIGN-1):0;}
void *kheap_alloc(struct kheap *h,size_t n){
    n=rounded(n);if(!n||!kheap_check(h,0))return 0;
    for(unsigned i=0;i<h->count;i++){
        struct heap_extent *e=&h->e[i];if(e->used||e->len<n)continue;
        if(e->len!=n){
            if(h->count==KHEAP_EXTENTS)continue;
            for(unsigned j=h->count;j>i+1;j--)h->e[j]=h->e[j-1];
            h->e[i+1]=(struct heap_extent){e->off+n,e->len-n,0};h->count++;e->len=n;
        }
        e->used=1;return h->base+e->off;
    }return 0;
}
static unsigned find(const struct kheap *h,const void *p){
    uintptr_t a=(uintptr_t)p,b=(uintptr_t)h->base;
    if(a<b||a-b>=h->bytes)return h->count;
    for(unsigned i=0;i<h->count;i++)if(h->e[i].used&&h->e[i].off==a-b)return i;
    return h->count;
}
static void merge_next(struct kheap *h,unsigned i){
    h->e[i].len+=h->e[i+1].len;
    for(unsigned j=i+1;j+1<h->count;j++)h->e[j]=h->e[j+1];
    h->count--;
}
int kheap_free(struct kheap *h,void *p){
    if(!kheap_check(h,0))return 0;
    if(!p)return 1;
    unsigned i=find(h,p);if(i==h->count)return 0;
    h->e[i].used=0;
    if(i+1<h->count&&!h->e[i+1].used)merge_next(h,i);
    if(i&&!h->e[i-1].used)merge_next(h,i-1);
    return 1;
}
void *kheap_calloc(struct kheap *h,size_t n,size_t size){
    if(!n||!size||n>SIZE_MAX/size)return 0;
    unsigned char *p=kheap_alloc(h,n*size);if(p)for(size_t i=0;i<n*size;i++)p[i]=0;return p;
}
void *kheap_realloc(struct kheap *h,void *p,size_t n){
    if(!p)return kheap_alloc(h,n);
    if(!kheap_check(h,0))return 0;
    unsigned i=find(h,p);if(i==h->count)return 0;
    if(!n){(void)kheap_free(h,p);return 0;}
    size_t need=rounded(n),old=h->e[i].len;if(!need)return 0;
    /* Retain capacity on shrink; grow by allocate/copy/free. Failure preserves p. */
    if(need<=old)return p;
    unsigned char *q=kheap_alloc(h,n);if(!q)return 0;
    for(size_t j=0;j<old;j++)q[j]=((unsigned char *)p)[j];
    (void)kheap_free(h,p);return q;
}
