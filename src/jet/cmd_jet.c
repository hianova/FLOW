#include "../cmd_dispatch.h"
#include "../flowy.h"
#include "../flowy_cli.h"
#include "../flow_jet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void cmd_jet_print_usage(FILE *out) { fprintf(out, "Usage: flowy jet <inspect|sim|phase-portrait|learn> <file.fjet> [options...]\n"); }
static int load_jet_or_err(int argc, char **argv, const char *usage, FlowJet *jet_out) { if (argc < 2) { fprintf(stderr, "usage: %s\n", usage); return 0; }
    if (!flow_jet_read_file(argv[1], jet_out)) { fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", argv[1]); return 0; } return 1; }
int cmd_jet_run(int argc, char **argv) { if (argc < 1 || !strcmp(argv[0], "-h") || !strcmp(argv[0], "--help") || !strcmp(argv[0], "help")) {
        cmd_jet_print_usage(argc < 1 ? stderr : stdout); return argc < 1 ? EXIT_FAILURE : EXIT_SUCCESS; } const char *act = argv[0]; FlowJet jet;
    if (!strcmp(act, "inspect") || !strcmp(act, "show")) { if (!load_jet_or_err(argc, argv, "flowy jet inspect <file.fjet>", &jet)) return EXIT_FAILURE;
        flowy_print_jet_inspection(&jet, stdout); return EXIT_SUCCESS; }
    if (!strcmp(act, "sim") || !strcmp(act, "simulate") || !strcmp(act, "phase-portrait") || !strcmp(act, "portrait")) {
        int is_pp = (!strcmp(act, "phase-portrait") || !strcmp(act, "portrait"));
        if (!load_jet_or_err(argc, argv, "flowy jet sim <file.fjet> [--steps N] [--dt D]", &jet)) return EXIT_FAILURE;
        int steps = is_pp ? 60 : 20; double dt = is_pp ? 0.02 : 0.01; for (int i = 2; i < argc; ++i) {
            if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]); else if (!strcmp(argv[i], "--dt") && i + 1 < argc) dt = atof(argv[++i]); }
        if (is_pp) flowy_render_phase_portrait(&jet, 0, 0, steps, dt, stdout); else flowy_jet_simulate_run(&jet, steps, dt, stdout); return EXIT_SUCCESS; }
    if (!strcmp(act, "learn") || !strcmp(act, "edmd")) { if (!load_jet_or_err(argc, argv, "flowy jet learn <file.fjet> [--samples N]", &jet)) return EXIT_FAILURE;
        int samples = 50; for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--samples") && i + 1 < argc) samples = atoi(argv[++i]);
        flowy_jet_learn_demo(&jet, samples, stdout); return EXIT_SUCCESS; } cmd_jet_print_usage(stderr); return EXIT_FAILURE; }
