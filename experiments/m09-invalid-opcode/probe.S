.text
.global trigger_ud
.type trigger_ud,@function
trigger_ud:
    mov %rsp,%rdi
    sub $8,%rsp
    call announce_ud
    add $8,%rsp
.global ud_fault_site
.type ud_fault_site,@function
ud_fault_site:
    ud2
.size ud_fault_site,.-ud_fault_site
    jmp ud_unexpected_continuation
.size trigger_ud,.-trigger_ud

#ifndef PROZEST_UNIFIED_FATAL
.global exception_stub_6
.type exception_stub_6,@function
exception_stub_6:
    mov %rsp,%rsi
    cld
    mov $6,%edi
    and $-16,%rsp
    mov %rsp,%rdx
    call dispatch_ud
1:  cli
    hlt
    jmp 1b
.size exception_stub_6,.-exception_stub_6
#endif
.section .note.GNU-stack,"",@progbits
