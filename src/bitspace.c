#include "bitspace.h"
#include "registry.h"
#include "security.h"
#include "smt.h"
#include "adaptive.h"
#include "hardware_telemetry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FNV64_OFFSET UINT64_C(14695981039346656037)
#define FNV64_PRIME  UINT64_C(1099511628211)
static uint64_t hash_bytes(uint64_t hash, const void *ptr, size_t size) { const uint8_t *bytes = (const uint8_t *)ptr;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * FNV64_PRIME; return hash; }
static uint64_t hash_str(uint64_t hash, const char *str) { return str ? hash_bytes(hash, str, strlen(str)) : hash; }
uint64_t flow_compute_contract_hash(const SemanticIR *ir) { uint64_t h = FNV64_OFFSET; if (!ir) return h;
    const char * const strs[] = { ir->flow_name, ir->domain_name, ir->contract_name, ir->output_name, ir->resource_name, ir->capability_name, ir->fallback_policy };
    for (size_t i = 0; i < 7; ++i) h = hash_str(h, strs[i]);
    h = hash_bytes(hash_bytes(hash_bytes(h, &ir->input_max_count, sizeof(ir->input_max_count)), &ir->top_n, sizeof(ir->top_n)), &ir->memory_limit_mb, sizeof(ir->memory_limit_mb));
    const int ints[] = { ir->state_shared, ir->state_read_heavy, ir->state_bounded, ir->flow_parallelizable, ir->fact_ordered, ir->fact_unordered, ir->fact_deterministic, ir->fact_range_proven, ir->fact_size_preserved, ir->fact_mutability_read_only };
    for (size_t i = 0; i < 10; ++i) h = hash_bytes(h, &ints[i], sizeof(int)); h = hash_str(h, ir->project_name);
    for (size_t i = 0; i < ir->imported_module_count; ++i) h = hash_str(h, ir->imported_modules[i]);
    h = hash_bytes(h, &ir->declared_constraint_count, sizeof(ir->declared_constraint_count));
    for (size_t i = 0; i < ir->declared_constraint_count; ++i) h = hash_str(hash_str(hash_str(h, ir->constraints[i].name), ir->constraints[i].operator), ir->constraints[i].value);
    return h; }
uint64_t flow_bitspace_compute_schema_hash(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims) { uint64_t h = flow_compute_contract_hash(ir);
    if (comp) { h = hash_str(hash_str(hash_str(hash_str(h, comp->id), comp->kind), comp->resource), comp->capability);
        h = hash_bytes(hash_bytes(h, &comp->memory_fixed_bytes, sizeof(comp->memory_fixed_bytes)), &comp->memory_bytes_per_capacity, sizeof(comp->memory_bytes_per_capacity));
    } if (dims) { h = hash_bytes(h, &dims->count, sizeof(dims->count)); for (size_t i = 0; i < dims->count; ++i) { const FlowPlanDimension *d = &dims->dimensions[i];
            h = hash_bytes(hash_bytes(hash_bytes(hash_bytes(hash_str(h, d->name), &d->kind, sizeof(d->kind)), &d->min_val, sizeof(d->min_val)), &d->max_val, sizeof(d->max_val)), &d->step, sizeof(d->step)); } } return h; }
static unsigned dimension_bits(const FlowPlanDimension *dim) { if (dim->kind == FLOW_DIM_BOOLEAN) return 1;
    uint64_t step = (dim->kind == FLOW_DIM_EXPONENT || dim->step == 0) ? 1 : dim->step, span = dim->max_val >= dim->min_val ? (dim->max_val - dim->min_val) / step : 0;
    unsigned b = 1; while (((uint64_t)1 << b) <= span && b < 64) ++b; return b; }
static uint64_t decode_dim_val(const FlowPlanDimension *dim, uint64_t raw) {
    if (dim->kind == FLOW_DIM_EXPONENT) { uint64_t exp = dim->min_val + raw; return (uint64_t)1 << (exp > dim->max_val ? dim->max_val : exp); }
    if (dim->kind == FLOW_DIM_BOOLEAN) return (raw & 1) ? 1 : 0; uint64_t val = dim->min_val + raw * (dim->step == 0 ? 1 : dim->step); return val > dim->max_val ? dim->max_val : val; }
static uint64_t encode_dim_raw(const FlowPlanDimension *d, uint64_t val, int val_is_exp) {
    if (d->kind == FLOW_DIM_EXPONENT) { if (!val_is_exp) { uint64_t exp = d->min_val; while (((uint64_t)1 << exp) < val && exp < d->max_val) exp++; val = exp; } return val >= d->min_val ? val - d->min_val : 0; }
    return d->kind == FLOW_DIM_BOOLEAN ? (val & 1) : (val >= d->min_val ? (val - d->min_val) / (d->step == 0 ? 1 : d->step) : 0); }
static int hierarchical_decode(const FlowBitSpace *space, uint64_t genome, FlowPlan *plan) { if (!space || !plan || !space->candidate_count) return 0;
    memset(plan, 0, sizeof(*plan)); size_t cand_idx = 0; uint64_t dim_genome = genome; if (space->selector_bits > 0 && space->candidate_count > 1) {
        uint64_t mask = (space->selector_bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->selector_bits) - 1);
        cand_idx = (size_t)(genome & mask) % space->candidate_count; dim_genome = genome >> space->selector_bits; } const Component *comp = space->candidates[cand_idx];
    const FlowPlanDimensionSet *dims = &space->candidate_dims[cand_idx]; plan->component = comp; plan->dimension_set = *dims; plan->genome = genome;
    plan->bit_count = space->bit_count; plan->contract_hash = space->contract_hash; plan->schema_hash = flow_bitspace_compute_schema_hash(space->ir, comp, dims);
    plan->assignment.count = dims->count; for (size_t i = 0, shift = 0; i < dims->count; ++i) { unsigned bits = dimension_bits(&dims->dimensions[i]);
        uint64_t mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1); plan->assignment.values[i] = decode_dim_val(&dims->dimensions[i], (dim_genome >> shift) & mask); shift += bits; } return 1; }
static int hierarchical_evaluate(const FlowBitSpace *space, const FlowPlan *plan, FlowEvaluation *result) { if (!space || !plan || !result || !plan->component) return 0;
    FlowPlanMetrics m = {0}; if (!flow_component_evaluate(space->ir, plan->component, &plan->assignment, &m)) return 0;
    *result = (FlowEvaluation){ .energy = m.energy, .latency_score = m.latency_score, .throughput_score = m.throughput_score, .memory_bytes = m.memory_bytes, .capacity = m.capacity, .hard_gate_passed = 1 }; return 1; }
static int hierarchical_hard_gate_reason(const FlowBitSpace *space, const FlowPlan *plan, const FlowEvaluation *result, FlowGateFailureReason *reason_out) {
    #define FAIL(r) do { if (reason_out) *reason_out = (r); return 0; } while (0)
    if (!space || !plan || !result || !plan->component) FAIL(FLOW_GATE_FAIL_MUTATION_INVALID); if (!result->capacity) FAIL(FLOW_GATE_FAIL_DIMENSION_BOUND); if (space->ir) {
        if (!component_compatible(space->ir, plan->component)) FAIL(FLOW_GATE_FAIL_THREAD_AFFINITY);
        if (space->ir->top_n > 0 && result->capacity < (size_t)space->ir->top_n) FAIL(FLOW_GATE_FAIL_DIMENSION_BOUND);
        if (space->ir->memory_limit_mb > 0 && result->memory_bytes > (size_t)space->ir->memory_limit_mb * 1024u * 1024u) FAIL(FLOW_GATE_FAIL_MEMORY_LIMIT);
        VerificationReport v_report; if (!flow_component_verify_plan(space->ir, plan->component, &plan->assignment, &v_report) || v_report.status == VERIFIER_COMPILE_ERROR) FAIL(FLOW_GATE_FAIL_VERIFIER_UNPROVEN); }
    #undef FAIL
    if (reason_out) *reason_out = FLOW_GATE_PASS; return 1; }
static int hierarchical_hard_gate(const FlowBitSpace *space, const FlowPlan *plan, const FlowEvaluation *result) { return hierarchical_hard_gate_reason(space, plan, result, NULL); }
int flow_mask_canvas_compose(const SemanticIR *ir, const Component *comp, const FlowPlanDimensionSet *dims, const FlowPMUTelemetry *pmu, FlowMaskCanvas *c) { if (!c) return 0;
    memset(c, 0, sizeof(*c)); c->hard_safety_mask = flow_security_get_safety_mask(ir, comp, dims); c->hard_contract_mask = flow_verifier_get_contract_mask(ir, comp, dims);
    c->hard_resource_mask = flow_verifier_get_resource_mask(ir, comp, dims); c->hard_plugin_mask = flow_component_mutation_mask(ir, comp, dims);
    FlowPolyhedronSystem poly; flow_polyhedron_from_ir(ir, comp, dims, &poly); c->hard_polytope_mask = flow_polyhedron_project_mask(&poly, dims, 64);
    c->hard_composite_mask = c->hard_safety_mask & c->hard_contract_mask & c->hard_resource_mask & c->hard_plugin_mask & c->hard_polytope_mask;
    c->domain_preference_mask = flow_component_preference_mask(ir, comp, dims);
    c->dynamic_telemetry_bias = flow_adaptive_telemetry_bias_from_pmu(pmu, (ir && (ir->state_shared || ir->state_read_heavy)), dims);
    c->soft_composite_bias = (c->domain_preference_mask | c->dynamic_telemetry_bias) & c->hard_composite_mask; return 1; }
void flow_bmf_subspace_init_registry(FlowBmfSubspaceRegistry *reg) { if (!reg) return; memset(reg, 0, sizeof(*reg)); static const FlowBmfSubspace INIT_SUBS[7] = {
        {FLOW_BMF_SUBSPACE_GENERIC, "generic_default", "Standard software execution", {0}, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_CONTRACT_GATE | FLOW_BMF_SW_RESOURCE_QUOTA | FLOW_BMF_SW_DETERMINISM_INVAR, ~0ULL, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_CONTRACT_GATE | FLOW_BMF_SW_RESOURCE_QUOTA | FLOW_BMF_SW_DETERMINISM_INVAR | FLOW_BMF_SW_NUMA_FIRST_TOUCH | FLOW_BMF_SW_SIMD_VECTORIZED},
        {FLOW_BMF_SUBSPACE_SMOOTH_FETCH_LATTE, "smooth_fetch_latte", "Anti-spill tilt clamp", {[0] = 0.85, [1] = 0.65, [2] = 0.20}, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_ANTI_SPILL_TILT | FLOW_BMF_SW_GRIPPER_FORCE_SAFE | FLOW_BMF_SW_STICK_SLIP_MODE, 0xFFFFULL, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_ANTI_SPILL_TILT | FLOW_BMF_SW_GRIPPER_FORCE_SAFE | FLOW_BMF_SW_STICK_SLIP_MODE | FLOW_BMF_SW_POLYTOPE_FEASIBLE},
        {FLOW_BMF_SUBSPACE_AGILE_SPRINT, "agile_sprint", "Dynamic ZMP balance", {[0] = 0.15, [1] = 0.90, [2] = 0.80}, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_ZMP_BALANCE | FLOW_BMF_SW_STICK_SLIP_MODE | FLOW_BMF_SW_IMU_ZERO_SAT, 0xFFFFULL, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_ZMP_BALANCE | FLOW_BMF_SW_STICK_SLIP_MODE | FLOW_BMF_SW_IMU_ZERO_SAT | FLOW_BMF_SW_POLYTOPE_FEASIBLE},
        {FLOW_BMF_SUBSPACE_COLLABORATIVE_HOLD, "collaborative_hold", "Dual-robot sync lock", {[0] = 0.40, [1] = 0.30, [3] = 0.85}, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_COOP_SYNC_LOCK | FLOW_BMF_SW_GRIPPER_FORCE_SAFE, 0xFFFFULL, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_COOP_SYNC_LOCK | FLOW_BMF_SW_GRIPPER_FORCE_SAFE | FLOW_BMF_SW_POLYTOPE_FEASIBLE},
        {FLOW_BMF_SUBSPACE_EMERGENCY_PROTECT, "emergency_protect", "Emergency CAN halt", {[4] = 0.95, [5] = 0.90}, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_EMERGENCY_HALT | FLOW_BMF_SW_IMPACT_DAMPING, 0xFFULL, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_EMERGENCY_HALT | FLOW_BMF_SW_IMPACT_DAMPING},
        {FLOW_BMF_SUBSPACE_HFT_LATENCY, "hft_latency", "Zero-copy HFT", {[6] = 0.90, [7] = 0.85}, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_DETERMINISM_INVAR | FLOW_BMF_SW_NUMA_FIRST_TOUCH | FLOW_BMF_SW_SIMD_VECTORIZED, 0xFFFFFFFFULL, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_DETERMINISM_INVAR | FLOW_BMF_SW_NUMA_FIRST_TOUCH | FLOW_BMF_SW_SIMD_VECTORIZED | FLOW_BMF_SW_QSBR_EPOCH_ADVANCE},
        {FLOW_BMF_SUBSPACE_QUIESCENT_IOT, "quiescent_iot", "Ultra-low thermal", {[8] = 0.90}, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_THERMAL_THROTTLE, 0xFFULL, FLOW_BMF_SW_HARD_SAFETY | FLOW_BMF_SW_THERMAL_THROTTLE}
    }; memcpy(reg->subspaces, INIT_SUBS, sizeof(INIT_SUBS)); reg->subspace_count = 7; reg->is_initialized = 1; }
const FlowBmfSubspace *flow_bmf_subspace_lookup(const FlowBmfSubspaceRegistry *reg, uint32_t subspace_id) { if (!reg) return NULL;
    for (size_t i = 0; i < reg->subspace_count; ++i) if (reg->subspaces[i].subspace_id == subspace_id) return &reg->subspaces[i]; return &reg->subspaces[0]; }
uint32_t flow_bmf_subspace_index_from_features(const FlowBmfSubspaceRegistry *reg, const double *features, size_t dim) {
    if (!reg || !reg->subspace_count || !features || !dim) return FLOW_BMF_SUBSPACE_GENERIC; double best_dist_sq = 1e30; uint32_t best_subspace = FLOW_BMF_SUBSPACE_GENERIC;
    size_t check_dim = (dim < FLOW_BMF_SUBSPACE_FEATURE_DIM) ? dim : FLOW_BMF_SUBSPACE_FEATURE_DIM; for (size_t i = 0; i < reg->subspace_count; ++i) { double dist_sq = 0.0;
        for (size_t d = 0; d < check_dim; ++d) { double diff = features[d] - reg->subspaces[i].centroid[d]; dist_sq += diff * diff; }
        if (dist_sq < best_dist_sq) { best_dist_sq = dist_sq; best_subspace = reg->subspaces[i].subspace_id; } } return best_subspace; }
void flow_bmf_canvas_init_from_subspace(FlowBmf1BitCanvas *canvas, const FlowBmfSubspace *s) {
    if (canvas) flow_bmf_canvas_init(canvas, s ? s->subspace_id : FLOW_BMF_SUBSPACE_GENERIC, s ? s->invariant_mask : FLOW_BMF_SW_HARD_SAFETY, s ? s->malleable_mask : ~0ULL, s ? s->default_switches : FLOW_BMF_SW_HARD_SAFETY); }
int flow_bmf_canvas_adjudicate_smt(FlowBmf1BitCanvas *c) { return c ? (c->is_adjudicated_sound = ((c->switchboard_bits & c->invariant_mask) == c->invariant_mask) ? 1 : 0) : 0; }
void flow_bmf_canvas_to_mask_canvas(const FlowBmf1BitCanvas *c, FlowMaskCanvas *out) {
    if (c && out) *out = (FlowMaskCanvas){ .hard_safety_mask = c->invariant_mask ? c->invariant_mask : ~0ULL, .hard_composite_mask = c->switchboard_bits, .dynamic_telemetry_bias = c->dynamic_bias, .soft_composite_bias = c->dynamic_bias }; }
void flow_bmf_canvas_from_mask_canvas(const FlowMaskCanvas *m, uint32_t subspace_id, FlowBmf1BitCanvas *out) { if (!out) return;
    uint64_t inv = m ? m->hard_safety_mask : FLOW_BMF_SW_HARD_SAFETY; flow_bmf_canvas_init(out, subspace_id, inv, ~0ULL, m ? m->hard_composite_mask : inv); if (m) out->dynamic_bias = m->dynamic_telemetry_bias; }
static uint32_t calc_bits_for_dims(const FlowPlanDimensionSet *dims) { uint32_t total = 0; for (size_t i = 0; i < dims->count; ++i) total += dimension_bits(&dims->dimensions[i]); return total > 0 ? total : 1; }
static int finish_bitspace_init(FlowBitSpace *space, uint32_t max_dim_bits) { size_t comp_count = space->candidate_count; space->selector_bits = 0;
    if (comp_count > 1) { space->selector_bits = 1; while (((size_t)1 << space->selector_bits) < comp_count && space->selector_bits < 16) space->selector_bits++; }
    space->bit_count = space->selector_bits + max_dim_bits; if (!space->bit_count) space->bit_count = 1; if (space->bit_count > 64) space->bit_count = 64;
    uint64_t full_mask = (space->bit_count >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->bit_count) - 1); space->env_mask = full_mask; if (comp_count == 1) {
        space->global_canvas = space->candidate_canvases[0]; space->env_mask &= space->candidate_masks[0]; } else {
        uint64_t union_hard = (space->selector_bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << space->selector_bits) - 1), union_soft = 0;
        for (size_t i = 0; i < comp_count; ++i) { union_hard |= (space->candidate_canvases[i].hard_composite_mask << space->selector_bits);
            union_soft |= (space->candidate_canvases[i].soft_composite_bias << space->selector_bits); } space->global_canvas.hard_composite_mask = union_hard & full_mask;
        space->global_canvas.soft_composite_bias = union_soft & full_mask; space->env_mask &= space->global_canvas.hard_composite_mask; }
    space->schema_hash = flow_bitspace_compute_schema_hash(space->ir, space->candidates[0], &space->candidate_dims[0]);
    space->decode = hierarchical_decode; space->evaluate = hierarchical_evaluate; space->hard_gate = hierarchical_hard_gate; return 1; }
int flow_bitspace_init_single(const SemanticIR *ir, const Component *comp, FlowBitSpace *space) { if (!comp || !space) return 0; memset(space, 0, sizeof(*space));
    space->ir = ir; space->contract_hash = flow_compute_contract_hash(ir); space->candidate_count = 1; space->candidates[0] = comp;
    if (!flow_component_dimensions(ir, comp, &space->candidate_dims[0])) return 0; space->candidate_bits[0] = calc_bits_for_dims(&space->candidate_dims[0]);
    flow_mask_canvas_compose(ir, comp, &space->candidate_dims[0], NULL, &space->candidate_canvases[0]);
    space->candidate_masks[0] = space->candidate_canvases[0].hard_composite_mask; return finish_bitspace_init(space, space->candidate_bits[0]); }
int flow_bitspace_init_for_ir(const SemanticIR *ir, FlowBitSpace *space) { if (!ir || !space) return 0; memset(space, 0, sizeof(*space));
    space->ir = ir; space->contract_hash = flow_compute_contract_hash(ir); size_t comp_count = compatible_component_count(ir); if (!comp_count) return 0;
    if (comp_count > FLOW_BITSPACE_MAX_CANDIDATES) comp_count = FLOW_BITSPACE_MAX_CANDIDATES; space->candidate_count = comp_count; uint32_t max_dim_bits = 0;
    for (size_t i = 0; i < comp_count; ++i) { const Component *c = compatible_component_at(ir, i); space->candidates[i] = c;
        if (!flow_component_dimensions(ir, c, &space->candidate_dims[i])) return 0; space->candidate_bits[i] = calc_bits_for_dims(&space->candidate_dims[i]);
        if (space->candidate_bits[i] > max_dim_bits) max_dim_bits = space->candidate_bits[i];
        flow_mask_canvas_compose(ir, c, &space->candidate_dims[i], NULL, &space->candidate_canvases[i]);
        space->candidate_masks[i] = space->candidate_canvases[i].hard_composite_mask; } return finish_bitspace_init(space, max_dim_bits); }
void flow_genome_init(FlowGenome *g, uint32_t total_bits) { if (!g) return; memset(g, 0, sizeof(*g));
    g->total_bits = (total_bits > 0 && total_bits <= (FLOW_GENOME_MAX_WORDS * 64)) ? total_bits : 64; g->active_words = (g->total_bits + 63) / 64;
    if (!g->active_words) g->active_words = 1; if (g->active_words > FLOW_GENOME_MAX_WORDS) g->active_words = FLOW_GENOME_MAX_WORDS; }
void flow_genome_flip_bit(FlowGenome *g, uint32_t bit_idx) { if (g && bit_idx < g->total_bits && bit_idx / 64 < FLOW_GENOME_MAX_WORDS) g->words[bit_idx / 64] ^= (UINT64_C(1) << (bit_idx % 64)); }
void flow_genome_mutate_1bit(FlowGenome *g, uint64_t *rng_state, uint32_t *mutated_bit_out) { if (!g || !g->total_bits) return;
    uint32_t bit_idx = (uint32_t)(flow_prng_next(rng_state) % (uint64_t)g->total_bits); flow_genome_flip_bit(g, bit_idx); if (mutated_bit_out) *mutated_bit_out = bit_idx; }
uint64_t flow_bitspace_mutate_1bit_superposed(const FlowBitSpace *space, uint64_t genome, const FlowMaskCanvas *canvas, double bias_weight, uint64_t *rng_state, uint32_t *mutated_bit_out) {
    uint32_t bits = (space && space->bit_count > 0) ? (space->bit_count > 64 ? 64 : space->bit_count) : 16;
    uint64_t hard_mask = (canvas && canvas->hard_composite_mask) ? canvas->hard_composite_mask : ((space && space->env_mask) ? space->env_mask : (uint64_t)-1);
    if (space && space->env_mask) hard_mask &= space->env_mask; uint64_t soft_bias = (canvas ? canvas->soft_composite_bias : 0) & hard_mask, rnd = flow_prng_next(rng_state);
    if (bias_weight > 0.0 && soft_bias != 0) { uint32_t soft_indices[64]; size_t soft_count = 0;
        for (uint32_t b = 0; b < bits; ++b) if (soft_bias & (UINT64_C(1) << b)) soft_indices[soft_count++] = b;
        if (soft_count > 0 && (uint32_t)(rnd & 0xFFFF) < (uint32_t)(bias_weight * 65536.0)) { uint32_t chosen_bit = soft_indices[(rnd >> 16) % soft_count];
            if (mutated_bit_out) *mutated_bit_out = chosen_bit; return genome ^ (UINT64_C(1) << chosen_bit); } } uint32_t bit = (uint32_t)((rnd >> 16) % bits);
    if ((UINT64_C(1) << bit) & hard_mask) { if (mutated_bit_out) *mutated_bit_out = bit; return genome ^ (UINT64_C(1) << bit); }
    if (mutated_bit_out) *mutated_bit_out = 0xFFFFFFFF; return genome; }
uint64_t flow_bitspace_mutate_1bit(const FlowBitSpace *space, uint64_t genome, uint64_t *rng_state, uint32_t *mutated_bit_out) {
    FlowMaskCanvas canvas = {.hard_composite_mask = (uint64_t)-1}; return flow_bitspace_mutate_1bit_superposed(space, genome, &canvas, 0.0, rng_state, mutated_bit_out); }
static void pareto_update_bitspace(FlowBitSearchResult *res, const FlowPlan *plan) { if (!res || !plan || !plan->eval.hard_gate_passed) return;
    double lat = plan->eval.latency_score, mem = (double)plan->eval.memory_bytes;
    if (!res->pareto_count) { res->pareto_points[0] = *plan; res->pareto_count = 1; res->best_latency = lat; res->best_memory = mem; return; }
    if (lat < res->best_latency) res->best_latency = lat; if (mem < res->best_memory) res->best_memory = mem; for (size_t i = 0; i < res->pareto_count; ++i) {
        double pi_lat = res->pareto_points[i].eval.latency_score, pi_mem = (double)res->pareto_points[i].eval.memory_bytes;
        if (pi_lat <= lat && pi_mem <= mem && (pi_lat < lat || pi_mem < mem)) return; } size_t j = 0; for (size_t i = 0; i < res->pareto_count; ++i) {
        double pi_lat = res->pareto_points[i].eval.latency_score, pi_mem = (double)res->pareto_points[i].eval.memory_bytes;
        if (!(lat <= pi_lat && mem <= pi_mem && (lat < pi_lat || mem < pi_mem))) res->pareto_points[j++] = res->pareto_points[i]; } res->pareto_count = j;
    if (res->pareto_count < FLOW_PARETO_MAX) res->pareto_points[res->pareto_count++] = *plan; }
uint64_t flow_bitspace_encode(const FlowBitSpace *space, size_t cand_idx, const FlowPlanAssignment *plan) {
    if (!space || !plan || cand_idx >= space->candidate_count) return 0; uint64_t genome = (uint64_t)cand_idx;
    const FlowPlanDimensionSet *dims = &space->candidate_dims[cand_idx]; for (size_t i = 0, shift = space->selector_bits; i < dims->count && i < plan->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i]; unsigned bits = dimension_bits(d); uint64_t mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        genome |= (encode_dim_raw(d, plan->values[i], 0) & mask) << shift; shift += bits; } return genome; }
static uint64_t encode_candidate_heuristic(const FlowBitSpace *space, size_t cand_idx, int is_default) { uint64_t genome = (uint64_t)cand_idx;
    const FlowPlanDimensionSet *dims = &space->candidate_dims[cand_idx]; for (size_t i = 0, shift = space->selector_bits; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i]; unsigned bits = dimension_bits(d); uint64_t val = is_default ? d->default_val : d->min_val;
        int val_is_exp = is_default && (d->kind == FLOW_DIM_EXPONENT); if (space->ir) { if (!strcmp(d->name, "capacity")) {
                size_t req_cap = space->ir->input_max_count > 0 ? (size_t)space->ir->input_max_count : (is_default ? 0 : 1024);
                if (is_default && req_cap > 4096) req_cap = 4096;
                if (is_default && space->ir->top_n > 0 && req_cap < (size_t)space->ir->top_n) req_cap = (size_t)space->ir->top_n; val = req_cap; val_is_exp = 0;
            } else if ((!strcmp(d->name, "threads") || !strcmp(d->name, "shards")) && d->kind == FLOW_DIM_EXPONENT) {
                uint64_t exp = (!strcmp(d->name, "threads") && space->ir->flow_parallelizable) ? 2 : (space->ir->state_shared ? 4 : 0);
                val = exp < d->min_val ? d->min_val : (exp > d->max_val ? d->max_val : exp); val_is_exp = 1; } }
        uint64_t mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1); genome |= (encode_dim_raw(d, val, val_is_exp) & mask) << shift; shift += bits; } return genome; }
uint64_t flow_bitspace_default_genome(const FlowBitSpace *space) { if (!space || !space->candidate_count) return 0; size_t cand_idx = 0; if (space->ir) {
        const Component *heur = select_component(space->ir);
        if (heur) for (size_t c = 0; c < space->candidate_count; ++c) if (space->candidates[c] == heur) { cand_idx = c; break; } }
    return encode_candidate_heuristic(space, cand_idx, 1); }
int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed, int measured, const FlowPlan *seed_plan, FlowBitSearchResult *result_out) {
    if (!space || !result_out || !space->candidate_count) return 0; memset(result_out, 0, sizeof(*result_out));
    result_out->iterations = iterations ? iterations : 1; result_out->seed = seed; result_out->measured = measured; FlowPlan best_plan = {0};
    uint64_t current_genome = seed_plan ? seed_plan->genome : flow_bitspace_default_genome(space); if (space->decode(space, current_genome, &best_plan)) {
        space->evaluate(space, &best_plan, &best_plan.eval); FlowGateFailureReason r = FLOW_GATE_PASS;
        best_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &best_plan, &best_plan.eval, &r); result_out->heatmap.total_mutations++;
        if (best_plan.eval.hard_gate_passed) pareto_update_bitspace(result_out, &best_plan);
        else { best_plan.eval.energy += 1.0e12; result_out->heatmap.total_failures++; result_out->heatmap.failure_counts[r]++; } }
    for (size_t c = 0; c < space->candidate_count; ++c) { FlowPlan cand; if (space->decode(space, encode_candidate_heuristic(space, c, 0), &cand)) {
            space->evaluate(space, &cand, &cand.eval); FlowGateFailureReason cr = FLOW_GATE_PASS;
            cand.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &cand, &cand.eval, &cr); result_out->heatmap.total_mutations++; if (cand.eval.hard_gate_passed) {
                pareto_update_bitspace(result_out, &cand); if (!best_plan.eval.hard_gate_passed || cand.eval.energy < best_plan.eval.energy) best_plan = cand;
            } else { result_out->heatmap.total_failures++; result_out->heatmap.failure_counts[cr]++; } } }
    if (best_plan.eval.hard_gate_passed) pareto_update_bitspace(result_out, &best_plan); result_out->best_plan = best_plan;
    if (result_out->best_latency > 0.0) result_out->latency_regret_percent = ((best_plan.eval.latency_score - result_out->best_latency) / result_out->best_latency) * 100.0;
    if (result_out->best_memory > 0.0) result_out->memory_regret_percent = (((double)best_plan.eval.memory_bytes - result_out->best_memory) / result_out->best_memory) * 100.0;
    return best_plan.eval.hard_gate_passed ? 1 : 0; }
int flow_plan_to_artifact(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, FlowPlanArtifact *art) {
    if (!plan || !art) return 0; memset(art, 0, sizeof(*art)); if (ir) strncpy(art->flow_name, ir->flow_name, sizeof(art->flow_name) - 1); if (plan->component) {
        strncpy(art->component_id, plan->component->id, sizeof(art->component_id) - 1); const FlowPlugin *plugin = flow_component_plugin(plan->component);
        if (plugin) { strncpy(art->module_name, plugin->name, sizeof(art->module_name) - 1); strncpy(art->module_version, plugin->version, sizeof(art->module_version) - 1); }
    } art->contract_hash = plan->contract_hash; art->plan_schema_hash = plan->schema_hash; art->seed = seed; art->bit_count = plan->bit_count; art->genome = plan->genome;
    art->dimensions = plan->dimension_set; art->plan = plan->assignment; art->metrics = plan->eval;
    strncpy(art->verification_status, plan->eval.hard_gate_passed ? "verified" : "rejected", sizeof(art->verification_status) - 1);
    snprintf(art->attestation_msg, sizeof(art->attestation_msg), "schema_hash=%llu genome=0x%016llx energy=%.4f", (unsigned long long)art->plan_schema_hash, (unsigned long long)art->genome, art->metrics.energy);
    double cap_d = (double)(plan->eval.capacity > 0 ? plan->eval.capacity : (ir && ir->input_max_count > 0 ? ir->input_max_count : 64));
    art->jet_omega = fmax(0.1, sqrt(cap_d) / 16.0); art->jet_barrier_mu = 1.0 / cap_d; art->jet_q_saturation = 1.25 * cap_d; return 1; }
int flow_plan_artifact_save(FILE *output, const FlowPlanArtifact *art) { if (!output || !art) return 0;
    fprintf(output, "# Flow Plan Artifact (FLOW_PLAN_V1)\nflow=%s\nmodule=%s\nmodule_version=%s\ncomponent=%s\ncontract_hash=%llu\nplan_schema_hash=%llu\nseed=%u\nbit_count=%u\ngenome=0x%016llx\ndimension_count=%zu\n",
            art->flow_name, art->module_name, art->module_version, art->component_id, (unsigned long long)art->contract_hash, (unsigned long long)art->plan_schema_hash, art->seed, art->bit_count, (unsigned long long)art->genome, art->dimensions.count);
    for (size_t i = 0; i < art->dimensions.count && i < art->plan.count; ++i) { const FlowPlanDimension *d = &art->dimensions.dimensions[i];
        fprintf(output, "dim.%s=%llu\ndim_kind.%s=%d\ndim_min.%s=%llu\ndim_max.%s=%llu\ndim_step.%s=%llu\n", d->name, (unsigned long long)art->plan.values[i], d->name, (int)d->kind, d->name, (unsigned long long)d->min_val, d->name, (unsigned long long)d->max_val, d->name, (unsigned long long)d->step); }
    fprintf(output, "metric.energy=%.6f\nmetric.latency=%.6f\nmetric.throughput=%.6f\nmetric.memory_bytes=%zu\nmetric.capacity=%zu\nmetric.benchmark_ns=%llu\nverification_status=%s\nattestation=%s\njet.omega=%.6f\njet.barrier_mu=%.8f\njet.q_saturation=%.6f\n",
            art->metrics.energy, art->metrics.latency_score, art->metrics.throughput_score, art->metrics.memory_bytes, art->metrics.capacity, (unsigned long long)art->metrics.benchmark_ns, art->verification_status, art->attestation_msg, art->jet_omega, art->jet_barrier_mu, art->jet_q_saturation);
    return ferror(output) == 0; }
static FlowPlanDimension *find_or_add_dim(FlowPlanArtifact *art, const char *dname, int create, size_t *idx_out) {
    for (size_t i = 0; i < art->dimensions.count; ++i) if (!strcmp(art->dimensions.dimensions[i].name, dname)) { if (idx_out) *idx_out = i; return &art->dimensions.dimensions[i]; }
    if (create && art->dimensions.count < 16) { size_t idx = art->dimensions.count++; art->plan.count = art->dimensions.count;
        strncpy(art->dimensions.dimensions[idx].name, dname, sizeof(art->dimensions.dimensions[idx].name) - 1); if (idx_out) *idx_out = idx; return &art->dimensions.dimensions[idx]; } return NULL; }
int flow_plan_artifact_load(FILE *input, FlowPlanArtifact *art) {
    char line[256]; if (!input || !art) return 0; memset(art, 0, sizeof(*art)); while (fgets(line, sizeof(line), input)) { char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0'; char *key = line, *val = eq + 1; val[strcspn(val, "\r\n")] = '\0'; FlowPlanDimension *d; size_t idx = 0;
        if (!strcmp(key, "flow")) strncpy(art->flow_name, val, sizeof(art->flow_name) - 1);
        else if (!strcmp(key, "module")) strncpy(art->module_name, val, sizeof(art->module_name) - 1);
        else if (!strcmp(key, "module_version")) strncpy(art->module_version, val, sizeof(art->module_version) - 1);
        else if (!strcmp(key, "component")) strncpy(art->component_id, val, sizeof(art->component_id) - 1);
        else if (!strcmp(key, "contract_hash")) art->contract_hash = strtoull(val, NULL, 10);
        else if (!strcmp(key, "plan_schema_hash")) art->plan_schema_hash = strtoull(val, NULL, 10);
        else if (!strcmp(key, "seed")) art->seed = (uint32_t)strtoul(val, NULL, 10); else if (!strcmp(key, "bit_count")) art->bit_count = (uint32_t)strtoul(val, NULL, 10);
        else if (!strcmp(key, "genome")) art->genome = strtoull(val, NULL, 16);
        else if (!strncmp(key, "dim.", 4) && find_or_add_dim(art, key + 4, 1, &idx)) art->plan.values[idx] = strtoull(val, NULL, 10);
        else if (!strncmp(key, "dim_kind.", 9) && (d = find_or_add_dim(art, key + 9, 0, NULL))) d->kind = (FlowDimensionKind)atoi(val);
        else if (!strncmp(key, "dim_min.", 8) && (d = find_or_add_dim(art, key + 8, 0, NULL))) d->min_val = strtoull(val, NULL, 10);
        else if (!strncmp(key, "dim_max.", 8) && (d = find_or_add_dim(art, key + 8, 0, NULL))) d->max_val = strtoull(val, NULL, 10);
        else if (!strncmp(key, "dim_step.", 9) && (d = find_or_add_dim(art, key + 9, 0, NULL))) d->step = strtoull(val, NULL, 10);
        else if (!strcmp(key, "metric.energy")) art->metrics.energy = strtod(val, NULL);
        else if (!strcmp(key, "metric.latency")) art->metrics.latency_score = strtod(val, NULL);
        else if (!strcmp(key, "metric.throughput")) art->metrics.throughput_score = strtod(val, NULL);
        else if (!strcmp(key, "metric.memory_bytes")) art->metrics.memory_bytes = (size_t)strtoull(val, NULL, 10);
        else if (!strcmp(key, "metric.capacity")) art->metrics.capacity = (size_t)strtoull(val, NULL, 10);
        else if (!strcmp(key, "metric.benchmark_ns")) art->metrics.benchmark_ns = strtoull(val, NULL, 10);
        else if (!strcmp(key, "verification_status")) strncpy(art->verification_status, val, sizeof(art->verification_status) - 1);
        else if (!strcmp(key, "attestation")) strncpy(art->attestation_msg, val, sizeof(art->attestation_msg) - 1);
        else if (!strcmp(key, "jet.omega")) art->jet_omega = strtod(val, NULL); else if (!strcmp(key, "jet.barrier_mu")) art->jet_barrier_mu = strtod(val, NULL);
        else if (!strcmp(key, "jet.q_saturation")) art->jet_q_saturation = strtod(val, NULL); } return 1; }
int flow_artifact_validate(const FlowPlanArtifact *art, const SemanticIR *ir, const FlowBitSpace *space, char *err_msg, size_t err_size) {
    #define VERR(...) do { if (err_msg && err_size) snprintf(err_msg, err_size, __VA_ARGS__); return 0; } while (0)
    if (err_msg && err_size) err_msg[0] = '\0'; if (!art || !ir) VERR("null artifact or IR"); uint64_t cur_hash = flow_compute_contract_hash(ir);
    if (art->contract_hash && art->contract_hash != cur_hash) VERR("contract hash mismatch (expected %llu, got %llu)", (unsigned long long)cur_hash, (unsigned long long)art->contract_hash);
    const Component *comp = NULL;
    if (space) for (size_t i = 0; i < space->candidate_count; ++i) if (!strcmp(space->candidates[i]->id, art->component_id)) { comp = space->candidates[i]; break; }
    if (!comp) for (size_t i = 0; i < component_count(); ++i) { const Component *c = component_at(i); if (c && !strcmp(c->id, art->component_id)) { comp = c; break; } }
    if (!comp) VERR("component '%s' not registered", art->component_id); if (!component_compatible(ir, comp)) VERR("component '%s' is incompatible with contract", comp->id);
    const FlowPlugin *plugin = flow_component_plugin(comp);
    if (plugin && art->module_version[0] && strcmp(plugin->version, art->module_version)) VERR("module '%s' version mismatch (expected %s, got %s)", plugin->name, plugin->version, art->module_version);
    if (art->plan_schema_hash) { uint64_t exp_schema = flow_bitspace_compute_schema_hash(ir, comp, &art->dimensions);
        if (exp_schema != art->plan_schema_hash) VERR("plan schema hash mismatch (expected %llu, got %llu)", (unsigned long long)exp_schema, (unsigned long long)art->plan_schema_hash); }
    VerificationReport report; if (!flow_component_verify_plan(ir, comp, &art->plan, &report) || report.status == VERIFIER_COMPILE_ERROR) VERR("hard gate verification failed: %s", report.message);
    #undef VERR
    return 1; }
const char *flow_plan_tactic_name(FlowPlanTactic tactic) { static const char * const names[] = {"speed", "balanced", "memory"}; return ((int)tactic >= 0 && (int)tactic < 3) ? names[tactic] : "unknown"; }
