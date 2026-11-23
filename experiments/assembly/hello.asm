global _start

section .rodata
    message: db "Hello from x86-64 assembly!", 10
    message_len equ $ - message

section .text
_start:
    ; Linux write() system call
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel message]
    mov rdx, message_len
    syscall

    ; Linux exit() system call
    mov rax, 60
    xor rdi, rdi
    syscall
