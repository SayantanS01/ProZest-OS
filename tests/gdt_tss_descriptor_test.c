#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "gdt.h"

static int failures = 0;

static void check(const char *name, int passed)
{
    printf(
        "[%s] %s\n",
        passed ? "PASS" : "FAIL",
        name
    );

    if (!passed) {
        failures++;
    }
}

int main(void)
{
    printf("ProZest OS - TSS Descriptor Tests\n");
    printf("================================\n");

    struct gdt_tss_descriptor descriptor;

    /*
     * Test the exact 16-byte encoding.
     *
     * This deliberately uses a test pattern,
     * not an address intended for CPU use.
     */
    const uint8_t expected[16] = {
        0x67, 0x00,
        0xEF, 0xCD,
        0xAB,
        0x89,
        0x00,
        0x89,
        0x67, 0x45, 0x23, 0x01,
        0x00, 0x00, 0x00, 0x00
    };

    memset(&descriptor, 0xA5, sizeof(descriptor));

    int result = gdt_encode_tss_descriptor(
        &descriptor,
        0x0123456789ABCDEFULL,
        103
    );

    check("Descriptor encoding succeeds", result == 1);

    check(
        "Exact 16-byte representation",
        memcmp(&descriptor, expected, 16) == 0
    );

    check(
        "TSS limit = 103",
        descriptor.limit_low == 103
    );

    check(
        "Access byte = 0x89",
        descriptor.access == 0x89
    );

    check(
        "Upper base bits preserved",
        descriptor.base_upper == 0x01234567u
    );

    check(
        "Reserved field cleared",
        descriptor.reserved == 0
    );

    check(
        "Byte granularity selected",
        (descriptor.limit_high_flags & 0xF0u) == 0
    );

    /*
     * Maximum 20-bit limit.
     */
    result = gdt_encode_tss_descriptor(
        &descriptor,
        0,
        0xFFFFFu
    );

    check("Maximum limit accepted", result == 1);

    check(
        "High limit bits preserved",
        descriptor.limit_low == 0xFFFFu &&
        descriptor.limit_high_flags == 0x0Fu
    );

    /*
     * Overflow must fail without modifying
     * the previously valid descriptor.
     */
    struct gdt_tss_descriptor snapshot = descriptor;

    result = gdt_encode_tss_descriptor(
        &descriptor,
        0,
        0x100000u
    );

    check("Oversized limit rejected", result == 0);

    check(
        "Rejected encoding changes nothing",
        memcmp(
            &descriptor,
            &snapshot,
            sizeof(descriptor)
        ) == 0
    );

    /*
     * Verify high-half kernel addresses.
     */
    result = gdt_encode_tss_descriptor(
        &descriptor,
        0xFFFFFFFF80001234ULL,
        103
    );

    check("High-half address accepted", result == 1);

    check(
        "High-half address bits preserved",
        descriptor.base_low == 0x1234u &&
        descriptor.base_middle == 0x00u &&
        descriptor.base_high == 0x80u &&
        descriptor.base_upper == 0xFFFFFFFFu
    );

    check(
        "NULL descriptor rejected",
        gdt_encode_tss_descriptor(
            NULL,
            0,
            103
        ) == 0
    );

    printf("--------------------------------\n");
    printf("Failures: %d\n", failures);

    return failures == 0 ? 0 : 1;
}
