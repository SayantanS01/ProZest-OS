/*
 * ProZest OS - x86-64 Exception Entry Stubs
 *
 * Intel syntax is not used here.
 * GNU assembler AT&T syntax is used.
 *
 * These handlers are fatal-only:
 * the future exception_dispatch() function
 * must never return.
 */

.section .text

.extern exception_dispatch

/*
 * Exceptions WITHOUT a CPU-pushed error code.
 *
 * C interface:
 *   exception_dispatch(vector, error_code)
 *
 * System V AMD64 calling convention:
 *   RDI = first argument
 *   RSI = second argument
 */
.macro EXCEPTION_NO_ERROR vector
    .global exception_stub_\vector
    .type exception_stub_\vector, @function

exception_stub_\vector:
    cli
    cld

    /* First argument: exception vector. */
    movq $\vector, %rdi

    /* Second argument: no hardware error code. */
    xorq %rsi, %rsi

    /*
     * Prepare a 16-byte-aligned stack
     * before calling the C dispatcher.
     *
     * We do not restore RSP because
     * the dispatcher must not return.
     */
    andq $-16, %rsp

    call exception_dispatch

    /* Safety fallback if C returns unexpectedly. */
1:
    cli
    hlt
    jmp 1b

    .size exception_stub_\vector, .-exception_stub_\vector
.endm

/*
 * Exceptions WITH a CPU-pushed error code.
 *
 * On entry:
 *   [RSP] = hardware error code
 */
.macro EXCEPTION_WITH_ERROR vector
    .global exception_stub_\vector
    .type exception_stub_\vector, @function

exception_stub_\vector:
    cli
    cld

    /* First argument: exception vector. */
    movq $\vector, %rdi

    /* Second argument: CPU-provided error code. */
    movq (%rsp), %rsi

    /* Align stack before C call. */
    andq $-16, %rsp

    call exception_dispatch

    /* Safety fallback if C returns unexpectedly. */
1:
    cli
    hlt
    jmp 1b

    .size exception_stub_\vector, .-exception_stub_\vector
.endm

/*
 * Vectors 0-31: processor exception entry points.
 * Reserved vectors receive defensive stubs too.
 *
 * These stubs are NOT installed into the IDT yet.
 */

/* Vector 0: Division error. */
EXCEPTION_NO_ERROR 0

/* Vector 1: Debug. */
EXCEPTION_NO_ERROR 1

/* Vector 2: Non-maskable interrupt. */
EXCEPTION_NO_ERROR 2

/* Vector 3: Breakpoint. */
EXCEPTION_NO_ERROR 3

/* Vector 4: Overflow. */
EXCEPTION_NO_ERROR 4

/* Vector 5: BOUND range exceeded. */
EXCEPTION_NO_ERROR 5

/* Vector 6: Invalid opcode. */
EXCEPTION_NO_ERROR 6

/* Vector 7: Device not available. */
EXCEPTION_NO_ERROR 7

/* Vector 8: Double fault. */
EXCEPTION_WITH_ERROR 8

/* Vector 9: Reserved. */
EXCEPTION_NO_ERROR 9

/* Vector 10: Invalid TSS. */
EXCEPTION_WITH_ERROR 10

/* Vector 11: Segment not present. */
EXCEPTION_WITH_ERROR 11

/* Vector 12: Stack-segment fault. */
EXCEPTION_WITH_ERROR 12

/* Vector 13: General protection. */
EXCEPTION_WITH_ERROR 13

/* Vector 14: Page fault. */
EXCEPTION_WITH_ERROR 14

/* Vector 15: Reserved. */
EXCEPTION_NO_ERROR 15

/* Vector 16: x87 floating-point exception. */
EXCEPTION_NO_ERROR 16

/* Vector 17: Alignment check. */
EXCEPTION_WITH_ERROR 17

/* Vector 18: Machine check. */
EXCEPTION_NO_ERROR 18

/* Vector 19: SIMD floating-point exception. */
EXCEPTION_NO_ERROR 19

/* Vector 20: Virtualization exception. */
EXCEPTION_NO_ERROR 20

/* Vector 21: Control protection. */
EXCEPTION_WITH_ERROR 21

/* Vector 22: Reserved. */
EXCEPTION_NO_ERROR 22

/* Vector 23: Reserved. */
EXCEPTION_NO_ERROR 23

/* Vector 24: Reserved. */
EXCEPTION_NO_ERROR 24

/* Vector 25: Reserved. */
EXCEPTION_NO_ERROR 25

/* Vector 26: Reserved. */
EXCEPTION_NO_ERROR 26

/* Vector 27: Reserved. */
EXCEPTION_NO_ERROR 27

/* Vector 28: Hypervisor injection. */
EXCEPTION_NO_ERROR 28

/* Vector 29: VMM communication. */
EXCEPTION_WITH_ERROR 29

/* Vector 30: Security exception. */
EXCEPTION_WITH_ERROR 30

/* Vector 31: Reserved. */
EXCEPTION_NO_ERROR 31

/* Mark the stack as non-executable. */
.section .note.GNU-stack,"",@progbits
