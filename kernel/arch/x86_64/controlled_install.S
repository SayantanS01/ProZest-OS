/* RDI/RSI are bridge operands, RDX/RCX are final operands.
 * Markers use only R8 and AL, both caller-saved. */
.text
.macro marker msg
.pushsection .rodata
1: .asciz "\msg\n"
.popsection
    lea 1b(%rip), %r8
2:  movb (%r8), %al
    test %al,%al
    jz 3f
    outb %al,$0xe9
    inc %r8
    jmp 2b
3:
.endm
.global controlled_install
.type controlled_install,@function
controlled_install:
    cli
    cld
    lgdt (%rdi)
    marker "BRIDGE GDT ACTIVE"
    pushq $8
    lea .Lnew_cs(%rip),%rax
    pushq %rax
    lretq
.Lnew_cs:
    mov $16,%eax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%fs
    mov %ax,%gs
    mov %ax,%ss
    marker "KERNEL SEGMENTS ACTIVE"
    lidt (%rsi)
    marker "SECOND TEMPORARY IDT ACTIVE"
    lgdt (%rdx)
    marker "FINAL GDT ACTIVE"
    mov $24,%eax
    ltr %ax
    marker "TSS ACTIVE"
    lidt (%rcx)
    marker "FINAL IDT ACTIVE"
    ret
.size controlled_install,.-controlled_install
.section .note.GNU-stack,"",@progbits
