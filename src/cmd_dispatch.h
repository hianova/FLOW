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
int cmd_topo_run(int argc, char **argv); int cmd_inspect_run(int argc, char **argv); int cmd_fvec_run(int argc, char **argv); int cmd_jet_run(int argc, char **argv);
void cmd_topo_print_usage(FILE *out); void cmd_inspect_print_usage(FILE *out); void cmd_fvec_print_usage(FILE *out); void cmd_jet_print_usage(FILE *out);
#endif /* FLOW_CMD_DISPATCH_H */
