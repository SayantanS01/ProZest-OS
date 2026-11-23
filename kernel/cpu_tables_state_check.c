#include <stdint.h>

#include "cpu_tables_state_check.h"
#include "cpu_tables_probe.h"
#include "cpu_state.h"
#include "console.h"

static int descriptor_registers_equal(
    const struct cpu_state_snapshot *a,
    const struct cpu_state_snapshot *b
)
{
    return
        a->gdtr.base == b->gdtr.base &&
        a->gdtr.limit == b->gdtr.limit &&
        a->idtr.base == b->idtr.base &&
        a->idtr.limit == b->idtr.limit;
}

static int segment_registers_equal(
    const struct cpu_state_snapshot *a,
    const struct cpu_state_snapshot *b
)
{
    return
        a->cs == b->cs &&
        a->ss == b->ss &&
        a->ds == b->ds &&
        a->es == b->es &&
        a->tr == b->tr;
}

int cpu_tables_state_check_run(void)
{
    struct cpu_state_snapshot before = {0};
    struct cpu_state_snapshot after = {0};

    /*
     * Capture bootloader-provided CPU state.
     */
    cpu_state_capture(&before);

    /*
     * Prepare our custom tables in memory.
     */
    int preparation_ok = cpu_tables_probe_run();

    /*
     * Capture active processor state again.
     */
    cpu_state_capture(&after);

    int descriptors_unchanged =
        descriptor_registers_equal(&before, &after);

    int segments_unchanged =
        segment_registers_equal(&before, &after);

    console_newline();

    console_write(
        descriptors_unchanged
        ? "[PASS] Active GDTR/IDTR unchanged"
        : "[FAIL] Active GDTR/IDTR changed"
    );
    console_newline();

    console_write(
        segments_unchanged
        ? "[PASS] Active segments unchanged"
        : "[FAIL] Active segments changed"
    );
    console_newline();

    int success =
        preparation_ok &&
        descriptors_unchanged &&
        segments_unchanged;

    console_write(
        success
        ? "M09 CPU STATE CHECK: PASS"
        : "M09 CPU STATE CHECK: FAIL"
    );
    console_newline();

    return success;
}
