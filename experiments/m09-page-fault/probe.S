.text
.global trigger_pf
.type trigger_pf,@function
trigger_pf:
    mov %rsp,%rdi
    sub $8,%rsp
    call announce_pf
    add $8,%rsp
.global pf_fault_site
.type pf_fault_site,@function
pf_fault_site:
    mov (%rax),%rax
.size pf_fault_site,.-pf_fault_site
    jmp pf_unexpected_continuation
.size trigger_pf,.-trigger_pf

#ifndef PROZEST_UNIFIED_FATAL
.global exception_stub_14
.type exception_stub_14,@function
exception_stub_14:
    mov %cr2,%rcx
    mov %rsp,%rsi
    cld
    mov $14,%edi
    and $-16,%rsp
    mov %rsp,%rdx
    call dispatch_pf
1:  cli
    hlt
    jmp 1b
.size exception_stub_14,.-exception_stub_14
#endif
.section .note.GNU-stack,"",@progbits
