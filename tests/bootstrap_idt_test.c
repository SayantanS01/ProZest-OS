#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "bootstrap_idt.h"
#include "gdt.h"
#include "tss.h"

static int checks = 0;
static int failures = 0;

static void check(const char *name, int condition)
{
    checks++;

    printf("[%s] %s\n",
           condition ? "PASS" : "FAIL",
           name);

    if (!condition) {
        failures++;
    }
}

/*
 * The real assembly exception stubs reference
 * exception_dispatch(). This host-only version
 * prevents accidental execution during testing.
 */
__attribute__((noreturn))
void exception_dispatch(uint64_t vector, uint64_t error_code)
{
    fprintf(
        stderr,
        "UNEXPECTED EXCEPTION: %llu, %llu\n",
        (unsigned long long)vector,
        (unsigned long long)error_code
    );

    abort();
}

/* Reconstruct the 64-bit handler address. */
static uint64_t decode_handler(const struct idt_entry *entry)
{
    return (uint64_t)entry->offset_low |
           ((uint64_t)entry->offset_middle << 16) |
           ((uint64_t)entry->offset_high << 32);
}

int main(void)
{
    puts("ProZest OS - Persistent IDT Integration");
    puts("======================================");

    check(
        "Unavailable before initialization",
        bootstrap_idt_get() == NULL
    );

    check(
        "IDT initialization succeeds",
        bootstrap_idt_prepare() == 1
    );

    const struct bootstrap_idt_state *state =
        bootstrap_idt_get();

    check(
        "Prepared state available",
        state != NULL
    );

    if (state == NULL) {
        return 1;
    }

    check(
        "Stable storage address",
        bootstrap_idt_get() == state
    );

    check(
        "Persistent state 16-byte aligned",
        ((uintptr_t)state % 16u) == 0u
    );

    check(
        "IDT size equals 4096 bytes",
        sizeof(state->table) == 4096u
    );

    check(
        "IDTR operand size equals 10 bytes",
        sizeof(state->idtr) == 10u
    );

    check(
        "IDTR limit equals 0x0FFF",
        state->idtr.limit == 0x0FFFu
    );

    check(
        "IDTR base points to persistent table",
        state->idtr.base ==
            (uint64_t)(uintptr_t)&state->table
    );

    unsigned int present = 0;
    unsigned int emergency = 0;
    unsigned int ordinary = 0;
    int gates_valid = 1;

    for (unsigned int vector = 0; vector < 32; vector++) {
        const struct idt_entry *entry =
            &state->table.entries[vector];

        uint8_t expected_ist = 0;

        if (vector == 2) {
            expected_ist = TSS_IST_NMI;
        } else if (vector == 8) {
            expected_ist = TSS_IST_DOUBLE_FAULT;
        } else if (vector == 18) {
            expected_ist = TSS_IST_MACHINE_CHECK;
        }

        if (entry->type_attributes != IDT_INTERRUPT_GATE ||
            entry->selector != GDT_KERNEL_CODE_SELECTOR ||
            entry->ist != expected_ist ||
            entry->reserved != 0 ||
            decode_handler(entry) == 0) {
            gates_valid = 0;
        }

        if (entry->type_attributes & 0x80u) {
            present++;
        }

        if (entry->ist != 0) {
            emergency++;
        } else {
            ordinary++;
        }
    }

    check("All 32 exception gates valid", gates_valid);
    check("Exactly 32 present gates", present == 32);
    check("Three emergency IST gates", emergency == 3);
    check("Remaining 29 use current stack", ordinary == 29);

    int empty_gates_clear = 1;

    for (unsigned int vector = 32;
         vector < IDT_MAX_ENTRIES;
         vector++) {

        const unsigned char *bytes =
            (const unsigned char *)&state->table.entries[vector];

        for (size_t offset = 0;
             offset < sizeof(struct idt_entry);
             offset++) {

            if (bytes[offset] != 0) {
                empty_gates_clear = 0;
            }
        }
    }

    check(
        "Remaining 224 entries are zero",
        empty_gates_clear
    );

    /*
     * Reinitialize only in this host-side test.
     * Never rewrite an IDT that is already live.
     */
    struct bootstrap_idt_state snapshot = *state;

    check(
        "Second initialization succeeds",
        bootstrap_idt_prepare() == 1
    );

    check(
        "Persistent state address unchanged",
        bootstrap_idt_get() == state
    );

    check(
        "Prepared table contents unchanged",
        memcmp(state, &snapshot, sizeof(snapshot)) == 0
    );

    check(
        "Tables remain available",
        bootstrap_idt_get() != NULL
    );

    puts("--------------------------------------");
    printf("Checks: %d\n", checks);
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
