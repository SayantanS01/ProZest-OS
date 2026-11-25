#include "vmm.h"
#include <stddef.h>
int vmm_canonical(uint64_t v) { return (v>>48)==(((v>>47)&1)?UINT64_C(0xffff):0); }
unsigned vmm_index(uint64_t v,unsigned l) { return l>=1&&l<=4?(unsigned)((v>>(12+9*(l-1)))&511):512; }
static int frame_ok(const struct vmm_space *s,uint64_t p) { return p && !(p&4095) && p<(UINT64_C(1)<<s->bits); }
static int valid(const struct vmm_space *s) { return s && s->bits>=32 && s->bits<=52 && s->nx<=1 && s->huge1g<=1 && s->io.resolve && frame_ok(s,s->root); }
static int private_va(const struct vmm_space *s,uint64_t va) {
    return valid(s)&&s->io.alloc&&s->io.release&&s->io.invalidate&&s->nx&&s->table_pages&&
           vmm_canonical(s->window)&&vmm_index(s->window,4)>=256&&vmm_index(s->window,4)<511&&
           !(s->window&((UINT64_C(1)<<39)-1))&&vmm_canonical(va)&&!(va&4095)&&vmm_index(va,4)==vmm_index(s->window,4);
}
static int flags_ok(uint64_t f) { return f==VMM_READ_ONLY||f==VMM_READ_WRITE||f==VMM_EXECUTE; }
static int entry_ok(const struct vmm_space *s,uint64_t e,unsigned level) {
    if (!(e&VMM_P) || (e&VMM_ADDR)>=(UINT64_C(1)<<s->bits) || (!s->nx&&(e&VMM_NX))) return 0;
    if (level==4&&(e&VMM_PS)) return 0;
    if (level==3&&(e&VMM_PS)&&!s->huge1g) return 0;
    if ((level==2||level==3)&&(e&VMM_PS)) {
        uint64_t mask=(UINT64_C(1)<<(12+9*(level-1)))-1;
        if (e&VMM_ADDR&mask&~UINT64_C(0x1000)) return 0;
    }
    return 1;
}
enum vmm_status vmm_translate(const struct vmm_space *s,uint64_t va,struct vmm_translation *out) {
    if (!valid(s)||!out||!vmm_canonical(va)) return VMM_INVALID;
    uint64_t p=s->root, flags=VMM_P|VMM_W|VMM_U;
    for (unsigned l=4;l;l--) {
        volatile uint64_t *t=s->io.resolve(s->io.ctx,p);
        if (!t) return VMM_BROKEN;
        uint64_t e=t[vmm_index(va,l)];
        if (!(e&VMM_P)) return VMM_ABSENT;
        if (!entry_ok(s,e,l)) return VMM_BROKEN;
        flags &= e|(VMM_P|VMM_NX); flags |= e&VMM_NX;
        p=e&VMM_ADDR;
        if (l==1||((l==2||l==3)&&(e&VMM_PS))) {
            uint64_t size=UINT64_C(1)<<(12+9*(l-1));
            *out=(struct vmm_translation){(p&~(size-1))+(va&(size-1)),size,flags};
            return VMM_OK;
        }
        if (!frame_ok(s,p)) return VMM_BROKEN;
    }
    return VMM_BROKEN;
}
static enum vmm_status fresh(struct vmm_space *s,uint64_t *p,volatile uint64_t **t) {
    enum vmm_status r=s->io.alloc(s->io.ctx,p);
    if (r!=VMM_OK) return r;
    if (!frame_ok(s,*p)||!(*t=s->io.resolve(s->io.ctx,*p))) {
        (void)s->io.release(s->io.ctx,*p); return VMM_BROKEN;
    }
    for (unsigned i=0;i<512;i++) (*t)[i]=0;
    return VMM_OK;
}
enum vmm_status vmm_clone(struct vmm_space *out,const struct vmm_backend *io,uint64_t root,unsigned bits,unsigned nx,unsigned huge1g) {
    if (!out||!io||!io->alloc||!io->release||!io->resolve||!io->invalidate||nx!=1) return VMM_INVALID;
    struct vmm_space s={.io=*io,.root=root,.bits=bits,.nx=nx,.huge1g=huge1g};
    if (!valid(&s)) return VMM_INVALID;
    volatile uint64_t *old=io->resolve(io->ctx,root),*new;
    if (!old) return VMM_BROKEN;
    unsigned slot=256;
    while (slot<511&&old[slot]) slot++;
    if (slot==511) return VMM_OOM;
    enum vmm_status r=fresh(&s,&s.root,&new);
    if (r!=VMM_OK) return r;
    for (unsigned i=0;i<512;i++) new[i]=old[i];
    s.window=UINT64_C(0xffff000000000000)|((uint64_t)slot<<39);
    s.table_pages=1; *out=s; return VMM_OK;
}
/* Walk only our branch. Every nonzero intermediate entry must be a present
 * supervisor RW non-huge table entry, with optional hardware accessed bit. */
static enum vmm_status path(struct vmm_space *s,uint64_t va,volatile uint64_t **t,uint64_t *p,unsigned *missing) {
    p[4]=s->root;
    for (unsigned l=4;l;l--) {
        t[l]=s->io.resolve(s->io.ctx,p[l]);
        if (!t[l]) return VMM_BROKEN;
        uint64_t e=t[l][vmm_index(va,l)];
        if (!e) { *missing=l; return VMM_ABSENT; }
        if (!entry_ok(s,e,l)||!frame_ok(s,e&VMM_ADDR)) return VMM_BROKEN;
        if (l==1) {
            if (e&~(VMM_ADDR|VMM_P|VMM_W|VMM_NX|UINT64_C(0x60))) return VMM_BROKEN;
            if (!flags_ok(e&(VMM_W|VMM_NX))) return VMM_BROKEN;
            *missing=0; return VMM_OK;
        }
        if ((e&~(VMM_ADDR|UINT64_C(0x20)))!=(VMM_P|VMM_W)) return VMM_BROKEN;
        p[l-1]=e&VMM_ADDR;
    }
    return VMM_BROKEN;
}
enum vmm_status vmm_map_page(struct vmm_space *s,uint64_t va,uint64_t flags,uint64_t *out) {
    if (!private_va(s,va)||!flags_ok(flags)||!out) return VMM_INVALID;
    volatile uint64_t *t[5]={0},*data; uint64_t p[5]={0},frames[4]; unsigned m=0,n=0;
    enum vmm_status r=path(s,va,t,p,&m);
    if (r==VMM_OK) return VMM_EXISTS;
    if (r!=VMM_ABSENT) return r;
    /* Build all missing tables and data off-tree, then publish one link. */
    for (unsigned l=m-1;l;l--) {
        r=fresh(s,&p[l],&t[l]); if (r!=VMM_OK) goto rollback;
        frames[n++]=p[l];
    }
    uint64_t pa;
    r=fresh(s,&pa,&data); if (r!=VMM_OK) goto rollback;
    for (unsigned l=1;l<=m;l++) {
        t[l][vmm_index(va,l)]=l==1?(pa|VMM_P|flags):(p[l-1]|VMM_P|VMM_W);
    }
    s->table_pages+=n; s->mapped++;
    s->io.invalidate(s->io.ctx,va); *out=pa; return VMM_OK;
rollback:
    while (n) if (!s->io.release(s->io.ctx,frames[--n])) r=VMM_BROKEN;
    return r;
}
static int empty(volatile uint64_t *t) { for (unsigned i=0;i<512;i++) if(t[i]) return 0; return 1; }
enum vmm_status vmm_unmap_page(struct vmm_space *s,uint64_t va) {
    if (!private_va(s,va)) return VMM_INVALID;
    volatile uint64_t *t[5]={0}; uint64_t p[5]={0},detached[3]; unsigned m,n=0;
    enum vmm_status r=path(s,va,t,p,&m); if(r!=VMM_OK) return r;
    if (!s->mapped) return VMM_BROKEN;
    uint64_t pa=t[1][vmm_index(va,1)]&VMM_ADDR;
    t[1][vmm_index(va,1)]=0;
    for (unsigned l=1;l<4;l++) {
        if (!empty(t[l])) break;
        t[l+1][vmm_index(va,l+1)]=0; detached[n++]=p[l];
    }
    /* Invalidate after unlinking all levels, before any physical reuse. */
    s->io.invalidate(s->io.ctx,va);
    s->mapped--; s->table_pages-=n;
    if (!s->io.release(s->io.ctx,pa)) r=VMM_BROKEN;
    while(n) if(!s->io.release(s->io.ctx,detached[--n])) r=VMM_BROKEN;
    return r;
}
enum vmm_status vmm_protect_page(struct vmm_space *s,uint64_t va,uint64_t flags) {
    if (!private_va(s,va)||!flags_ok(flags)) return VMM_INVALID;
    volatile uint64_t *t[5]={0}; uint64_t p[5]={0}; unsigned m;
    enum vmm_status r=path(s,va,t,p,&m); if(r!=VMM_OK) return r;
    unsigned i=vmm_index(va,1);
    t[1][i]=(t[1][i]&~(VMM_W|VMM_NX))|flags;
    s->io.invalidate(s->io.ctx,va); return VMM_OK;
}
enum vmm_status vmm_destroy_empty(struct vmm_space *s) {
    if (!valid(s)||!private_va(s,s->window)||s->mapped||s->table_pages!=1) return VMM_INVALID;
    volatile uint64_t *t=s->io.resolve(s->io.ctx,s->root);
    if (!t||t[vmm_index(s->window,4)]) return VMM_BROKEN;
    if (!s->io.release(s->io.ctx,s->root)) return VMM_BROKEN;
    s->root=0; s->table_pages=0; return VMM_OK;
}
