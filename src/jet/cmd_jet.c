#include "../cmd_dispatch.h"
#include "../flowy.h"
#include "../flowy_cli.h"
#include "../flow_jet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void cmd_jet_print_usage(FILE *out) { fprintf(out, "Usage: flowy jet <inspect|sim|phase-portrait|learn> <file.fjet> [options...]\n"); }
int cmd_jet_run(int argc, char **argv) { if (argc < 1 || !strcmp(argv[0], "-h") || !strcmp(argv[0], "--help") || !strcmp(argv[0], "help")) {
        cmd_jet_print_usage(argc < 1 ? stderr : stdout); return argc < 1 ? EXIT_FAILURE : EXIT_SUCCESS; }
    const char *act = argv[0]; FlowJet jet; if (argc < 2 || !flow_jet_read_file(argv[1], &jet)) return EXIT_FAILURE;
    if (!strcmp(act, "inspect") || !strcmp(act, "show")) { flowy_print_jet_inspection(&jet, stdout); return EXIT_SUCCESS; }
    if (!strcmp(act, "sim") || !strcmp(act, "simulate") || !strcmp(act, "phase-portrait") || !strcmp(act, "portrait")) {
        int is_pp = (!strcmp(act, "phase-portrait") || !strcmp(act, "portrait")), steps = is_pp ? 60 : 20; double dt = is_pp ? 0.02 : 0.01;
        for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]); else if (!strcmp(argv[i], "--dt") && i + 1 < argc) dt = atof(argv[++i]);
        if (is_pp) flowy_render_phase_portrait(&jet, 0, 0, steps, dt, stdout); else flowy_jet_simulate_run(&jet, steps, dt, stdout);
        return EXIT_SUCCESS; }
    if (!strcmp(act, "learn") || !strcmp(act, "edmd")) { int samples = 50;
        for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--samples") && i + 1 < argc) samples = atoi(argv[++i]);
        flowy_jet_learn_demo(&jet, samples, stdout); return EXIT_SUCCESS; }
    cmd_jet_print_usage(stderr); return EXIT_FAILURE; }
