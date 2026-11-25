#include "scheduler.h"
#include <stddef.h>
_Static_assert(sizeof(struct sched_frame)==168,"IRQ frame size");
_Static_assert(offsetof(struct sched_frame,vector)==120,"IRQ vector offset");
_Static_assert(offsetof(struct sched_frame,rip)==128,"IRQ RIP offset");
unsigned sched_pick(const unsigned s[SCHED_TASKS],unsigned current){
    if(!s||current>=SCHED_TASKS)return SCHED_TASKS;
    for(unsigned i=1;i<=SCHED_TASKS;i++){unsigned j=(current+i)%SCHED_TASKS;if(s[j]==SCHED_READY)return j;}
    return SCHED_TASKS;
}
static int canonical(uint64_t a){return a>>48==((a&(UINT64_C(1)<<47))?65535u:0u);}
int sched_frame_init(struct sched_frame *f,uint64_t rip,uint64_t rsp,uint64_t arg){
    if(!f||!rip||!rsp||!canonical(rip)||!canonical(rsp)||(rsp&15)!=8)return 0;
    *f=(struct sched_frame){.rdi=arg,.rip=rip,.cs=8,.rflags=0x202,.rsp=rsp,.ss=16};return 1;
}
