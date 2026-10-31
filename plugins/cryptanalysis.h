#ifndef FLOW_CRYPTANALYSIS_H
#define FLOW_CRYPTANALYSIS_H

#include "flow.h"
#include "plugin.h"
#include "induce.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. Mersenne Twister (MT19937) State Cloning, Untwisting & Seed=42 Recovery
 * ============================================================================ */

#define FLOW_MT19937_N          624U
#define FLOW_MT19937_M          397U
#define FLOW_MT19937_MATRIX_A   0x9908B0DFU
#define FLOW_MT19937_UPPER_MASK 0x80000000U
#define FLOW_MT19937_LOWER_MASK 0x7FFFFFFFU
#define FLOW_MT19937_INIT_MULT  1812433253U
#define FLOW_MT19937_INIT_INV   2520285293U /* Modular inverse of 1812433253 mod 2^32 */

typedef struct {
    uint32_t mt[FLOW_MT19937_N];
    uint32_t index;
} FlowMT19937State;

typedef struct {
    int seed_recovered;
    int state_cloned;
    uint32_t recovered_seed;
    uint32_t observed_offset;
    uint32_t first_untempered_word;
    uint32_t first_tempered_word;
    size_t verified_outputs;
    char audit_summary[256];
} FlowMT19937AuditReport;

void flow_mt19937_seed(FlowMT19937State *st, uint32_t seed);
void flow_mt19937_twist(FlowMT19937State *st);
void flow_mt19937_untwist(uint32_t mt[FLOW_MT19937_N]);
uint32_t flow_mt19937_next(FlowMT19937State *st);

uint32_t flow_mt19937_temper(uint32_t x);
uint32_t flow_mt19937_untemper(uint32_t y);
uint32_t flow_mt19937_init_step(uint32_t prev, uint32_t idx);
uint32_t flow_mt19937_invert_init_step(uint32_t cur, uint32_t idx);

int flow_mt19937_clone_state(const uint32_t observed_outputs[FLOW_MT19937_N],
                             FlowMT19937State *cloned_out);

int flow_mt19937_recover_seed(const uint32_t *observed_outputs, size_t count,
                              FlowMT19937AuditReport *report_out);

/* ============================================================================
 * 2. PCG32 (PCG-XSH-RR) 64-Bit State Reconstruction & GF(2) Linear Solver
 * ============================================================================ */

#define FLOW_PCG32_DEFAULT_MULT 6364136223846793005ULL
#define FLOW_PCG32_DEFAULT_INC  1442695040888963407ULL
#define FLOW_PCG32_INV_MULT     13877824140714322085ULL

uint32_t flow_pcg32_output(uint64_t state);
uint32_t flow_pcg32_step(uint64_t *state, uint64_t inc);

int flow_pcg32_recover_state(const uint32_t *observed_outputs, size_t count, uint64_t inc,
                             uint64_t *recovered_initial_state_out);

int flow_gf2_solve_64x64(const uint64_t matrix_rows[64], uint64_t rhs_vec, uint64_t *solution_out,
                         size_t *rank_out);

/* ============================================================================
 * 3. Certified Quasi-Monte Carlo (QMC) Derandomization Engine
 * ============================================================================ */

typedef struct {
    size_t sample_count;
    uint32_t base_x;
    uint32_t base_y;
    double empirical_star_discrepancy;
    double theoretical_bound;
    double pseudo_random_discrepancy;
    double derandomization_gain_ratio;
    int bound_certified;
} FlowQMCCertificate;

double flow_qmc_radical_inverse(uint64_t index, uint32_t base);
int flow_qmc_generate_2d(size_t count, double *x_out, double *y_out, FlowQMCCertificate *cert_out);

/* ============================================================================
 * 4. Hardware Physical Unclonable Function (PUF) Min-Entropy Certification
 * ============================================================================ */

#define FLOW_PUF_STAGES 64U

typedef struct {
    uint32_t chip_id;
    double stage_delays_ps[FLOW_PUF_STAGES]
                          [2]; /* Differential delay paths (0 vs 1) in picoseconds */
    double thermal_noise_sigma_ps;
} FlowPUFDevice;

typedef struct {
    uint32_t chip_id;
    size_t challenge_count;
    double ones_bias_ratio;      /* P(Response = 1) in [0, 1] */
    double shannon_entropy_bits; /* H_1 in [0, 1] */
    double min_entropy_bits;     /* NIST SP 800-90B H_inf = -log2(max(p, 1-p)) */
    double inter_chip_hd_ratio;  /* Target ~0.50 (uniqueness across silicon dies) */
    double intra_chip_hd_ratio;  /* Target <= 0.05 (reliability under thermal noise) */
    double lipschitz_constant;   /* Max delay sensitivity per bit flip */
    int is_cryptographically_sound;
} FlowPUFCertificate;

int flow_puf_device_init(FlowPUFDevice *dev, uint32_t chip_id, uint64_t silicon_entropy_seed,
                         double thermal_noise_ps);

uint8_t flow_puf_eval_challenge(const FlowPUFDevice *dev, uint64_t challenge,
                                double noise_perturbation_ps, double *final_delay_diff_out);

int flow_puf_certify_device(const FlowPUFDevice *dev, const FlowPUFDevice *reference_dev,
                            const uint64_t *challenges, size_t count, uint8_t *responses_out,
                            FlowPUFCertificate *cert_out);

FlowSMTResult flow_puf_verify_smt(const FlowPUFCertificate *cert,
                                  FlowSMTProofAttestation *proof_out);

const FlowPrimitiveDriver *flow_primitive_puf_driver(void);

/* ============================================================================
 * 5. Formal Lean 4 Cryptanalysis Certificate Synthesizer & Plugin Entry
 * ============================================================================ */

int flow_cryptanalysis_emit_lean_certificate(FILE *out, const char *namespace_name,
                                             const FlowMT19937AuditReport *mt_report,
                                             const FlowQMCCertificate *qmc_cert,
                                             const FlowPUFCertificate *puf_cert);

const FlowPluginDescriptor *flow_cryptanalysis_entry_v1(void);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_CRYPTANALYSIS_H */
