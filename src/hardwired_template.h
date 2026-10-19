#ifndef FLOW_HARDWIRED_TEMPLATE_H
#define FLOW_HARDWIRED_TEMPLATE_H
#include "smt.h"
#include "polyhedral.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FLOW_HARDWIRED_MAX_PLANES 64
#define FLOW_HARDWIRED_MAX_DIM    8
typedef struct FlowHardwiredPolyhedralTemplate FlowHardwiredPolyhedralTemplate; struct __attribute__((aligned(64))) FlowHardwiredPolyhedralTemplate {
    double coeffs[FLOW_HARDWIRED_MAX_PLANES][FLOW_HARDWIRED_MAX_DIM]; _Atomic double b_constants[FLOW_HARDWIRED_MAX_PLANES]; _Atomic uint64_t active_mask;
    _Atomic double translation[FLOW_HARDWIRED_MAX_DIM]; _Atomic double scale[FLOW_HARDWIRED_MAX_DIM]; size_t dimension, total_planes; _Atomic uint64_t generation;
    uint64_t total_evaluations, total_hot_updates, icache_flushes_avoided; }; typedef struct { bool is_inside; uint64_t violated_mask; double max_violation;
    double projected_x[FLOW_HARDWIRED_MAX_DIM]; } FlowHardwiredEvalResult; FlowHardwiredPolyhedralTemplate *flow_hardwired_create(size_t dimension);
void flow_hardwired_destroy(FlowHardwiredPolyhedralTemplate *tpl); int flow_hardwired_template_init(FlowHardwiredPolyhedralTemplate *tpl, size_t dimension);
int flow_hardwired_template_from_polyhedron(FlowHardwiredPolyhedralTemplate *tpl, const FlowPolyhedron *poly);
int flow_hardwired_set_mask(FlowHardwiredPolyhedralTemplate *tpl, uint64_t new_mask);
int flow_hardwired_set_bound(FlowHardwiredPolyhedralTemplate *tpl, size_t plane_idx, double new_b);
int flow_hardwired_translate(FlowHardwiredPolyhedralTemplate *tpl, const double delta[FLOW_HARDWIRED_MAX_DIM]);
int flow_hardwired_scale(FlowHardwiredPolyhedralTemplate *tpl, const double scale[FLOW_HARDWIRED_MAX_DIM]);
int flow_hardwired_eval(FlowHardwiredPolyhedralTemplate *tpl, const double x[FLOW_HARDWIRED_MAX_DIM], FlowHardwiredEvalResult *res_out);
int flow_hardwired_cascade_eval(FlowHardwiredPolyhedralTemplate *tpl_a, FlowHardwiredPolyhedralTemplate *tpl_b, const double x[FLOW_HARDWIRED_MAX_DIM], FlowHardwiredEvalResult *res_out);
FlowSMTResult flow_hardwired_verify_smt(const FlowHardwiredPolyhedralTemplate *tpl, FlowSMTProofAttestation *proof_out);
#ifdef __cplusplus
}
#endif
#endif /* FLOW_HARDWIRED_TEMPLATE_H */
