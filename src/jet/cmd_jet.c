/* jet/cmd_jet.c
 * CLI handler for 'flowy jet' subcommands:
 *   inspect, sim, phase-portrait, learn, dtc, dead-reckon, lob, impact, geodesic.
 *
 * Part of FLOW Living Architecture - Method A refactoring.
 * Extracted from src/flowy_main.c.
 */
#include "../cmd_dispatch.h"
#include "../flowy.h"
#include "../flowy_cli.h"
#include "../flow_jet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cmd_jet_print_usage(FILE *out) {
    fprintf(out, "Usage: flowy jet <subcommand> [options...]\n\n");
    fprintf(out, "Subcommands:\n");
    fprintf(out, "  inspect <file.fjet>                  Display phase coordinates, spectrum & SMT proof\n");
    fprintf(out, "  sim <file.fjet> [--steps N] [--dt D] Symplectic orbit leapfrog simulation\n");
    fprintf(out, "  phase-portrait <file.fjet> [options] ASCII terminal phase space trajectory plot\n");
    fprintf(out, "  learn <file.fjet> [--samples N]      Online Streaming EDMD assimilation\n");
    fprintf(out, "  dtc <file.fjet> [options]            Discrete Time Crystal oscillation simulation\n");
    fprintf(out, "  dead-reckon <file.fjet> [options]    CXL dead-reckoning bandwidth reduction simulation\n");
    fprintf(out, "  lob <file.fjet> [--ticks N]          LOB phase-space liquidity hydrodynamics\n");
    fprintf(out, "  impact <file.fjet> [--ticks N]       Robot reflex non-smooth symplectic impact\n");
    fprintf(out, "  geodesic <file.fjet> [--tokens N]    Neuro-bridge latent geodesic pre-play @ 10kHz\n");
}

int cmd_jet_run(int argc, char **argv) {
    if (argc < 1 || strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "help") == 0) {
        cmd_jet_print_usage(argc < 1 ? stderr : stdout);
        return argc < 1 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    const char *action = argv[0];
    int arg_offset = 1;

    /* 24b. Phase Space Jet Bundles (.fjet) & Koopman Physics (flowy jet) */

/* Subcommand: flowy jet inspect <file.fjet> */
if (strcmp(action, "inspect") == 0 || strcmp(action, "show") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet inspect <file.fjet>\n");
        return EXIT_FAILURE;
    }
    FlowJet jet;
    if (!flow_jet_read_file(argv[arg_offset], &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", argv[arg_offset]);
        return EXIT_FAILURE;
    }
    flowy_print_jet_inspection(&jet, stdout);
    return EXIT_SUCCESS;
}

/* Subcommand: flowy jet sim <file.fjet> [--steps N] [--dt D] */
if (strcmp(action, "sim") == 0 || strcmp(action, "simulate") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet sim <file.fjet> [--steps N] [--dt D]\n");
        return EXIT_FAILURE;
    }
    const char *filepath = argv[arg_offset++];
    int steps = 20;
    double dt = 0.01;
    for (int i = arg_offset; i < argc; ++i) {
        if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) steps = atoi(argv[++i]);
        else if (strcmp(argv[i], "--dt") == 0 && i + 1 < argc) dt = atof(argv[++i]);
    }
    FlowJet jet;
    if (!flow_jet_read_file(filepath, &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", filepath);
        return EXIT_FAILURE;
    }
    flowy_jet_simulate_run(&jet, steps, dt, stdout);
    return EXIT_SUCCESS;
}

/* Subcommand: flowy jet phase-portrait <file.fjet> [--steps N] [--dt D] */
if (strcmp(action, "phase-portrait") == 0 || strcmp(action, "portrait") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet phase-portrait <file.fjet> [--steps N] [--dt D]\n");
        return EXIT_FAILURE;
    }
    const char *filepath = argv[arg_offset++];
    int steps = 60;
    double dt = 0.02;
    for (int i = arg_offset; i < argc; ++i) {
        if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) steps = atoi(argv[++i]);
        else if (strcmp(argv[i], "--dt") == 0 && i + 1 < argc) dt = atof(argv[++i]);
    }
    FlowJet jet;
    if (!flow_jet_read_file(filepath, &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", filepath);
        return EXIT_FAILURE;
    }
    flowy_render_phase_portrait(&jet, 0, 0, steps, dt, stdout);
    return EXIT_SUCCESS;
}

/* Subcommand: flowy jet learn <file.fjet> [--samples N] */
if (strcmp(action, "learn") == 0 || strcmp(action, "edmd") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet learn <file.fjet> [--samples N]\n");
        return EXIT_FAILURE;
    }
    const char *filepath = argv[arg_offset++];
    int samples = 50;
    for (int i = arg_offset; i < argc; ++i) {
        if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc) samples = atoi(argv[++i]);
    }
    FlowJet jet;
    if (!flow_jet_read_file(filepath, &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", filepath);
        return EXIT_FAILURE;
    }
    flowy_jet_learn_demo(&jet, samples, stdout);
    return EXIT_SUCCESS;
}

/* Subcommand: flowy jet dtc <file.fjet> [--cycles N] [--period T] [--imperfection E] */
if (strcmp(action, "dtc") == 0 || strcmp(action, "time-crystal") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet dtc <file.fjet> [--cycles N] [--period T] [--imperfection E]\n");
        return EXIT_FAILURE;
    }
    const char *filepath = argv[arg_offset++];
    uint32_t cycles = 24;
    double period_T = 0.02;
    double imperfection = 0.05;
    for (int i = arg_offset; i < argc; ++i) {
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) cycles = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--period") == 0 && i + 1 < argc) period_T = atof(argv[++i]);
        else if (strcmp(argv[i], "--imperfection") == 0 && i + 1 < argc) imperfection = atof(argv[++i]);
    }
    FlowJet jet;
    if (!flow_jet_read_file(filepath, &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", filepath);
        return EXIT_FAILURE;
    }
    flowy_jet_dtc_simulate(&jet, cycles, period_T, imperfection, stdout);
    return EXIT_SUCCESS;
}

/* Subcommand: flowy jet dead-reckon <file.fjet> [--ticks N] [--threshold EPS] */
if (strcmp(action, "dead-reckon") == 0 || strcmp(action, "reckon") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet dead-reckon <file.fjet> [--ticks N] [--threshold EPS]\n");
        return EXIT_FAILURE;
    }
    const char *filepath = argv[arg_offset++];
    uint32_t ticks = 50;
    double threshold = 0.08;
    for (int i = arg_offset; i < argc; ++i) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) ticks = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) threshold = atof(argv[++i]);
    }
    FlowJet jet;
    if (!flow_jet_read_file(filepath, &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", filepath);
        return EXIT_FAILURE;
    }
    flowy_jet_dead_reckon_demo(&jet, ticks, threshold, stdout);
    return EXIT_SUCCESS;
}

/* Subcommand: flowy jet lob <file.fjet> [--ticks N] */
if (strcmp(action, "lob") == 0 || strcmp(action, "hydro") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet lob <file.fjet> [--ticks N]\n");
        return EXIT_FAILURE;
    }
    const char *filepath = argv[arg_offset++];
    uint32_t ticks = 50;
    for (int i = arg_offset; i < argc; ++i) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) ticks = (uint32_t)atoi(argv[++i]);
    }
    FlowJet jet;
    if (!flow_jet_read_file(filepath, &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", filepath);
        return EXIT_FAILURE;
    }
    flowy_jet_lob_demo(&jet, ticks, stdout);
    return EXIT_SUCCESS;
}

/* Subcommand: flowy jet impact <file.fjet> [--ticks N] */
if (strcmp(action, "impact") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet impact <file.fjet> [--ticks N]\n");
        return EXIT_FAILURE;
    }
    const char *filepath = argv[arg_offset++];
    uint32_t ticks = 50;
    for (int i = arg_offset; i < argc; ++i) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) ticks = (uint32_t)atoi(argv[++i]);
    }
    FlowJet jet;
    if (!flow_jet_read_file(filepath, &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", filepath);
        return EXIT_FAILURE;
    }
    flowy_jet_impact_demo(&jet, ticks, stdout);
    return EXIT_SUCCESS;
}

/* Subcommand: flowy jet geodesic <file.fjet> [--tokens N] */
if (strcmp(action, "geodesic") == 0 || strcmp(action, "preplay") == 0) {
    if (arg_offset >= argc) {
        fprintf(stderr, "usage: flowy jet geodesic <file.fjet> [--tokens N]\n");
        return EXIT_FAILURE;
    }
    const char *filepath = argv[arg_offset++];
    uint32_t tokens = 5;
    for (int i = arg_offset; i < argc; ++i) {
        if (strcmp(argv[i], "--tokens") == 0 && i + 1 < argc) tokens = (uint32_t)atoi(argv[++i]);
    }
    FlowJet jet;
    if (!flow_jet_read_file(filepath, &jet)) {
        fprintf(stderr, "flowy jet: failed to load or verify '%s'\n", filepath);
        return EXIT_FAILURE;
    }
    flowy_jet_geodesic_demo(&jet, tokens, stdout);
    return EXIT_SUCCESS;
}

    fprintf(stderr, "Unknown jet action: %s\n", action);
    cmd_jet_print_usage(stderr);
    return EXIT_FAILURE;
}
