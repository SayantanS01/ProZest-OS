.text
.macro SCHED_ENTRY name,vector
.global \name
.type \name,@function
\name:
    pushq $\vector
    jmp sched_common
.size \name,.-\name
.endm
SCHED_ENTRY sched_timer_entry,65
SCHED_ENTRY sched_yield_entry,67
.type sched_common,@function
sched_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    movq %rsp,%rdi
    andq $-16,%rsp
    call sched_dispatch
    movq %rax,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $8,%rsp
    iretq
.size sched_common,.-sched_common
.section .note.GNU-stack,"",@progbits
