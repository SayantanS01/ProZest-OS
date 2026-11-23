#ifndef PROZEST_CPU_TABLES_VALIDATION_REPORT_H
#define PROZEST_CPU_TABLES_VALIDATION_REPORT_H

/*
 * Validate the already-prepared CPU tables.
 *
 * Must be called after successful bootstrap
 * GDT/TSS and IDT preparation.
 *
 * The graphical console must be initialized.
 *
 * Returns 1 when both validation layers pass.
 *
 * IMPORTANT:
 * This is structural validation only.
 * It does NOT install CPU descriptor tables.
 */
int cpu_tables_validation_report_run(void);

#endif
