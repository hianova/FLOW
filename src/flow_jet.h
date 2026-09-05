#ifndef FLOW_JET_H
#define FLOW_JET_H

#include "flow.h"
#include "bitspace.h"
#include "bitmanifold.h"
#include "flowy_fvec.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Phase Space Jet Bundle (.fjet) & Koopman Operator (flow_jet.h)
 * ============================================================================
 * Mathematical Evolution beyond static .fvec:
 * 1. Jet Bundle Representation J^2(M):
 *    - Generalized coordinates q in R^16 (position / static archetype)
 *    - Conjugate momentum / phase velocity p = \dot{q} in R^16
 *    - Geodesic curvature / acceleration a = \ddot{q} in R^16
 * 2. Mori-Zwanzig Non-Markovian Memory Kernel:
 *    - Resolves coarse-graining projection barrier via memory convolution:
 *      \mathcal{I}_{mem}(t) = \int_0^t K(t - s) p(s) ds
 * 3. Koopman Linear Transfer Operator:
 *    - Infinitesimal generator matrix \mathcal{K} in R^{8x8} mapping
 *      nonlinear manifold observables linearly: g_{t+\tau} = \mathcal{K} g_t
 * 4. Symplectic Hamiltonian Integrator:
 *    - Preserves symplectic 2-form \omega = \sum dq_i \wedge dp_i and total energy H(q, p)
 * ============================================================================
 */

#define FLOW_JET_STANDARD_DIM 16
#define FLOW_JET_DIM 16
#define FLOW_JET_MAX_DIM 64

#define FLOW_JET_STANDARD_KOOPMAN_DIM 8
#define FLOW_JET_KOOPMAN_DIM 8
#define FLOW_JET_MAX_KOOPMAN_DIM 16

#define FLOW_JET_STANDARD_TAPS 8
#define FLOW_JET_MEMORY_KERNEL_TAPS 8
#define FLOW_JET_MAX_TAPS 16

#define FLOW_FJET_MAGIC "FJET_V1"
#define FLOW_FJET_HEADER_SIZE 1024
#define FLOW_FJET_DEFAULT_DIR ".flow/jets"

/* ------------------------------------------------------------------------- */
/* 1. Jet Semantic Metadata Header (1024 Bytes Fixed Padded Block)           */
/* ------------------------------------------------------------------------- */
typedef struct {
    char magic[16];                /* "FJET_V1" */
    char id[64];                   /* e.g., "jet_hft_symplectic_flow" */
    char name[128];                /* Human readable title */
    char origin_hardware[128];     /* e.g., "x86_avx2, L1=64K, Cores=64" */
    char trigger_intent[64];       /* e.g., "HFT_TRADING", "MEMORY_CRITICAL" */
    char category[32];             /* e.g., "JET_BUNDLE", "KOOPMAN_FLOW" */
    char component_id[32];         /* e.g., "bounded_queue", "sharded_hash" */
    char description[256];         /* Dynamical / topological context */
    char smt_signature[64];        /* e.g., "SYMPLECTIC_UNSAT:KOOPMAN_UNSAT" */
    double hamiltonian_energy;     /* Total phase-space energy H(q, p) */
    uint64_t created_at_unix;      /* Epoch timestamp */
    uint32_t vector_dim;           /* Active coordinate dimensions (1..64, default 16) */
    uint32_t payload_size;         /* Size of payload bytes */
    uint32_t confidence_score;     /* Hebbian learning weight */
    uint64_t last_reinforced_unix; /* Timestamp of last activation */
    uint8_t is_auto_promoted;      /* 1 if evolved online */
    uint8_t koopman_dim;           /* Active Koopman observables (1..16, default 8) */
    uint8_t memory_taps;           /* Active memory taps (1..16, default 8) */
    uint8_t reserved_flags;        /* Reserved alignment */
    char content_hash[32];         /* 16-hex content hash string */
    char filepath[256];            /* Filepath when loaded from disk */
} FlowJetHeader;

/* ------------------------------------------------------------------------- */
/* 2. Jet Binary Payload (Phase Space Coordinates & Operators)               */
/* ------------------------------------------------------------------------- */
typedef struct {
    double q[FLOW_JET_MAX_DIM];                     /* Generalized coordinates (512 bytes) */
    double p[FLOW_JET_MAX_DIM];                     /* Conjugate momentum / velocity \dot{q} (512 bytes) */
    double a[FLOW_JET_MAX_DIM];                     /* Geodesic acceleration \ddot{q} (512 bytes) */
    double memory_kernel[FLOW_JET_MAX_TAPS];        /* Mori-Zwanzig decay taps (128 bytes) */
    double koopman_matrix[FLOW_JET_MAX_KOOPMAN_DIM][FLOW_JET_MAX_KOOPMAN_DIM]; /* Koopman transfer matrix (2048 bytes) */
    uint64_t pure_genome;                       /* 64-bit physical architecture genome (8 bytes) */
    uint64_t hard_composite_mask;               /* 64-bit constraint mask (8 bytes) */
    uint64_t soft_composite_bias;               /* 64-bit Boltzmann probability bias (8 bytes) */
    FlowBmf1BitCanvas staged_canvas;            /* 64-byte single cache-line switchboard canvas */
    FlowSMTProofAttestation proof;              /* 4-theorem zero-defect formal status */
    uint32_t crc32;                             /* Checksum (4 bytes) */
} FlowJetPayload;

/* ------------------------------------------------------------------------- */
/* 3. In-Memory Living Jet Object                                            */
/* ------------------------------------------------------------------------- */
typedef struct FlowJet {
    FlowJetHeader header;
    FlowJetPayload payload;
    double memory_integral[FLOW_JET_MAX_DIM];   /* \int_0^t K(t-s) p(s) ds */
    uint64_t last_update_ns;
} FlowJet;

/* ------------------------------------------------------------------------- */
/* 4. Online Streaming Extended Dynamic Mode Decomposition (Streaming EDMD) */
/* ------------------------------------------------------------------------- */
typedef struct {
    double P[FLOW_JET_MAX_KOOPMAN_DIM][FLOW_JET_MAX_KOOPMAN_DIM]; /* Inverse covariance matrix P */
    double A[FLOW_JET_MAX_KOOPMAN_DIM][FLOW_JET_MAX_KOOPMAN_DIM]; /* Discrete transfer operator A = exp(K*dt) */
    double K[FLOW_JET_MAX_KOOPMAN_DIM][FLOW_JET_MAX_KOOPMAN_DIM]; /* Continuous generator K */
    double forgetting_factor;                                     /* RLS forgetting factor \lambda (0.95 ~ 0.999) */
    uint32_t dim;                                                 /* Active Koopman dimension */
    uint64_t update_count;                                        /* Total streaming snapshot pairs processed */
} FlowJetStreamingEDMD;

int flow_jet_edmd_init(FlowJetStreamingEDMD *edmd, uint32_t dim, double forgetting_factor);
int flow_jet_edmd_update(FlowJetStreamingEDMD *edmd, const double x_curr[], const double x_next[], double dt);
int flow_jet_apply_learned_koopman(FlowJet *jet, const FlowJetStreamingEDMD *edmd);
int flow_jet_stream_learn_step(FlowJet *jet, FlowJetStreamingEDMD *edmd, const double pmu_obs[], double dt);

/* ------------------------------------------------------------------------- */
/* 5. PMU Hardware Telemetry & Nonlinear Potential Landscape                 */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint32_t dim;
    double q_equilibrium[FLOW_JET_MAX_DIM]; /* Nominal baseline equilibrium coordinates q* */
    double omega[FLOW_JET_MAX_DIM];         /* Harmonic frequency / stiffness */
    double q_saturation[FLOW_JET_MAX_DIM];  /* Physical hardware capacity barrier thresholds */
    double barrier_mu;                      /* Hyperbolic barrier strength parameter */
    double moreau_low[FLOW_JET_MAX_DIM];    /* Moreau convex set lower boundary */
    double moreau_high[FLOW_JET_MAX_DIM];   /* Moreau convex set upper boundary */
    double moreau_kappa;                    /* Non-smooth normal cone restoring force scale */
} FlowJetPotentialLandscape;

int flow_jet_potential_init_default(FlowJetPotentialLandscape *landscape, uint32_t dim);
int flow_jet_potential_eval_gradient(const FlowJetPotentialLandscape *landscape, const double q[], double grad_out[]);
int flow_jet_symplectic_step_with_potential(FlowJet *jet, const FlowJetPotentialLandscape *landscape, double dt);

/* ------------------------------------------------------------------------- */
/* 6. Core Phase Space & Symplectic APIs                                     */
/* ------------------------------------------------------------------------- */

/* Initialize a Jet with default 16-D coordinates and canonical Koopman generator */
int flow_jet_init(FlowJet *jet, const char *id, const char *name);

/* Initialize a Jet with extended/flexible dimensions up to 64 */
int flow_jet_init_extended(FlowJet *jet, const char *id, const char *name,
                           uint32_t dim, uint32_t koopman_dim, uint32_t taps);

/* Calculate total Hamiltonian energy: H(q, p) = 0.5 * |p|^2 + V(q) */
double flow_jet_hamiltonian(const FlowJet *jet);

/* Symplectic Leapfrog / Velocity Verlet step preserving symplectic form dq \wedge dp */
int flow_jet_symplectic_step(FlowJet *jet, double dt);

/* Koopman linear observable prediction: g_{t+dt} = exp(K * dt) * g_t */
int flow_jet_koopman_predict(const FlowJet *jet, double dt, double *observable_out);

/* Mori-Zwanzig memory convolution update */
int flow_jet_mori_zwanzig_step(FlowJet *jet, double dt);

/* Phase space metric distance accounting for both position and conjugate momentum */
double flow_jet_phase_distance(const FlowJet *a, const FlowJet *b);

/* ------------------------------------------------------------------------- */
/* 7. Serialization & Conversion APIs                                        */
/* ------------------------------------------------------------------------- */

uint32_t flow_jet_crc32(const void *data, size_t length);

int flow_jet_write_file(const FlowJet *jet, const char *filepath);
int flow_jet_read_file(const char *filepath, FlowJet *jet_out);

/* Seamless bidirectional conversion between .fvec and .fjet */
int flow_jet_from_fvec(const FlowVecHeader *hdr, const FlowVecPayload *payload, FlowJet *jet_out);
int flow_jet_to_fvec(const FlowJet *jet, FlowVecHeader *hdr_out, FlowVecPayload *payload_out);

/* ------------------------------------------------------------------------- */
/* 8. Formal SMT Verification of Symplectic Safety & Koopman Stability       */
/* ------------------------------------------------------------------------- */
FlowSMTResult flow_jet_verify_symplectic_soundness_smt(const FlowJet *jet,
                                                       FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_JET_H */
