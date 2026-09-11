/* cmd_dispatch.h
 * CLI command dispatch table interface for the flowy tool.
 *
 * Each cmd_*.c module registers handlers via this interface.
 * flowy_main.c only contains the dispatch table and main().
 *
 * Part of FLOW Living Architecture - Method A refactoring.
 */
#ifndef FLOW_CMD_DISPATCH_H
#define FLOW_CMD_DISPATCH_H

#include <stdio.h>

/* ---- topo / orchestrator commands (src/topo/cmd_topo.c) ---- */
int cmd_topo_run(int argc, char **argv);

/* ---- inspect commands (src/inspect/cmd_inspect.c) ---- */
int cmd_inspect_run(int argc, char **argv);

/* ---- fvec / vault / hub commands (src/fvec/cmd_fvec.c) ---- */
int cmd_fvec_run(int argc, char **argv);

/* ---- jet commands (src/jet/cmd_jet.c) ---- */
int cmd_jet_run(int argc, char **argv);

/* ---- usage printers (forwarded from each module) ---- */
void cmd_topo_print_usage(FILE *out);
void cmd_inspect_print_usage(FILE *out);
void cmd_fvec_print_usage(FILE *out);
void cmd_jet_print_usage(FILE *out);

#endif /* FLOW_CMD_DISPATCH_H */
