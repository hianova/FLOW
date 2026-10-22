#ifndef FLOW_GEOMETRIC_AXIOM_H
#define FLOW_GEOMETRIC_AXIOM_H
#include "flow.h"
#include "polyhedral.h"
#include "bitspace.h"
#include "bitmanifold.h"
#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct FlowJet FlowJet;
typedef struct FlowThermalState {
    double temp_c, temp_ambient_c, dtemp_dt, r_thermal, c_thermal, temp_throttle_c, temp_target_c, active_power_w; uint64_t throttle_events; uint8_t is_throttled, reserved[7];
} FlowThermalState;
#define FLOW_AXIOM_DIM 64
#define FLOW_AXIOM_LATTICE_DIM 4
typedef struct FlowUnifiedSection FlowUnifiedSection;
struct __attribute__((aligned(64))) FlowUnifiedSection { double q[FLOW_AXIOM_DIM], p[FLOW_AXIOM_DIM], a[FLOW_AXIOM_DIM];
    double s, transversality_margin; int64_t lattice_idx[FLOW_AXIOM_LATTICE_DIM]; uint32_t optimal_tile_size, optimal_simd_width;
    uint64_t total_lattice_points, bmf_subspace_mask; FlowBmf1BitCanvas bmf_canvas; double curvature_spectrum[FLOW_AXIOM_DIM]; FlowThermalState thermal;
    FlowSMTProofAttestation proof; uint64_t cubical_sieve; uint8_t kan_homotopy_status, omega_classifier_bit; uint32_t crc32; uint8_t is_transversal, is_compact, reserved[16]; };
FlowUnifiedSection *flow_axiom_create(const char *intent); void flow_axiom_destroy(FlowUnifiedSection *sec);
int flow_axiom_init(FlowUnifiedSection *sec, const char *intent); int flow_axiom_eval_transversality(FlowUnifiedSection *sec, const FlowPolyhedron *poly);
int flow_axiom_project_bmf(FlowUnifiedSection *sec); int flow_axiom_step(FlowUnifiedSection *sec, double dt, double p_active_watts);
int flow_axiom_from_jet(FlowUnifiedSection *sec, const FlowJet *jet); int flow_axiom_to_jet(const FlowUnifiedSection *sec, FlowJet *jet_out);
int flow_axiom_eval_cubical_homotopy(FlowUnifiedSection *sec, uint64_t target_path, uint64_t boundary_filter);
FlowSMTResult flow_axiom_verify_smt(const FlowUnifiedSection *sec, FlowSMTProofAttestation *proof_out);
#endif /* FLOW_GEOMETRIC_AXIOM_H */
