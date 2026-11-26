/* Bounded BSP-only Ring 3 tests. No user scheduling or untrusted ELF loader. */
#include "user_abi.h"
#include "bootstrap_tables.h"
#include <stddef.h>
extern void user_enter(uint64_t,uint64_t,uint64_t),user_syscall_entry(void),user_pf_entry(void),user_gp_entry(void);
extern const unsigned char user_image_start[],user_image_end[],user_normal[],user_read_kernel[],user_write_code[],user_exec_data[],user_read_guard[],user_cli[];
struct user_frame { uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,vector,error,rip,cs,rflags,rsp,ss; };
_Static_assert(sizeof(struct user_frame)==176&&offsetof(struct user_frame,rip)==136,"user interrupt frame");
struct user_gdt { struct gdt_table kernel; uint64_t data,code; } __attribute__((aligned(8)));
_Static_assert(sizeof(struct user_gdt)==56,"extended GDT");
static struct user_gdt user_gdt;
static unsigned char user_kstack[16384] __attribute__((aligned(16)));
struct user_space { struct vmm_space view; uint64_t frames[7]; unsigned id; };
static struct user_space user_spaces[2],*user_current;
static unsigned user_mode,user_running,user_calls,user_writes,user_rejected,user_unknown,user_faults,user_exits;
static uint64_t user_kernel_probe;
static volatile uint64_t *user_take(uint64_t *pa){
    check("USER FRAME ALLOC",vmm_take(NULL,pa)==VMM_OK);
    volatile uint64_t *p=vmm_resolve(NULL,*pa);check("USER FRAME RESOLVE",p!=NULL);
    for(unsigned i=0;i<512;i++)p[i]=0;
    return p;
}
static int user_translation(struct user_space *s,uint64_t va,uint64_t pa,uint64_t flags){
    struct vmm_translation t;
    return vmm_translate(&s->view,va,&t)==VMM_OK&&t.physical==pa&&t.page_size==4096&&t.flags==flags;
}
static void user_space_create(struct user_space *s,unsigned id){
    volatile uint64_t *r=user_take(&s->frames[0]),*pdpt=user_take(&s->frames[1]),*pd=user_take(&s->frames[2]),*pt=user_take(&s->frames[3]);
    volatile uint64_t *old=vmm_resolve(NULL,vmm_active.root);check("USER KERNEL ROOT",old!=NULL);
    for(unsigned i=256;i<512;i++)r[i]=old[i]&~VMM_U;
    r[0]=s->frames[1]|7;pdpt[0]=s->frames[2]|7;pd[2]=s->frames[3]|7;
    volatile uint8_t *code=(volatile uint8_t *)user_take(&s->frames[4]);
    volatile uint8_t *data=(volatile uint8_t *)user_take(&s->frames[5]);
    (void)user_take(&s->frames[6]);
    size_t n=(size_t)((uintptr_t)user_image_end-(uintptr_t)user_image_start);
    check("USER IMAGE SIZE",n>0&&n<=4096);
    for(size_t i=0;i<n;i++)code[i]=user_image_start[i];
    *(volatile uint64_t *)data=id;
    const char msg[]="PROZEST";for(unsigned i=0;i<7;i++)data[8+i]=(uint8_t)msg[i];
    data[64]=0xc3;
    pt[0]=s->frames[4]|VMM_P|VMM_U;
    pt[1]=s->frames[5]|VMM_P|VMM_U|VMM_W|VMM_NX;
    pt[3]=s->frames[6]|VMM_P|VMM_U|VMM_W|VMM_NX;
    s->view=vmm_active;s->view.root=s->frames[0];s->id=id;
    struct vmm_translation t;
    check("USER PAGE PERMISSIONS",user_translation(s,USER_CODE,s->frames[4],VMM_P|VMM_U)&&
        user_translation(s,USER_DATA,s->frames[5],VMM_P|VMM_U|VMM_W|VMM_NX)&&
        user_translation(s,USER_STACK,s->frames[6],VMM_P|VMM_U|VMM_W|VMM_NX)&&
        vmm_translate(&s->view,USER_GUARD,&t)==VMM_ABSENT&&
        vmm_translate(&s->view,user_kernel_probe,&t)==VMM_OK&&!(t.flags&VMM_U));
    for(unsigned i=0;i<7;i++)for(unsigned j=0;j<i;j++)check("USER FRAME UNIQUE",s->frames[i]!=s->frames[j]);
    qemu_debugcon_puts("USER FRAMESET ");qemu_debugcon_hex64(id);
    for(unsigned i=0;i<7;i++){qemu_debugcon_putc(' ');qemu_debugcon_hex64(s->frames[i]);}
    qemu_debugcon_putc('\n');
}
static uint64_t user_data_value(struct user_space *s){return *vmm_resolve(NULL,s->frames[5]);}
int user_dispatch(struct user_frame *f){
    uint64_t cr2;__asm__ volatile("mov %%cr2,%0":"=r"(cr2));
    uintptr_t a=(uintptr_t)f,lo=(uintptr_t)user_kstack;
    check("USER ENTRY FRAME",user_running&&user_current&&a>=lo+256&&a<=lo+sizeof(user_kstack)-sizeof(*f)&&
        f->cs==USER_CS&&f->ss==USER_SS&&!(f->rflags&0x3200)&&!(irq_flags()&0x600)&&
        f->rsp>=USER_STACK&&f->rsp<=USER_TOP&&vmm_cr3()==(user_current->view.root|(vmm_old_cr3&0x18)));
    if(f->vector!=128){
        check("USER EXPECTED FAULT",user_fault_expected(user_mode,f->vector,f->error,cr2,user_kernel_probe));
        qemu_debugcon_puts("USER FAULT ");qemu_debugcon_hex64(user_mode);qemu_debugcon_putc(' ');qemu_debugcon_hex64(f->vector);qemu_debugcon_putc(' ');qemu_debugcon_hex64(f->error);qemu_debugcon_putc(' ');qemu_debugcon_hex64(cr2);qemu_debugcon_putc('\n');
        user_faults++;user_running=0;return 1;
    }
    check("USER SYSCALL FRAME",!user_mode&&!f->error&&f->rip>=USER_CODE&&f->rip<USER_DATA);
    user_calls++;
    switch(f->rax){
    case 0:f->rax=user_current->id;break;
    case 1:
        if(!user_write_range(f->rdi,f->rsi)){user_rejected++;f->rax=(uint64_t)-14;break;}
        {const volatile uint8_t *p=(const volatile uint8_t *)vmm_resolve(NULL,user_current->frames[5]);
        qemu_debugcon_puts("M16 USER WRITE: ");
        for(uint64_t i=0;i<f->rsi;i++){uint8_t c=p[f->rdi-USER_DATA+i];qemu_debugcon_putc(c>=32&&c<=126?(char)c:'?');}
        qemu_debugcon_putc('\n');user_writes++;f->rax=f->rsi;break;}
    case 2:check("USER EXIT STATUS",f->rdi==0);user_exits++;user_running=0;return 1;
    default:user_unknown++;f->rax=(uint64_t)-38;break;
    }
    return 0;
}
static void user_execute(struct user_space *s,const unsigned char *entry,unsigned mode,uint64_t arg){
    user_current=s;user_mode=mode;user_running=1;
    uint64_t offset=(uintptr_t)entry-(uintptr_t)user_image_start;
    check("USER ENTRY OFFSET",offset<(uintptr_t)user_image_end-(uintptr_t)user_image_start);
    uint64_t rsp;__asm__ volatile("mov %%rsp,%0":"=r"(rsp));
    struct vmm_translation t;
    check("USER KERNEL STACK MAPPING",rsp>=UINT64_C(0xffff800000000000)&&
        vmm_translate(&s->view,rsp-1,&t)==VMM_OK&&(t.flags&VMM_W)&&!(t.flags&VMM_U));
    vmm_switch(s->view.root);
    user_enter(USER_CODE+offset,USER_TOP,arg);
    check("USER KERNEL RETURN",!user_running&&!(irq_flags()&0x200));
    vmm_switch(vmm_active.root);user_current=NULL;
}
static void user_run(void){
    uint16_t cs,tr;uint64_t cr4;
    __asm__ volatile("mov %%cs,%0; str %1":"=r"(cs),"=r"(tr));
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    check("M16.1 PRIVILEGE CONTEXT: PASS",cs==8&&tr==24&&!(irq_flags()&0x3200)&&vmm_counts(&vmm_active));
    /* Flush global translations as well as ordinary CR3-tagged entries. */
    uint64_t no_pge=cr4&~UINT64_C(0x80);
    __asm__ volatile("mov %0,%%cr4"::"r"(no_pge):"memory");
    const struct bootstrap_cpu_tables *cpu=bootstrap_cpu_tables_get();
    check("USER TSS READY",cpu&&cpu->tss.iomap_base==sizeof(cpu->tss)&&cpu->gdt.tss_descriptor.access==0x8b);
    struct tss64 *tss=(struct tss64 *)(uintptr_t)&cpu->tss;
    uint64_t saved_rsp0=tss->rsp[0];tss->rsp[0]=(uintptr_t)(user_kstack+sizeof(user_kstack));
    user_gdt.kernel=cpu->gdt;user_gdt.data=UINT64_C(0x00cff3000000ffff);user_gdt.code=UINT64_C(0x00affb000000ffff);
    struct gdt_pointer gdtr={sizeof(user_gdt)-1,(uintptr_t)&user_gdt};
    __asm__ volatile("lgdt %0"::"m"(gdtr):"memory");
    check("M16.2 USER GDT: PASS",rights_match(USER_CS,0x60fe00,0x20fa00)&&rights_match(USER_SS,0xfe00,0xf200));
    user_kernel_probe=(uintptr_t)user_run;
    user_space_create(&user_spaces[0],1);user_space_create(&user_spaces[1],2);
    check("M16.3 USER PAGE TABLES: PASS",1);
    for(unsigned i=0;i<7;i++)for(unsigned j=0;j<7;j++)check("USER SPACE DISJOINT",user_spaces[0].frames[i]!=user_spaces[1].frames[j]);
    check("M16.4 ADDRESS SPACE ISOLATION: PASS",user_data_value(&user_spaces[0])==1&&user_data_value(&user_spaces[1])==2);
    check("M16.5 USER STACK: PASS",tss->rsp[0]==(uintptr_t)(user_kstack+sizeof(user_kstack)));
    struct irq_gate old80=irq_idt[128],oldpf=irq_idt[14],oldgp=irq_idt[13];
    irq_gate_set(128,user_syscall_entry);irq_idt[128].attr=0xee;
    irq_gate_set(14,user_pf_entry);irq_gate_set(13,user_gp_entry);
    check("M16.6 SYSCALL GATE: PASS",irq_idt[128].attr==0xee&&irq_idt[14].attr==0x8e&&irq_idt[13].attr==0x8e);
    user_execute(&user_spaces[0],user_normal,0,1);
    check("USER FIRST PROCESS",user_data_value(&user_spaces[0])==2&&user_data_value(&user_spaces[1])==2);
    user_execute(&user_spaces[1],user_normal,0,2);
    check("M16.7 SYSCALL DISPATCH: PASS",user_calls==10&&user_writes==2&&user_rejected==2&&user_unknown==2&&user_exits==2);
    check("M16.8 USER PROGRAMS: PASS",user_data_value(&user_spaces[0])==2&&user_data_value(&user_spaces[1])==3);
    const unsigned char *faults[]={user_read_kernel,user_write_code,user_exec_data,user_read_guard,user_cli};
    for(unsigned i=0;i<5;i++)user_execute(&user_spaces[0],faults[i],i+1,user_kernel_probe);
    check("M16.9 PRIVILEGE FAULTS: PASS",user_faults==5&&user_calls==10);
    irq_idt[128]=old80;irq_idt[14]=oldpf;irq_idt[13]=oldgp;
    __asm__ volatile("lgdt %0"::"m"(cpu->gdtr):"memory");tss->rsp[0]=saved_rsp0;
    __asm__ volatile("mov %0,%%cr4"::"r"(cr4):"memory");
    for(unsigned i=0;i<2;i++)for(unsigned j=7;j;j--)check("USER FRAME RELEASE",pmm_free_page(user_spaces[i].frames[j-1])==PMM_OK);
    check("M16.10 USERSPACE CLEANUP: PASS",vmm_counts(&vmm_active)&&vmm_cr3()==(vmm_active.root|(vmm_old_cr3&0x18)));
    number("USER SYSCALLS: ",user_calls);number("USER FAULTS: ",user_faults);number("USER KERNEL PROBE: ",user_kernel_probe);
    log_write(LOG_OK,"Ring 3 programs, syscalls and privilege faults verified");qemu_debugcon_puts("M16 COMPLETE\n");
}
