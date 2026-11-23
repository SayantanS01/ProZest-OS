#ifndef PROZEST_CPU_TABLES_PROBE_H
#define PROZEST_CPU_TABLES_PROBE_H

/*
 * Prepare and inspect the bootstrap CPU tables.
 *
 * The graphical console must be initialized.
 *
 * Returns 1 if all checks pass, 0 otherwise.
 *
 * Does not install descriptor tables into the CPU.
 */
int cpu_tables_probe_run(void);

#endif
