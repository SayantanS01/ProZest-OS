#ifndef PROZEST_CPU_DIAGNOSTICS_H
#define PROZEST_CPU_DIAGNOSTICS_H

/*
 * Capture and display the current CPU
 * descriptor-table state.
 *
 * The graphical console must already
 * be initialized before this is called.
 *
 * This function does not install or
 * modify CPU descriptor tables.
 */
void cpu_diagnostics_show(void);

#endif
