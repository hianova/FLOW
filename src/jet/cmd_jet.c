/* jet/cmd_jet.c - CLI handler for 'flowy jet' subcommands */
#include "../cmd_dispatch.h"
#include "../flowy.h"
#include "../flowy_cli.h"
#include "../flow_jet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cmd_jet_print_usage(FILE *out) {
    fprintf(out, "Usage: flowy jet <subcommand> [options...]\n\nSubcommands:\n"
                 "  inspect <file.fjet>                  Display phase coordinates, spectrum & SMT proof\n"
                 "  sim <file.fjet> [--steps N] [--dt D] Symplectic orbit leapfrog simulation\n"
                 "  phase-portrait <file.fjet> [options] ASCII terminal phase space trajectory plot\n"
                 "  learn <file.fjet> [--samples N]      Online Streaming EDMD assimilation\n"
                 "  dtc <file.fjet> [options]            Discrete Time Crystal oscillation simulation\n"
                 "  dead-reckon <file.fjet> [options]    CXL dead-reckoning bandwidth reduction simulation\n"
                 "  lob <file.fjet> [--ticks N]          LOB phase-space liquidity hydrodynamics\n"
                 "  impact <file.fjet> [--ticks N]       Robot reflex non-smooth symplectic impact\n"
                 "  geodesic <file.fjet> [--tokens N]    Neuro-bridge latent geodesic pre-play @ 10kHz\n");
}

static int load_jet_or_err(int argc, char **argv, const char *usage, FlowJet *jet_out) {
    if (argc < 2) { fprintf(stderr, "usage: %s\n", usage); return 0; }
    if (!flow_jet_read_file(argv[1], jet_out)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", argv[1]);
        return 0;
    }
    return 1;
}

int cmd_jet_run(int argc, char **argv) {
    if (argc < 1 || !strcmp(argv[0], "-h") || !strcmp(argv[0], "--help") || !strcmp(argv[0], "help")) {
        cmd_jet_print_usage(argc < 1 ? stderr : stdout);
        return argc < 1 ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    const char *act = argv[0];
    FlowJet jet;

    if (!strcmp(act, "inspect") || !strcmp(act, "show")) {
        if (!load_jet_or_err(argc, argv, "flowy jet inspect <file.fjet>", &jet)) return EXIT_FAILURE;
        flowy_print_jet_inspection(&jet, stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(act, "sim") || !strcmp(act, "simulate") || !strcmp(act, "phase-portrait") || !strcmp(act, "portrait")) {
        int is_pp = (!strcmp(act, "phase-portrait") || !strcmp(act, "portrait"));
        if (!load_jet_or_err(argc, argv, is_pp ? "flowy jet phase-portrait <file.fjet> [--steps N] [--dt D]" : "flowy jet sim <file.fjet> [--steps N] [--dt D]", &jet))
            return EXIT_FAILURE;
        int steps = is_pp ? 60 : 20; double dt = is_pp ? 0.02 : 0.01;
        for (int i = 2; i < argc; ++i) {
            if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--dt") && i + 1 < argc) dt = atof(argv[++i]);
        }
        if (is_pp) flowy_render_phase_portrait(&jet, 0, 0, steps, dt, stdout);
        else flowy_jet_simulate_run(&jet, steps, dt, stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(act, "learn") || !strcmp(act, "edmd")) {
        if (!load_jet_or_err(argc, argv, "flowy jet learn <file.fjet> [--samples N]", &jet)) return EXIT_FAILURE;
        int samples = 50;
        for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--samples") && i + 1 < argc) samples = atoi(argv[++i]);
        flowy_jet_learn_demo(&jet, samples, stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(act, "dtc") || !strcmp(act, "time-crystal")) {
        if (!load_jet_or_err(argc, argv, "flowy jet dtc <file.fjet> [--cycles N] [--period T] [--imperfection E] [--subharmonic 2|4|8] [--mask 0xM]", &jet))
            return EXIT_FAILURE;
        uint32_t cycles = 24, sub = 2; double period_T = 0.02, imp = 0.05; uint64_t mask = ~0ULL;
        for (int i = 2; i < argc; ++i) {
            if (!strcmp(argv[i], "--cycles") && i + 1 < argc) cycles = (uint32_t)atoi(argv[++i]);
            else if (!strcmp(argv[i], "--period") && i + 1 < argc) period_T = atof(argv[++i]);
            else if (!strcmp(argv[i], "--imperfection") && i + 1 < argc) imp = atof(argv[++i]);
            else if ((!strcmp(argv[i], "--subharmonic") || !strcmp(argv[i], "--order")) && i + 1 < argc) sub = (uint32_t)atoi(argv[++i]);
            else if (!strcmp(argv[i], "--mask") && i + 1 < argc) mask = strtoull(argv[++i], NULL, 0);
        }
        flowy_jet_dtc_simulate_order(&jet, cycles, period_T, imp, sub, mask, stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(act, "dead-reckon") || !strcmp(act, "reckon")) {
        if (!load_jet_or_err(argc, argv, "flowy jet dead-reckon <file.fjet> [--ticks N] [--threshold EPS]", &jet)) return EXIT_FAILURE;
        uint32_t ticks = 50; double thresh = 0.08;
        for (int i = 2; i < argc; ++i) {
            if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = (uint32_t)atoi(argv[++i]);
            else if (!strcmp(argv[i], "--threshold") && i + 1 < argc) thresh = atof(argv[++i]);
        }
        flowy_jet_dead_reckon_demo(&jet, ticks, thresh, stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(act, "lob") || !strcmp(act, "hydro") || !strcmp(act, "impact")) {
        int is_imp = !strcmp(act, "impact");
        if (!load_jet_or_err(argc, argv, is_imp ? "flowy jet impact <file.fjet> [--ticks N]" : "flowy jet lob <file.fjet> [--ticks N]", &jet))
            return EXIT_FAILURE;
        uint32_t ticks = 50;
        for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = (uint32_t)atoi(argv[++i]);
        if (is_imp) flowy_jet_impact_demo(&jet, ticks, stdout);
        else flowy_jet_lob_demo(&jet, ticks, stdout);
        return EXIT_SUCCESS;
    }
    if (!strcmp(act, "geodesic") || !strcmp(act, "preplay")) {
        if (!load_jet_or_err(argc, argv, "flowy jet geodesic <file.fjet> [--tokens N]", &jet)) return EXIT_FAILURE;
        uint32_t tokens = 5;
        for (int i = 2; i < argc; ++i) if (!strcmp(argv[i], "--tokens") && i + 1 < argc) tokens = (uint32_t)atoi(argv[++i]);
        flowy_jet_geodesic_demo(&jet, tokens, stdout);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Unknown jet action: %s\n", act);
    cmd_jet_print_usage(stderr);
    return EXIT_FAILURE;
}
