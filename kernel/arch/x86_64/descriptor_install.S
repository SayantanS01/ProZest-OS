/*
 * ProZest OS - CPU Descriptor Installation
 *
 * EXPERIMENTAL: DO NOT CALL YET.
 *
 * System V AMD64 arguments:
 *   RDI = pointer to new GDTR operand
 *   RSI = pointer to new IDTR operand
 *
 * Assumptions:
 *   - Running in x86-64 long mode, ring 0
 *   - GDT, TSS and IDT already initialized
 *   - Descriptor memory remains mapped
 *   - Current kernel stack is usable
 *
 * Kernel selectors:
 *   CS  = 0x08
 *   DS/ES/SS = 0x10
 *   TR  = 0x18
 */

.section .text

.global cpu_descriptors_install
.type cpu_descriptors_install, @function

cpu_descriptors_install:
    /*
     * Disable maskable interrupts.
     * CLD maintains the expected direction flag
     * state for C code.
     */
    cli
    cld

    /*
     * Load the prepared GDT.
     *
     * Existing segment-register descriptor
     * caches have not yet been replaced.
     */
    lgdt (%rdi)

    /*
     * Reload the kernel data selectors.
     */
    movw $0x10, %ax

    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /*
     * Reload CS with a far return.
     *
     * Push new CS and the address of the
     * continuation label, then use LRETQ.
     *
     * Stack pointer is restored by LRETQ.
     */
    pushq $0x08

    leaq .Lnew_cs(%rip), %rax
    pushq %rax

    lretq

.Lnew_cs:
    /*
     * Load the kernel's available 64-bit TSS.
     *
     * LTR marks the descriptor busy.
     */
    movw $0x18, %ax
    ltr %ax

    /*
     * Activate the prepared IDT.
     */
    lidt (%rsi)

    /*
     * Return to the calling C function.
     *
     * Interrupts remain disabled.
     */
    ret

.size cpu_descriptors_install, .-cpu_descriptors_install

.section .note.GNU-stack,"",@progbits
