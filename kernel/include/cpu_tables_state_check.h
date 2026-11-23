#ifndef PROZEST_CPU_TABLES_STATE_CHECK_H
#define PROZEST_CPU_TABLES_STATE_CHECK_H

/*
 * Capture CPU state, prepare kernel tables,
 * then verify active registers did not change.
 *
 * Console must already be initialized.
 *
 * Returns 1 on success.
 * Does not install GDT, TSS or IDT.
 */
int cpu_tables_state_check_run(void);

#endif
