/* Shared kernel address space, single CPU, integer-only tasks. Timer IRQ
 * preempts workers; software IRQ yields/exits. No allocation in dispatch. */
#include "scheduler.h"
extern void sched_timer_entry(void),sched_yield_entry(void);
#define TASK_STACK 16384u
#define TASK_CANARY UINT64_C(0x89ab76cd12ef3450)
struct sched_task { struct sched_frame *frame; unsigned char *stack; volatile uint64_t ticks,yields,work; };
static struct sched_task tasks[SCHED_TASKS];
static unsigned task_state[SCHED_TASKS],task_current;
static uint64_t sched_switches,sched_timer_events,sched_yield_events,sched_deadline;
static int sched_active;
static __attribute__((noreturn)) void sched_fail(void){__asm__ volatile("cli":::"memory");check("SCHEDULER INVARIANT",0);for(;;)__asm__ volatile("hlt");}
static int task_guard(unsigned id){
    const unsigned char *p=tasks[id].stack;
    return id==0||(p&&*(const uint64_t *)p==(TASK_CANARY|id)&&*(const uint64_t *)(p+TASK_STACK-8)==(TASK_CANARY|id));
}
static int frame_ok(unsigned id,const struct sched_frame *f){
    if(!f||f->cs!=8||!(f->rflags&2)||!vmm_canonical(f->rip)||!vmm_canonical(f->rsp))return 0;
    if(!id)return 1;
    uintptr_t lo=(uintptr_t)tasks[id].stack,hi=lo+TASK_STACK-16,a=(uintptr_t)f;
    return task_guard(id)&&a>=lo+256&&a<=hi-sizeof(*f)&&f->rsp>=lo+256&&f->rsp<hi;
}
struct sched_frame *sched_dispatch(struct sched_frame *f){
    unsigned id=task_current;
    if(!sched_active||id>=SCHED_TASKS||(irq_flags()&0x600)||!frame_ok(id,f)||(task_state[id]!=SCHED_RUNNING&&task_state[id]!=SCHED_DONE))sched_fail();
    if(f->vector==65){
        if(task_state[id]!=SCHED_RUNNING)sched_fail();
        irq_dispatch(65);tasks[id].ticks++;sched_timer_events++;
    }else if(f->vector==67){tasks[id].yields++;sched_yield_events++;}else sched_fail();
    tasks[id].frame=f;
    if(task_state[id]==SCHED_RUNNING)task_state[id]=SCHED_READY;
    unsigned next=sched_pick(task_state,id);
    if(next>=SCHED_TASKS||!frame_ok(next,tasks[next].frame))sched_fail();
    task_state[next]=SCHED_RUNNING;task_current=next;if(next!=id)sched_switches++;
    return tasks[next].frame;
}
static inline void sched_yield(void){__asm__ volatile("int $0x43":::"memory","cc");}
static __attribute__((noreturn)) void sched_worker(uint64_t arg){
    unsigned id=(unsigned)arg;
    volatile uint64_t local=TASK_CANARY|id;
    if(id==0||id>=SCHED_TASKS||task_current!=id||!(irq_flags()&0x200))sched_fail();
    /* No voluntary yield until four genuine timer preemptions occurred. */
    while(tasks[id].ticks<4){
        tasks[id].work++;
        if((tasks[id].work&4095)==0&&(irq_tsc()>sched_deadline||local!=(TASK_CANARY|id)||task_current!=id))sched_fail();
    }
    for(unsigned i=0;i<8;i++){
        sched_yield();
        if(local!=(TASK_CANARY|id)||task_current!=id||!(irq_flags()&0x200))sched_fail();
    }
    __asm__ volatile("cli":::"memory");task_state[id]=SCHED_DONE;sched_yield();sched_fail();
    for(;;)__asm__ volatile("hlt");
}
static void scheduler_run(void){
    check("M15.1 SCHEDULER CONTEXT: PASS",!(irq_flags()&0x200)&&vmm_counts(&vmm_active));
    heap_open();task_current=0;task_state[0]=SCHED_RUNNING;
    for(unsigned id=1;id<SCHED_TASKS;id++){
        unsigned char *p=kmalloc(TASK_STACK);check("SCHED STACK ALLOC",p!=NULL);tasks[id].stack=p;
        *(uint64_t *)p=TASK_CANARY|id;*(uint64_t *)(p+TASK_STACK-8)=TASK_CANARY|id;
        uint64_t rsp=(uint64_t)(uintptr_t)(p+TASK_STACK-24);
        *(uint64_t *)(uintptr_t)rsp=0; /* never return from worker */
        struct sched_frame *f=(struct sched_frame *)(uintptr_t)(rsp-sizeof(*f));
        check("SCHED FRAME INIT",sched_frame_init(f,(uint64_t)(uintptr_t)sched_worker,rsp,id));
        tasks[id].frame=f;task_state[id]=SCHED_READY;check("SCHED FRAME VALID",frame_ok(id,f));
    }
    check("M15.2 TASK STACKS: PASS",1);check("M15.3 TASK CONTEXT: PASS",1);
    check("M15.4 ROUND ROBIN: PASS",sched_pick(task_state,0)==1);
    /* M12 masked the timer and detached its MMIO aliases. Reuse the same
     * calibrated timer and temporary private branch; heap uses HHDM only. */
    irq_map_init();irq_map_page(0,services_lapic_pa,0x1a);irq_lapic=(volatile uint32_t *)(uintptr_t)irq_window;
    struct irq_gate saved_timer=irq_idt[65],saved_yield=irq_idt[67];
    irq_gate_set(65,sched_timer_entry);irq_gate_set(67,sched_yield_entry);
    check("M15.5 SCHEDULER GATES: PASS",irq_idt[65].attr==0x8e&&irq_idt[67].attr==0x8e);
    uint64_t ticks_before=irq_apic_ticks;uint64_t start=irq_tsc();sched_deadline=start+UINT64_C(60000000000);
    check("SCHED DEADLINE",sched_deadline>start&&services_lapic_period>=16);
    sched_active=1;irq_lwrite(0x3e0,3);irq_lwrite(0x320,(1u<<17)|65);irq_lwrite(0x380,services_lapic_period);
    while(task_state[1]!=SCHED_DONE||task_state[2]!=SCHED_DONE||task_state[3]!=SCHED_DONE){
        if(irq_tsc()>sched_deadline||irq_bad)sched_fail();
        sched_yield();
        if(task_current!=0||(irq_flags()&0x200))sched_fail();
    }
    irq_lwrite(0x320,1u<<16);irq_lwrite(0x380,0);sched_active=0;
    check("M15.6 TIMER PREEMPTION: PASS",sched_timer_events>=12&&irq_apic_ticks-ticks_before==sched_timer_events&&!irq_bad&&!irq_isr(65));
    for(unsigned id=1;id<SCHED_TASKS;id++){
        check("SCHED TASK FINISHED",task_state[id]==SCHED_DONE&&tasks[id].ticks>=4&&tasks[id].yields==9&&tasks[id].work&&task_guard(id));
        qemu_debugcon_puts("SCHED TASK ");qemu_debugcon_hex64(id);qemu_debugcon_putc(' ');qemu_debugcon_hex64(tasks[id].ticks);qemu_debugcon_putc(' ');qemu_debugcon_hex64(tasks[id].yields);qemu_debugcon_putc(' ');qemu_debugcon_hex64(tasks[id].work);qemu_debugcon_putc('\n');
    }
    number("SCHED TIMER EVENTS: ",sched_timer_events);number("SCHED YIELD EVENTS: ",sched_yield_events);number("SCHED SWITCHES: ",sched_switches);
    check("M15.7 COOPERATIVE YIELD: PASS",sched_yield_events>=28&&sched_switches>=36);
    check("M15.8 TASK EXIT: PASS",task_current==0&&task_state[0]==SCHED_RUNNING);
    irq_idt[65]=saved_timer;irq_idt[67]=saved_yield;
    irq_root[vmm_index(irq_window,4)]=0;for(unsigned i=0;i<3;i++)vmm_flush(NULL,irq_window+i*4096);
    for(unsigned i=3;i;i--)check("SCHED TABLE RELEASE",pmm_free_page(irq_frames[i-1])==PMM_OK);
    irq_pt=NULL;irq_lapic=NULL;irq_ioapic=NULL;
    for(unsigned id=1;id<SCHED_TASKS;id++){check("SCHED STACK RELEASE",kfree(tasks[id].stack));tasks[id].stack=NULL;tasks[id].frame=NULL;}
    heap_close();check("M15.9 SCHEDULER CLEANUP: PASS",vmm_counts(&vmm_active)&&!irq_bad&&!(irq_flags()&0x200));
    log_write(LOG_OK,"Kernel tasks preempted, yielded and exited");check("M15.10 SCHEDULER INTEGRATION: PASS",1);qemu_debugcon_puts("M15 COMPLETE\n");
}
