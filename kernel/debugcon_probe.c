#include "debugcon_probe.h"
#include "qemu_debugcon.h"

void debugcon_probe(void)
{
    qemu_debugcon_puts(
        "PROZEST DEBUGCON: ONLINE\n"
    );

    qemu_debugcon_puts("Test value: ");

    qemu_debugcon_hex64(
        0x123456789ABCDEF0ULL
    );

    qemu_debugcon_putc('\n');

    qemu_debugcon_puts(
        "DEBUGCON PROBE: COMPLETE\n"
    );
}
