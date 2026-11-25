#ifndef PROZEST_SCHEDULER_H
#define PROZEST_SCHEDULER_H
#include <stdint.h>
#define SCHED_TASKS 4u
enum sched_state { SCHED_READY=1,SCHED_RUNNING,SCHED_DONE };
/* IRQ entry pushes 15 GPRs, vector, and the 64-bit CPU frame. */
struct sched_frame { uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,vector,rip,cs,rflags,rsp,ss; };
unsigned sched_pick(const unsigned state[SCHED_TASKS],unsigned current);
int sched_frame_init(struct sched_frame *,uint64_t rip,uint64_t rsp,uint64_t arg);
#endif
