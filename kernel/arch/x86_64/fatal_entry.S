/* RDI=vector, RSI=untouched hardware entry RSP, RDX=CR2 for #PF.
 * No error-code normalization on the interrupted stack. */
.text
.macro fatal n
.global exception_stub_\n
.type exception_stub_\n,@function
exception_stub_\n:
.if \n == 14
    mov %cr2,%rdx
.else
    xor %edx,%edx
.endif
    mov %rsp,%rsi
    mov $\n,%edi
    jmp .Ldispatch
.size exception_stub_\n,.-exception_stub_\n
.endm
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fatal \n
.endr
.Ldispatch:
    cld
    and $-16,%rsp
    call hardware_fatal
1:  cli
    hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits
