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

#define FLOW_JET_DIM 16
#define FLOW_JET_KOOPMAN_DIM 8
#define FLOW_JET_MEMORY_KERNEL_TAPS 8
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
    uint32_t vector_dim;           /* Always 16 */
    uint32_t payload_size;         /* Size of payload bytes */
    uint32_t confidence_score;     /* Hebbian learning weight */
    uint64_t last_reinforced_unix; /* Timestamp of last activation */
    uint8_t is_auto_promoted;      /* 1 if evolved online */
    char content_hash[32];         /* 16-hex content hash string */
    char filepath[256];            /* Filepath when loaded from disk */
} FlowJetHeader;

/* ------------------------------------------------------------------------- */
/* 2. Jet Binary Payload (Phase Space Coordinates & Operators)               */
/* ------------------------------------------------------------------------- */
typedef struct {
    double q[FLOW_JET_DIM];                     /* Generalized coordinates (128 bytes) */
    double p[FLOW_JET_DIM];                     /* Conjugate momentum / velocity \dot{q} (128 bytes) */
    double a[FLOW_JET_DIM];                     /* Geodesic acceleration \ddot{q} (128 bytes) */
    double memory_kernel[FLOW_JET_MEMORY_KERNEL_TAPS]; /* Mori-Zwanzig decay taps (64 bytes) */
    double koopman_matrix[FLOW_JET_KOOPMAN_DIM][FLOW_JET_KOOPMAN_DIM]; /* Koopman transfer matrix (512 bytes) */
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
    double memory_integral[FLOW_JET_DIM];       /* \int_0^t K(t-s) p(s) ds */
    uint64_t last_update_ns;
} FlowJet;

/* ------------------------------------------------------------------------- */
/* 4. Core Phase Space & Symplectic APIs                                     */
/* ------------------------------------------------------------------------- */

/* Initialize a Jet with default resting momentum and canonical Koopman generator */
int flow_jet_init(FlowJet *jet, const char *id, const char *name);

/* Calculate total Hamiltonian energy: H(q, p) = 0.5 * |p|^2 + V(q) */
double flow_jet_hamiltonian(const FlowJet *jet);

/* Symplectic Leapfrog / Velocity Verlet step preserving symplectic form dq \wedge dp */
int flow_jet_symplectic_step(FlowJet *jet, double dt);

/* Koopman linear observable prediction: g_{t+dt} = exp(K * dt) * g_t */
int flow_jet_koopman_predict(const FlowJet *jet, double dt, double observable_out[FLOW_JET_KOOPMAN_DIM]);

/* Mori-Zwanzig memory convolution update */
int flow_jet_mori_zwanzig_step(FlowJet *jet, double dt);

/* Phase space metric distance accounting for both position and conjugate momentum */
double flow_jet_phase_distance(const FlowJet *a, const FlowJet *b);

/* ------------------------------------------------------------------------- */
/* 5. Serialization & Conversion APIs                                        */
/* ------------------------------------------------------------------------- */

uint32_t flow_jet_crc32(const void *data, size_t length);

int flow_jet_write_file(const FlowJet *jet, const char *filepath);
int flow_jet_read_file(const char *filepath, FlowJet *jet_out);

/* Seamless bidirectional conversion between .fvec and .fjet */
int flow_jet_from_fvec(const FlowVecHeader *hdr, const FlowVecPayload *payload, FlowJet *jet_out);
int flow_jet_to_fvec(const FlowJet *jet, FlowVecHeader *hdr_out, FlowVecPayload *payload_out);

/* ------------------------------------------------------------------------- */
/* 6. Formal SMT Verification of Symplectic Safety & Koopman Stability       */
/* ------------------------------------------------------------------------- */
FlowSMTResult flow_jet_verify_symplectic_soundness_smt(const FlowJet *jet,
                                                       FlowSMTProofAttestation *proof_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_JET_H */
