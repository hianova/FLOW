#ifndef FLOW_HARDWIRED_TEMPLATE_H
#define FLOW_HARDWIRED_TEMPLATE_H
#include "smt.h"
#include "polyhedral.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#define FLOW_HARDWIRED_MAX_PLANES 64
#define FLOW_HARDWIRED_MAX_DIM    8
typedef struct __attribute__((aligned(64))) FlowHardwiredPolyhedralTemplate {
    double coeffs[FLOW_HARDWIRED_MAX_PLANES][FLOW_HARDWIRED_MAX_DIM]; _Atomic double b_constants[FLOW_HARDWIRED_MAX_PLANES]; _Atomic uint64_t active_mask;
    _Atomic double translation[FLOW_HARDWIRED_MAX_DIM]; _Atomic double scale[FLOW_HARDWIRED_MAX_DIM]; size_t dimension, total_planes; _Atomic uint64_t generation;
    uint64_t total_evaluations, total_hot_updates, icache_flushes_avoided; } FlowHardwiredPolyhedralTemplate;
typedef struct { bool is_inside; uint64_t violated_mask; double max_violation, projected_x[FLOW_HARDWIRED_MAX_DIM]; } FlowHardwiredEvalResult;
int flow_hardwired_template_init(FlowHardwiredPolyhedralTemplate *tpl, size_t dimension);
int flow_hardwired_template_from_polyhedron(FlowHardwiredPolyhedralTemplate *tpl, const FlowPolyhedron *poly);
int flow_hardwired_eval(FlowHardwiredPolyhedralTemplate *tpl, const double x[FLOW_HARDWIRED_MAX_DIM], FlowHardwiredEvalResult *res_out);
#endif
