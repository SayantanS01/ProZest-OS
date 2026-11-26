.text
.global user_enter
.type user_enter,@function
user_enter:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,user_kernel_rsp(%rip)
    pushq $0x2b
    pushq %rsi
    pushq $2
    pushq $0x33
    pushq %rdi
    movq %rdx,%rdi
    xor %eax,%eax
    xor %ebx,%ebx
    xor %ecx,%ecx
    xor %edx,%edx
    xor %esi,%esi
    xor %ebp,%ebp
    xor %r8d,%r8d
    xor %r9d,%r9d
    xor %r10d,%r10d
    xor %r11d,%r11d
    xor %r12d,%r12d
    xor %r13d,%r13d
    xor %r14d,%r14d
    xor %r15d,%r15d
    iretq
.size user_enter,.-user_enter
.global user_syscall_entry,user_pf_entry,user_gp_entry
.type user_syscall_entry,@function
user_syscall_entry:
    pushq $0
    pushq $128
    jmp user_common
.size user_syscall_entry,.-user_syscall_entry
.type user_pf_entry,@function
user_pf_entry:
    pushq $14
    jmp user_common
.size user_pf_entry,.-user_pf_entry
.type user_gp_entry,@function
user_gp_entry:
    pushq $13
    jmp user_common
.size user_gp_entry,.-user_gp_entry
.type user_common,@function
user_common:
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
    movq %rsp,%r12
    movq %rsp,%rdi
    andq $-16,%rsp
    call user_dispatch
    testl %eax,%eax
    jnz user_return_kernel
    movq %r12,%rsp
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
    addq $16,%rsp
    iretq
user_return_kernel:
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movq user_kernel_rsp(%rip),%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size user_common,.-user_common
.bss
.balign 8
user_kernel_rsp: .quad 0
.section .rodata.user,"a",@progbits
.global user_image_start,user_image_end,user_normal,user_read_kernel,user_write_code,user_exec_data,user_read_guard,user_cli
user_image_start:
user_normal:
    pushq %rdi
    xor %eax,%eax
    int $0x80
    popq %rbx
    cmpq %rbx,%rax
    jne user_fail
    movabs $0x401000,%r12
    cmpq %rbx,(%r12)
    jne user_fail
    addq $1,(%r12)
    mov $1,%eax
    lea 8(%r12),%rdi
    mov $7,%esi
    int $0x80
    cmp $7,%rax
    jne user_fail
    mov $1,%eax
    mov $0x401fff,%edi
    mov $2,%esi
    int $0x80
    cmp $-14,%rax
    jne user_fail
    mov $99,%eax
    int $0x80
    cmp $-38,%rax
    jne user_fail
    mov $2,%eax
    xor %edi,%edi
    int $0x80
    jmp user_fail
user_read_kernel:
    movq (%rdi),%rax
    jmp user_fail
user_write_code:
    mov $0x400000,%eax
    movb $0,(%rax)
    jmp user_fail
user_exec_data:
    mov $0x401040,%eax
    call *%rax
    jmp user_fail
user_read_guard:
    mov $0x402000,%eax
    movq (%rax),%rax
    jmp user_fail
user_cli:
    cli
user_fail:
    mov $2,%eax
    mov $1,%edi
    int $0x80
    ud2
user_image_end:
.section .note.GNU-stack,"",@progbits
