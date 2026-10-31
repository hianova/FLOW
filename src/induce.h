#ifndef FLOW_INDUCE_H
#define FLOW_INDUCE_H

#include "flow.h"
#include "flow_jet.h"
#include "cubical_hott.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. Empirical High-Order Moments & Integral Probability Metrics (IPM)
 * ============================================================================ */

typedef struct {
    size_t count;
    double mean;               /* 1st moment: E[X] */
    double variance;           /* 2nd central moment: E[(X - mu)^2] */
    double std_dev;            /* sqrt(variance) */
    double skewness;           /* 3rd standardized moment: gamma_1 */
    double excess_kurtosis;    /* 4th standardized moment - 3: gamma_2 */
    double acf1;               /* Lag-1 autocorrelation */
    double min_val;
    double max_val;
} FlowEmpiricalMoments;

typedef enum {
    FLOW_IPM_POLYNOMIAL_MOMENTS = 0, /* Low-order polynomial test class {x, x^2, x^3, x^4} */
    FLOW_IPM_WASSERSTEIN_1      = 1, /* Lipschitz-1 Earth Mover's Distance */
    FLOW_IPM_KOLMOGOROV_SMIRNOV = 2  /* Supremum indicator distance sup_t |F_1(t) - F_2(t)| */
} FlowIPMClass;

int flow_induce_compute_moments(const double *samples, size_t count, FlowEmpiricalMoments *out);
double flow_induce_ipm_distance(const double *model_samples, size_t model_count,
                                const double *data_samples, size_t data_count,
                                FlowIPMClass ipm_class);

/* ============================================================================
 * 2. Exact Rational Approximation (Q) & Parametric Polynomial Synthesis
 * ============================================================================ */

#define FLOW_INDUCE_MAX_POLY_DEGREE 4

typedef struct {
    int64_t num;               /* Numerator p in Z */
    int64_t den;               /* Denominator q in N+ (q >= 1) */
    double  approx_val;        /* (double)num / (double)den */
    double  approx_error;      /* |target - p/q| */
} FlowRational;

typedef struct {
    size_t degree;
    FlowRational coeffs[FLOW_INDUCE_MAX_POLY_DEGREE + 1]; /* c_0 + c_1 x + ... + c_d x^d */
    FlowRational epsilon_bound;                           /* Formal residual bound |f(x) - f_hat(x)| <= eps */
    FlowRational domain_min;
    FlowRational domain_max;
    FlowRational codomain_min;
    FlowRational codomain_max;
    double max_empirical_residual;
    double r_squared;
    size_t sample_count;
} FlowInducedPolyModel;

FlowRational flow_induce_rationalize(double value, int64_t max_denominator);
int flow_induce_fit_rational_poly(const double *x_samples, const double *y_samples,
                                  size_t count, size_t degree, int64_t max_denominator,
                                  FlowInducedPolyModel *out);

/* ============================================================================
 * 3. Interval Arithmetic & Finite Grid Envelope Reflection (via decide)
 * ============================================================================ */

#define FLOW_INDUCE_MAX_GRID_BUCKETS 16

typedef struct {
    int64_t x_left;
    int64_t x_right;
    int64_t y_min;
    int64_t y_max;
    size_t  sample_hits;
} FlowGridBucket;

typedef struct {
    size_t bucket_count;
    int64_t scale_factor;      /* Fixed-point integer scaling factor (e.g. 1000) */
    int64_t global_x_min;
    int64_t global_x_max;
    int64_t global_y_min;
    int64_t global_y_max;
    int64_t certified_upper_bound;
    int64_t certified_lower_bound;
    FlowGridBucket buckets[FLOW_INDUCE_MAX_GRID_BUCKETS];
    size_t total_samples;
} FlowGridEnvelope;

int flow_induce_build_grid_envelope(const double *x_samples, const double *y_samples,
                                    size_t count, size_t bucket_count, int64_t scale_factor,
                                    FlowGridEnvelope *out);

/* ============================================================================
 * 4. Simulated Method of Moments (SMM) Monte Carlo Inverse Solver
 * ============================================================================ */

typedef enum {
    FLOW_SMM_MERTON_JUMP    = 0, /* High-frequency returns: drift mu, vol sigma, jump lambda, mu_J, sigma_J */
    FLOW_SMM_CIR_REFLECTING = 1  /* Physical wind/energy: dX = a(b - X)dt + sigma*sqrt(X)dW, Feller 2ab >= sigma^2 */
} FlowSMMModelKind;

typedef struct {
    FlowSMMModelKind kind;
    /* Recovered continuous parameters */
    double param_a_or_mu;
    double param_b_or_sigma;
    double param_sigma_or_lambda;
    double param_jump_mean;
    double param_jump_std;
    /* Exact rationalized parameters for Lean 4 certificate */
    FlowRational rat_a_or_mu;
    FlowRational rat_b_or_sigma;
    FlowRational rat_sigma_or_lambda;
    FlowRational rat_jump_mean;
    FlowRational rat_jump_std;
    /* Moment matching diagnostics */
    FlowEmpiricalMoments target_moments;
    FlowEmpiricalMoments simulated_moments;
    double ipm_objective_loss;
    int feller_condition_holds;    /* For CIR: 2 * a * b >= sigma^2 */
    double feller_margin;          /* 2 * a * b - sigma^2 */
    size_t solver_iterations;
} FlowSMMResult;

int flow_induce_simulate_merton_jump(double mu, double sigma, double lambda,
                                     double mu_j, double sigma_j, double dt,
                                     uint64_t seed, double *out_returns, size_t count);

int flow_induce_simulate_cir(double a, double b, double sigma, double x0, double dt,
                             uint64_t seed, double *out_states, size_t count);

int flow_induce_solve_smm(const double *target_samples, size_t count,
                          FlowSMMModelKind kind, double dt, uint64_t crn_seed,
                          FlowSMMResult *out);

/* ============================================================================
 * 5. Algebraic Cryptanalysis & PRNG State / Seed Inversion Engine
 * ============================================================================ */

typedef enum {
    FLOW_PRNG_NONE       = 0,
    FLOW_PRNG_XORSHIFT64 = 1, /* Bijective F_2 matrix: x ^= x<<13; x ^= x>>7; x ^= x<<17 */
    FLOW_PRNG_LCG64      = 2, /* Knuth / PCG multiplier 6364136223846793005ULL + inc */
    FLOW_PRNG_SPLITMIX64 = 3  /* Stafford Mix13 bijective finalizer */
} FlowPRNGFamily;

typedef struct {
    int            detected;
    FlowPRNGFamily family;
    uint64_t       recovered_initial_seed;
    uint64_t       recovered_step_offset;
    uint64_t       first_observed_word;
    uint64_t       second_observed_word;
    size_t         matched_trajectory_len;
    double         scale_alpha;
    double         offset_beta;
    char           evidence_summary[256];
} FlowPRNGAuditResult;

uint64_t flow_induce_invert_xorshift64_step(uint64_t y);
uint64_t flow_induce_invert_splitmix64_stateless(uint64_t z);
int flow_induce_audit_prng_u64(const uint64_t *observed_words, size_t count,
                               uint64_t max_backtrack_steps, FlowPRNGAuditResult *out);
int flow_induce_audit_prng_floats(const double *observed_floats, size_t count,
                                  uint64_t max_seed_search, FlowPRNGAuditResult *out);

/* ============================================================================
 * 6. Automated Zero-GC Lean 4 Specification & Certificate Synthesizers
 * ============================================================================ */

int flow_induce_emit_lean_poly(FILE *out, const char *namespace_name,
                               const FlowInducedPolyModel *model);

int flow_induce_emit_lean_grid(FILE *out, const char *namespace_name,
                               const FlowGridEnvelope *env);

int flow_induce_emit_lean_smm(FILE *out, const char *namespace_name,
                              const FlowSMMResult *smm);

int flow_induce_emit_lean_prng_audit(FILE *out, const char *namespace_name,
                                     const FlowPRNGAuditResult *audit);

int flow_induce_emit_lean_composite(FILE *out, const char *namespace_name,
                                    const FlowInducedPolyModel *poly,
                                    const FlowGridEnvelope *grid,
                                    const FlowSMMResult *smm,
                                    const FlowPRNGAuditResult *audit);

int flow_induce_load_csv_samples(const char *csv_path,
                                 double **x_out, double **y_out,
                                 size_t *count_out, int *is_bivariate_out);

/* ============================================================================
 * 7. White-Box .lean Direct Patching & Formal Verification Gate (flowy patch)
 * ============================================================================ */

typedef struct {
    int                           patch_verified;
    uint64_t                      trigger_boundary_tag;
    FlowBoundaryCertificateReport boundary_cert;
    FlowInducedPolyModel          induced_poly;
    FlowGridEnvelope              induced_grid;
    FlowSMMResult                 induced_smm;
    char                          base_spec_path[256];
    char                          patched_spec_path[256];
    char                          patched_fn_name[64];
    size_t                        base_fn_count;
    size_t                        patched_fn_count;
    size_t                        base_thm_count;
    size_t                        patched_thm_count;
    uint64_t                      pre_sieve_mask;
    uint64_t                      post_sieve_mask;
    char                          diff_summary[1024];
} FlowLeanPatchReport;

int flow_induce_patch_lean_spec(const char *base_lean_path,
                                const char *out_patched_lean_path,
                                uint64_t trigger_boundary_tag,
                                const double *x_samples,
                                const double *y_samples,
                                size_t sample_count,
                                FlowLeanPatchReport *report_out);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_INDUCE_H */
