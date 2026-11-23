#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "idt.h"
#include "idt_exceptions.h"
#include "gdt.h"
#include "tss.h"

/*
 * Declare the real assembly stub symbols.
 */
#define DECLARE(n) extern void exception_stub_##n(void);
DECLARE(0)  DECLARE(1)  DECLARE(2)  DECLARE(3)
DECLARE(4)  DECLARE(5)  DECLARE(6)  DECLARE(7)
DECLARE(8)  DECLARE(9)  DECLARE(10) DECLARE(11)
DECLARE(12) DECLARE(13) DECLARE(14) DECLARE(15)
DECLARE(16) DECLARE(17) DECLARE(18) DECLARE(19)
DECLARE(20) DECLARE(21) DECLARE(22) DECLARE(23)
DECLARE(24) DECLARE(25) DECLARE(26) DECLARE(27)
DECLARE(28) DECLARE(29) DECLARE(30) DECLARE(31)
#undef DECLARE

static void (*const expected_stubs[32])(void) = {
#define STUB(n) exception_stub_##n,
    STUB(0)  STUB(1)  STUB(2)  STUB(3)
    STUB(4)  STUB(5)  STUB(6)  STUB(7)
    STUB(8)  STUB(9)  STUB(10) STUB(11)
    STUB(12) STUB(13) STUB(14) STUB(15)
    STUB(16) STUB(17) STUB(18) STUB(19)
    STUB(20) STUB(21) STUB(22) STUB(23)
    STUB(24) STUB(25) STUB(26) STUB(27)
    STUB(28) STUB(29) STUB(30) STUB(31)
#undef STUB
};

/*
 * Host-only replacement for exception_dispatch.
 *
 * This satisfies the assembly link dependency.
 * The test must never execute an exception stub.
 */
__attribute__((noreturn))
void exception_dispatch(uint64_t vector, uint64_t error_code)
{
    fprintf(stderr,
            "UNEXPECTED EXCEPTION STUB EXECUTION: %llu, %llu\n",
            (unsigned long long)vector,
            (unsigned long long)error_code);
    abort();
}

static int checks = 0;
static int failures = 0;

static void check(const char *name, int passed)
{
    checks++;
    printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);

    if (!passed) {
        failures++;
    }
}

static uint64_t decode_handler(const struct idt_entry *e)
{
    return (uint64_t)e->offset_low |
           ((uint64_t)e->offset_middle << 16) |
           ((uint64_t)e->offset_high << 32);
}

static uint8_t expected_ist(unsigned int vector)
{
    if (vector == 2)  return TSS_IST_NMI;
    if (vector == 8)  return TSS_IST_DOUBLE_FAULT;
    if (vector == 18) return TSS_IST_MACHINE_CHECK;
    return 0;
}

int main(void)
{
    puts("ProZest OS - Exception IDT Integration");
    puts("======================================");

    static struct idt_table table;
    memset(&table, 0xA5, sizeof(table));

    check("NULL table rejected",
          idt_exceptions_prepare(NULL) == 0);

    check("IDT preparation succeeds",
          idt_exceptions_prepare(&table) == 1);

    unsigned int present_count = 0;
    unsigned int emergency_count = 0;
    unsigned int default_ist_count = 0;

    for (unsigned int i = 0; i < 32; i++) {
        const struct idt_entry *e = &table.entries[i];

        uint64_t expected_address =
            (uint64_t)(uintptr_t)expected_stubs[i];

        uint8_t ist = expected_ist(i);

        int correct =
            decode_handler(e) == expected_address &&
            e->selector == GDT_KERNEL_CODE_SELECTOR &&
            e->ist == ist &&
            e->type_attributes == IDT_INTERRUPT_GATE &&
            e->reserved == 0;

        char label[64];
        snprintf(label, sizeof(label),
                 "Exception vector %02u gate", i);

        check(label, correct);

        if ((e->type_attributes & 0x80u) != 0) {
            present_count++;
        }

        if (e->ist != 0) {
            emergency_count++;
        } else {
            default_ist_count++;
        }
    }

    check("Exactly 32 present exception gates",
          present_count == 32);

    int remaining_clear = 1;

    for (unsigned int i = 32; i < IDT_MAX_ENTRIES; i++) {
        const struct idt_entry *e = &table.entries[i];
        const unsigned char *bytes =
            (const unsigned char *)e;

        for (size_t j = 0; j < sizeof(*e); j++) {
            if (bytes[j] != 0) {
                remaining_clear = 0;
            }
        }
    }

    check("Remaining 224 gates completely zero",
          remaining_clear);

    check("Three emergency IST gates assigned",
          emergency_count == 3);

    check("Other 29 gates use current stack",
          default_ist_count == 29);

    struct idt_table snapshot = table;

    int second_result = idt_exceptions_prepare(&table);

    check("Repeated preparation is consistent",
          second_result == 1 &&
          memcmp(&snapshot, &table, sizeof(table)) == 0);

    puts("--------------------------------------");
    printf("Checks: %d\n", checks);
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
