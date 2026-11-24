/* Fatal entry at CPL0. Hardware must still push a frame on the current
 * stack. Once entered, the handler needs no stack, data, TSS, or C ABI.
 * Emit T followed by the two-digit vector in hex and LF, as in T0E. */
.section .text
.macro emit_digit value
    .if (\value) < 10
        movb $(0x30 + (\value)), %al
    .else
        movb $(0x41 + (\value) - 10), %al
    .endif
    outb %al, $0xe9
.endm

.macro transition_stub vector
    .global transition_fatal_\vector
    .type transition_fatal_\vector, @function
transition_fatal_\vector:
    movb $0x54, %al
    outb %al, $0xe9
    emit_digit ((\vector) >> 4)
    emit_digit ((\vector) & 15)
    movb $10, %al
    outb %al, $0xe9
    jmp .Lhalt
    .size transition_fatal_\vector, .-transition_fatal_\vector
.endm

.irp vector,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    transition_stub \vector
.endr

.Lhalt:
    cli
    hlt
    jmp .Lhalt

.section .rodata
.balign 8
.global transition_fatal_handlers
.type transition_fatal_handlers, @object
transition_fatal_handlers:
.irp vector,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .quad transition_fatal_\vector
.endr
.size transition_fatal_handlers, .-transition_fatal_handlers
.section .note.GNU-stack,"",@progbits
