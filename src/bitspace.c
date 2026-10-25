#include "bitspace.h"
#include "registry.h"
#include "security.h"
#include "smt.h"
#include "reload.h"
#include "hardware_telemetry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
                size_t req_cap = space->ir->input_max_count > 0 ? (size_t)space->ir->input_max_count : (is_default ? 4096 : 1024);
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
    uint64_t rng = seed ? (uint64_t)seed : UINT64_C(0x9E3779B97F4A7C15);
    uint64_t walk_genome = best_plan.eval.hard_gate_passed ? best_plan.genome : current_genome;
    size_t max_steps = iterations > 4096 ? 4096 : iterations;
    for (size_t step = 0; step < max_steps; ++step) {
        uint32_t mbit = 0xFFFFFFFF;
        uint64_t next_g = flow_bitspace_mutate_1bit_superposed(space, walk_genome, &space->global_canvas, 0.35, &rng, &mbit);
        (void)mbit;
        FlowPlan cand = {0};
        if (space->decode(space, next_g, &cand)) {
            space->evaluate(space, &cand, &cand.eval);
            FlowGateFailureReason cr = FLOW_GATE_PASS;
            cand.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &cand, &cand.eval, &cr);
            result_out->heatmap.total_mutations++;
            if (cand.eval.hard_gate_passed) {
                pareto_update_bitspace(result_out, &cand);
                walk_genome = next_g;
                if (!best_plan.eval.hard_gate_passed || cand.eval.energy < best_plan.eval.energy) best_plan = cand;
            } else {
                result_out->heatmap.total_failures++;
                result_out->heatmap.failure_counts[cr]++;
            }
        }
    }
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
uint64_t flow_plan_get_value(const FlowPlanDimensionSet *dims, const FlowPlanAssignment *plan, const char *name, uint64_t default_val) {
    if (!dims || !plan || !name) return default_val;
    for (size_t i = 0; i < dims->count && i < plan->count; ++i)
        if (!strcmp(dims->dimensions[i].name, name)) return plan->values[i];
    return default_val; }
FlowTuning flow_default_tuning(const SemanticIR *ir, const Component *comp) { (void)comp;
    return (FlowTuning){.buffer_bytes = 16384, .initial_capacity = 4, .growth_percent = 150, .batch_size = 16384, .arena_bytes = (ir && ir->arena_limit_bytes > 0) ? ir->arena_limit_bytes : 0}; }
int flow_polyhedral_solve_plan(const SemanticIR *ir, const FlowBitSpace *space, FlowPlan *plan_out) { if (!ir || !plan_out) return 0; memset(plan_out, 0, sizeof(*plan_out));
    size_t cand_idx = 0; if (space && space->candidate_count > 0) { const Component *heur = select_component(ir);
        if (heur) for (size_t c = 0; c < space->candidate_count; ++c) if (space->candidates[c] == heur) { cand_idx = c; break; }
        plan_out->component = space->candidates[cand_idx]; plan_out->dimension_set = space->candidate_dims[cand_idx];
    } else if (!(plan_out->component = select_component(ir))) return 0; FlowPolyhedron poly; flow_polyhedral_init(&poly, 2);
    int64_t cap = ir->input_max_count > 0 ? (int64_t)ir->input_max_count : 4096;
    if (ir->top_n > 0 && cap < (int64_t)ir->top_n) cap = (int64_t)ir->top_n; flow_polyhedral_set_box_bounds(&poly, 0, 0, cap); flow_polyhedral_set_box_bounds(&poly, 1, 0, 64);
    FlowPolyhedralSchedule sched = {0}; flow_polyhedral_solve_schedule(&poly, 64, 16, &sched); long online = sysconf(_SC_NPROCESSORS_ONLN);
    online = online < 1 ? 1 : (online > 64 ? 64 : online); size_t target_threads = ir->flow_parallelizable ? 4 : (ir->state_shared ? 16 : 1);
    if (target_threads > (size_t)online) target_threads = (size_t)online; if (!target_threads) target_threads = 1; size_t target_shards = ir->state_shared ? 16 : 1;
    const FlowPlanDimensionSet *dims = &plan_out->dimension_set; plan_out->assignment.count = dims->count; for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i]; uint64_t val = d->default_val;
        if (!strcmp(d->name, "capacity")) {
            val = (uint64_t)cap;
            if (d->kind == FLOW_DIM_EXPONENT) {
                uint64_t p2 = (uint64_t)1 << d->min_val;
                while (p2 < val && p2 < ((uint64_t)1 << d->max_val)) p2 <<= 1;
                val = p2;
            }
        }
        else if (!strcmp(d->name, "threads")) val = target_threads; else if (!strcmp(d->name, "shards")) val = target_shards;
        else if (!strcmp(d->name, "buffer_bytes") || !strcmp(d->name, "batch_size")) val = sched.optimal_tile_size > 0 ? sched.optimal_tile_size * 256 : 16384;
        else if (!strcmp(d->name, "initial_capacity")) val = 4; else if (!strcmp(d->name, "growth_percent")) val = 150; else if (!strcmp(d->name, "arena_bytes")) val = ir->arena_limit_bytes;
        uint64_t lo = (d->kind != FLOW_DIM_EXPONENT) ? d->min_val : ((uint64_t)1 << d->min_val);
        uint64_t hi = (d->kind != FLOW_DIM_EXPONENT) ? d->max_val : ((uint64_t)1 << d->max_val); plan_out->assignment.values[i] = val < lo ? lo : (val > hi ? hi : val); }
    plan_out->genome = space ? flow_bitspace_encode(space, cand_idx, &plan_out->assignment) : (uint64_t)cand_idx;
    if (space && space->evaluate) space->evaluate(space, plan_out, &plan_out->eval);
    plan_out->eval.hard_gate_passed = (space && space->hard_gate) ? space->hard_gate(space, plan_out, &plan_out->eval) : 1;
    if (plan_out->eval.energy == 0.0) plan_out->eval.energy = 48.0; return 1; }
SearchResult search_best(const SemanticIR *ir, size_t iterations, uint32_t seed, int measured, const ProfileSeed *profile) { FlowBitSpace space; SearchResult result = {0};
    if (!ir || !flow_bitspace_init_for_ir(ir, &space)) return result; FlowPlan best_plan = {0}; int found_plan = 0; if (profile && profile->available) {
        for (size_t c = 0; c < space.candidate_count; ++c) { if (component_index(space.candidates[c]) == profile->component || c == profile->component) {
                best_plan.component = space.candidates[c]; best_plan.dimension_set = space.candidate_dims[c];
                best_plan.assignment = profile->assignment.count > 0 ? profile->assignment :
                    (FlowPlanAssignment){.count = 3, .values = {profile->capacity, profile->threads, profile->shards}};
                best_plan.genome = flow_bitspace_encode(&space, c, &best_plan.assignment); space.evaluate(&space, &best_plan, &best_plan.eval);
                best_plan.eval.hard_gate_passed = space.hard_gate ? space.hard_gate(&space, &best_plan, &best_plan.eval) : 1;
                if (best_plan.eval.hard_gate_passed) { found_plan = 1; break; } } } } if (!found_plan && !flow_polyhedral_solve_plan(ir, &space, &best_plan)) return result;
    if (measured && ir && best_plan.component) { best_plan.eval.benchmark_ns = flow_component_benchmark(ir, best_plan.component, &best_plan.assignment);
        best_plan.eval.latency_score = (double)best_plan.eval.benchmark_ns; best_plan.eval.energy = (double)best_plan.eval.benchmark_ns / 1000.0; }
    flow_plan_to_search_result(&best_plan, ir, seed, &result); result.iterations = iterations ? iterations : 1; result.measured = measured;
    result.benchmark_ns = best_plan.eval.benchmark_ns; result.pareto = (FlowParetoSummary){
        .count = 1, .best_latency = best_plan.eval.latency_score, .best_memory = best_plan.eval.memory_bytes,
        .points = {{ .component = best_plan.component, .assignment = best_plan.assignment, .energy = best_plan.eval.energy, .metrics = result.metrics }} };
    if (profile && profile->available) {
        #define OVR(f) if (profile->tuning.f) result.tuning.f = profile->tuning.f
        OVR(buffer_bytes); OVR(initial_capacity); OVR(growth_percent); OVR(batch_size); OVR(arena_bytes);
        #undef OVR
    } return result; }
int flow_artifact_to_profile_seed(const FlowPlanArtifact *art, ProfileSeed *seed_out) { if (!art || !seed_out) return 0; *seed_out = (ProfileSeed){
        .available = 1, .capacity = art->metrics.capacity, .benchmark_ns = art->metrics.benchmark_ns, .assignment = art->plan, .tuning = {
            .buffer_bytes = flow_plan_get_value(&art->dimensions, &art->plan, "buffer_bytes", 16384),
            .initial_capacity = flow_plan_get_value(&art->dimensions, &art->plan, "initial_capacity", 4),
            .growth_percent = (unsigned)flow_plan_get_value(&art->dimensions, &art->plan, "growth_percent", 150),
            .batch_size = flow_plan_get_value(&art->dimensions, &art->plan, "batch_size", 16384),
            .arena_bytes = flow_plan_get_value(&art->dimensions, &art->plan, "arena_bytes", 0) } }; return 1; }
void flow_plan_to_search_result(const FlowPlan *plan, const SemanticIR *ir, uint32_t seed, SearchResult *out) { if (!plan || !out) return; *out = (SearchResult){
        .component = plan->component, .genome = plan->genome, .dimension_set = plan->dimension_set,
        .assignment = plan->assignment, .capacity = (double)plan->eval.capacity, .energy = plan->eval.energy,
        .benchmark_ns = (uint64_t)plan->eval.latency_score, .iterations = 1, .seed = seed, .tuning = flow_default_tuning(ir, plan->component), .metrics = {
            .capacity = plan->eval.capacity, .latency_score = plan->eval.latency_score,
            .throughput_score = plan->eval.throughput_score, .memory_bytes = plan->eval.memory_bytes, .energy = plan->eval.energy,
            .threads = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "threads", 1),
            .shards = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "shards", 1) } };
    out->threads = (double)out->metrics.threads; out->shards = (double)out->metrics.shards;
    out->tuning.buffer_bytes = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "buffer_bytes", out->tuning.buffer_bytes);
    out->tuning.initial_capacity = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "initial_capacity", out->tuning.initial_capacity);
    out->tuning.growth_percent = (unsigned)flow_plan_get_value(&plan->dimension_set, &plan->assignment, "growth_percent", out->tuning.growth_percent);
    out->tuning.batch_size = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "batch_size", out->tuning.batch_size);
    out->tuning.arena_bytes = flow_plan_get_value(&plan->dimension_set, &plan->assignment, "arena_bytes", out->tuning.arena_bytes); }
static void eval_particle_plan(const FlowBitSpace *space, uint64_t genome, FlowPlan *plan) { space->decode(space, genome, plan); space->evaluate(space, plan, &plan->eval);
    plan->eval.hard_gate_passed = space->hard_gate(space, plan, &plan->eval); if (!plan->eval.hard_gate_passed) plan->eval.energy += 1.0e12; }
int flow_swarm_init(FlowSwarmCluster *cluster, const FlowBitSpace *space, size_t particle_count, uint32_t base_seed) { if (!cluster || !space || !particle_count) return 0;
    if (particle_count > FLOW_SWARM_MAX_PARTICLES) particle_count = FLOW_SWARM_MAX_PARTICLES; *cluster = (FlowSwarmCluster){ .space = space, .particle_count = particle_count,
        .rng_state = base_seed ? (uint64_t)base_seed : UINT64_C(0x1928374650afbecd), .pheromone = { .evaporation_rate = 0.92, .reinforcement_weight = 2.5,
                       .consensus_mask = (space->bit_count >= 64) ? ~0ULL : ((1ULL << space->bit_count) - 1ULL) } };
    for (int b = 0; b < 64; ++b) cluster->pheromone.bit_intensity[b] = 1.0; if (space->env_mask) cluster->pheromone.consensus_mask &= space->env_mask;
    uint64_t def_genome = flow_bitspace_default_genome(space); for (size_t i = 0; i < particle_count; ++i) { FlowSwarmParticle *p = &cluster->particles[i];
        p->id = (uint32_t)i; p->current_genome = def_genome ^ (i > 0 ? (1ULL << (flow_prng_next(&cluster->rng_state) % (space->bit_count ? space->bit_count : 1))) : 0ULL);
        p->best_genome = p->current_genome; p->local_mask = space->env_mask; eval_particle_plan(space, p->current_genome, &p->best_plan);
        p->current_energy = p->best_energy = p->best_plan.eval.energy;
        if (i == 0 || (p->best_plan.eval.hard_gate_passed && (!cluster->global_best_plan.eval.hard_gate_passed || p->best_energy < cluster->global_best_energy))) {
            cluster->global_best_plan = p->best_plan; cluster->global_best_energy = p->best_energy; } } return 1; }
int flow_swarm_diffuse_pheromone(FlowSwarmCluster *cluster) { if (!cluster || !cluster->space) return 0;
    uint32_t bits = (cluster->space->bit_count > 0 && cluster->space->bit_count <= 64) ? cluster->space->bit_count : (cluster->space->bit_count > 64 ? 64 : 16);
    double sum = 0.0; for (uint32_t b = 0; b < bits; ++b) {
        cluster->pheromone.bit_intensity[b] = fmax(0.1, cluster->pheromone.bit_intensity[b] * cluster->pheromone.evaporation_rate); sum += cluster->pheromone.bit_intensity[b];
    } double avg = bits > 0 ? sum / (double)bits : 1.0; uint64_t mask = 0, semilattice = 0;
    for (uint32_t b = 0; b < bits; ++b) if (cluster->pheromone.bit_intensity[b] >= avg) mask |= (1ULL << b);
    for (size_t i = 0; i < cluster->particle_count; ++i)
        if (cluster->particles[i].best_plan.eval.hard_gate_passed)
            semilattice |= (cluster->particles[i].best_genome & cluster->particles[i].local_mask);
    mask |= semilattice;
    if (!mask) mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1ULL); if (cluster->space->env_mask) mask &= cluster->space->env_mask; cluster->pheromone.consensus_mask = mask;
    return 1; }
int flow_swarm_step(FlowSwarmCluster *cluster, size_t steps_per_particle) { if (!cluster || !cluster->space) return 0;
    const FlowBitSpace *space = cluster->space; uint32_t bits = (space->bit_count > 0 && space->bit_count <= 64) ? space->bit_count : (space->bit_count > 64 ? 64 : 16);
    FlowMaskCanvas swarm_canvas = space->global_canvas; swarm_canvas.soft_composite_bias |= cluster->pheromone.consensus_mask;
    if (swarm_canvas.hard_composite_mask) swarm_canvas.soft_composite_bias &= swarm_canvas.hard_composite_mask;
    for (size_t p_idx = 0; p_idx < cluster->particle_count; ++p_idx) { FlowSwarmParticle *p = &cluster->particles[p_idx];
        for (size_t step = 0; step < steps_per_particle; ++step) { cluster->total_swarm_mutations++; uint32_t mutated_bit = 0;
            uint64_t cand_genome = flow_bitspace_mutate_1bit_superposed(space, p->current_genome, &swarm_canvas, 0.65, &cluster->rng_state, &mutated_bit);
            if (mutated_bit == 0xFFFFFFFF || mutated_bit >= bits) continue; FlowPlan cand_plan; eval_particle_plan(space, cand_genome, &cand_plan);
            if (cand_plan.eval.energy < p->current_energy || !p->best_plan.eval.hard_gate_passed) { p->current_genome = cand_genome; p->current_energy = cand_plan.eval.energy;
                if (cand_plan.eval.hard_gate_passed && (!p->best_plan.eval.hard_gate_passed || cand_plan.eval.energy < p->best_energy)) {
                    double gain = p->best_energy - cand_plan.eval.energy;
                    p->best_energy = cand_plan.eval.energy; p->best_genome = cand_genome; p->best_plan = cand_plan; p->stagnation_steps = 0; if (mutated_bit < 64)
                        cluster->pheromone.bit_intensity[mutated_bit] += cluster->pheromone.reinforcement_weight * ((gain > 0.0) ? (gain / (cand_plan.eval.energy + 1.0)) : 1.0);
                    if (!cluster->global_best_plan.eval.hard_gate_passed || p->best_energy < cluster->global_best_energy) {
                        cluster->global_best_plan = cand_plan; cluster->global_best_energy = p->best_energy; } } } else if (++p->stagnation_steps >= 8) {
                cluster->total_saddle_point_escapes++;
                uint64_t base_g = cluster->global_best_plan.eval.hard_gate_passed ? cluster->global_best_plan.genome : flow_bitspace_default_genome(space);
                p->current_genome = base_g ^ (1ULL << (flow_prng_next(&cluster->rng_state) % bits)); space->decode(space, p->current_genome, &p->best_plan);
                space->evaluate(space, &p->best_plan, &p->best_plan.eval); p->current_energy = p->best_plan.eval.energy; p->stagnation_steps = 0; } } }
    return flow_swarm_diffuse_pheromone(cluster); }
int flow_swarm_search(const FlowBitSpace *space, size_t particle_count, size_t cycles, uint32_t seed, int measured, FlowBitSearchResult *result_out) {
    if (!space || !result_out) return 0; if (!particle_count) particle_count = 8; if (!cycles) cycles = 10; FlowSwarmCluster cluster;
    if (!flow_swarm_init(&cluster, space, particle_count, seed)) return 0; for (size_t c = 0; c < cycles; ++c) flow_swarm_step(&cluster, 10);
    if (measured && cluster.global_best_plan.component && space->ir)
        cluster.global_best_plan.eval.benchmark_ns = flow_component_benchmark(space->ir, cluster.global_best_plan.component, &cluster.global_best_plan.assignment);
    *result_out = (FlowBitSearchResult){ .best_plan = cluster.global_best_plan, .mask_canvas = space->global_canvas,
        .iterations = particle_count * cycles * 10, .seed = seed, .measured = measured }; result_out->mask_canvas.soft_composite_bias = cluster.pheromone.consensus_mask;
    return cluster.global_best_plan.eval.hard_gate_passed ? 1 : 0; }

/* =========================================================================
 * C17 Implementation of SEARU no_std_tool::numerics (U256, Prime, Linalg, Random)
 * ========================================================================= */
FlowU256 flow_u256_zero(void) {
    FlowU256 z = {{0ULL, 0ULL, 0ULL, 0ULL}};
    return z;
}
FlowU256 flow_u256_one(void) {
    FlowU256 o = {{1ULL, 0ULL, 0ULL, 0ULL}};
    return o;
}
FlowU256 flow_u256_from_u64(uint64_t v) {
    FlowU256 u = {{v, 0ULL, 0ULL, 0ULL}};
    return u;
}
int flow_u256_from_hex_str(const char *hex, FlowU256 *out) {
    if (!hex || !out) return 0;
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    size_t len = strlen(hex);
    if (len == 0 || len > 64) return 0;
    *out = flow_u256_zero();
    size_t pos = len;
    for (size_t wi = 0; wi < 4 && pos > 0; ++wi) {
        uint64_t word = 0ULL;
        for (uint32_t j = 0; j < 16 && pos > 0; ++j) {
            char c = hex[--pos];
            uint64_t v = 0;
            if (c >= '0' && c <= '9') v = (uint64_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v = (uint64_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = (uint64_t)(c - 'A' + 10);
            else return 0;
            word |= (v << (j * 4U));
        }
        out->w[wi] = word;
    }
    return 1;
}
int flow_u256_is_zero(const FlowU256 *a) {
    return a && (a->w[0] | a->w[1] | a->w[2] | a->w[3]) == 0ULL;
}
int flow_u256_is_even(const FlowU256 *a) {
    return a && ((a->w[0] & 1ULL) == 0ULL);
}
uint32_t flow_u256_leading_zeros(const FlowU256 *a) {
    if (!a) return 256U;
    uint32_t z = 0U;
    for (int i = 3; i >= 0; --i) {
        if (a->w[i] == 0ULL) z += 64U;
        else { z += (uint32_t)__builtin_clzll(a->w[i]); break; }
    }
    return z;
}
int flow_u256_cmp(const FlowU256 *a, const FlowU256 *b) {
    for (int i = 3; i >= 0; --i) {
        if (a->w[i] < b->w[i]) return -1;
        if (a->w[i] > b->w[i]) return 1;
    }
    return 0;
}
FlowU256 flow_u256_overflowing_add(FlowU256 a, FlowU256 b, int *overflow_out) {
    FlowU256 r = {{0}};
    uint64_t carry = 0ULL;
    for (int i = 0; i < 4; ++i) {
        flow_u128_t s = (flow_u128_t)a.w[i] + (flow_u128_t)b.w[i] + (flow_u128_t)carry;
        r.w[i] = (uint64_t)s;
        carry = (uint64_t)(s >> 64U);
    }
    if (overflow_out) *overflow_out = (carry > 0ULL);
    return r;
}
FlowU256 flow_u256_overflowing_sub(FlowU256 a, FlowU256 b, int *borrow_out) {
    FlowU256 r = {{0}};
    uint64_t borrow = 0ULL;
    for (int i = 0; i < 4; ++i) {
        uint64_t sub1 = a.w[i] - b.w[i];
        uint64_t ov1 = (a.w[i] < b.w[i]) ? 1ULL : 0ULL;
        uint64_t sub2 = sub1 - borrow;
        uint64_t ov2 = (sub1 < borrow) ? 1ULL : 0ULL;
        r.w[i] = sub2;
        borrow = ov1 | ov2;
    }
    if (borrow_out) *borrow_out = (borrow > 0ULL);
    return r;
}
FlowU256 flow_u256_shl1(FlowU256 a) {
    FlowU256 r = {{0}};
    uint64_t carry = 0ULL;
    for (int i = 0; i < 4; ++i) {
        r.w[i] = (a.w[i] << 1U) | carry;
        carry = a.w[i] >> 63U;
    }
    return r;
}
FlowU256 flow_u256_shr1(FlowU256 a) {
    FlowU256 r = {{0}};
    uint64_t carry = 0ULL;
    for (int i = 3; i >= 0; --i) {
        r.w[i] = (a.w[i] >> 1U) | carry;
        carry = a.w[i] << 63U;
    }
    return r;
}
void flow_u256_mul_wide(FlowU256 a, FlowU256 b, uint64_t out_512[8]) {
    memset(out_512, 0, sizeof(uint64_t) * 8);
    for (int i = 0; i < 4; ++i) {
        flow_u128_t carry = 0;
        for (int j = 0; j < 4; ++j) {
            flow_u128_t prod = (flow_u128_t)a.w[i] * (flow_u128_t)b.w[j] + (flow_u128_t)out_512[i + j] + carry;
            out_512[i + j] = (uint64_t)prod;
            carry = prod >> 64U;
        }
        int idx = i + 4;
        while (carry != 0 && idx < 8) {
            flow_u128_t sum = (flow_u128_t)out_512[idx] + carry;
            out_512[idx] = (uint64_t)sum;
            carry = sum >> 64U;
            idx++;
        }
    }
}
FlowU256 flow_u256_mul(FlowU256 a, FlowU256 b) {
    uint64_t wide[8];
    flow_u256_mul_wide(a, b, wide);
    FlowU256 r = {{wide[0], wide[1], wide[2], wide[3]}};
    return r;
}
int flow_u256_div_rem(FlowU256 a, FlowU256 b, FlowU256 *q_out, FlowU256 *r_out) {
    if (flow_u256_is_zero(&b)) return 0;
    if (flow_u256_cmp(&a, &b) < 0) {
        if (q_out) *q_out = flow_u256_zero();
        if (r_out) *r_out = a;
        return 1;
    }
    FlowU256 q = flow_u256_zero(), r = flow_u256_zero();
    uint32_t bits = 256U - flow_u256_leading_zeros(&a);
    for (int32_t i = (int32_t)bits - 1; i >= 0; --i) {
        r = flow_u256_shl1(r);
        uint64_t bit = (a.w[i / 64] >> (i % 64)) & 1ULL;
        r.w[0] |= bit;
        if (flow_u256_cmp(&r, &b) >= 0) {
            r = flow_u256_overflowing_sub(r, b, NULL);
            q.w[i / 64] |= (1ULL << (i % 64));
        }
    }
    if (q_out) *q_out = q;
    if (r_out) *r_out = r;
    return 1;
}
uint64_t flow_u256_inv_word(const FlowU256 *n) {
    if (!n || (n->w[0] & 1ULL) == 0ULL) return 0ULL;
    uint64_t x = n->w[0], inv = x;
    for (int i = 0; i < 6; ++i) inv = inv * (2ULL - x * inv);
    return 0ULL - inv;
}
FlowU256 flow_u256_mont_reduce(const uint64_t t_in[8], const FlowU256 *modulus, uint64_t n0_inv) {
    uint64_t t[8];
    memcpy(t, t_in, sizeof(t));
    for (int i = 0; i < 4; ++i) {
        uint64_t m = t[i] * n0_inv;
        flow_u128_t carry = 0;
        for (int j = 0; j < 4; ++j) {
            flow_u128_t prod = (flow_u128_t)m * (flow_u128_t)modulus->w[j] + (flow_u128_t)t[i + j] + carry;
            t[i + j] = (uint64_t)prod;
            carry = prod >> 64U;
        }
        int j = i + 4;
        while (carry > 0 && j < 8) {
            flow_u128_t sum = (flow_u128_t)t[j] + carry;
            t[j] = (uint64_t)sum;
            carry = sum >> 64U;
            j++;
        }
    }
    FlowU256 res = {{t[4], t[5], t[6], t[7]}};
    if (flow_u256_cmp(&res, modulus) >= 0) res = flow_u256_overflowing_sub(res, *modulus, NULL);
    return res;
}
static FlowU256 flow_u256_mul_mod_even(FlowU256 a, FlowU256 b, FlowU256 m) {
    FlowU256 res = flow_u256_zero();
    FlowU256 cur = flow_u256_zero();
    flow_u256_div_rem(a, m, NULL, &cur);
    while (!flow_u256_is_zero(&b)) {
        if (!flow_u256_is_even(&b)) {
            res = flow_u256_overflowing_add(res, cur, NULL);
            if (flow_u256_cmp(&res, &m) >= 0) res = flow_u256_overflowing_sub(res, m, NULL);
        }
        cur = flow_u256_shl1(cur);
        if (flow_u256_cmp(&cur, &m) >= 0) cur = flow_u256_overflowing_sub(cur, m, NULL);
        b = flow_u256_shr1(b);
    }
    return res;
}
FlowU256 flow_u256_mod_pow(FlowU256 base, FlowU256 exp, FlowU256 modulus) {
    FlowU256 one = flow_u256_one();
    if (flow_u256_is_zero(&modulus) || flow_u256_cmp(&modulus, &one) == 0) return flow_u256_zero();
    if (flow_u256_is_even(&modulus)) {
        FlowU256 res = one, b = flow_u256_zero();
        flow_u256_div_rem(base, modulus, NULL, &b);
        while (!flow_u256_is_zero(&exp)) {
            if (!flow_u256_is_even(&exp)) res = flow_u256_mul_mod_even(res, b, modulus);
            b = flow_u256_mul_mod_even(b, b, modulus);
            exp = flow_u256_shr1(exp);
        }
        return res;
    }
    uint64_t n0_inv = flow_u256_inv_word(&modulus);
    FlowU256 r = one;
    for (int i = 0; i < 256; ++i) {
        r = flow_u256_shl1(r);
        if (flow_u256_cmp(&r, &modulus) >= 0) r = flow_u256_overflowing_sub(r, modulus, NULL);
    }
    FlowU256 r2 = r;
    for (int i = 0; i < 256; ++i) {
        r2 = flow_u256_shl1(r2);
        if (flow_u256_cmp(&r2, &modulus) >= 0) r2 = flow_u256_overflowing_sub(r2, modulus, NULL);
    }
    uint64_t wide[8];
    flow_u256_mul_wide(base, r2, wide);
    FlowU256 b_mont = flow_u256_mont_reduce(wide, &modulus, n0_inv);
    FlowU256 res_mont = r;
    while (!flow_u256_is_zero(&exp)) {
        if (!flow_u256_is_even(&exp)) {
            flow_u256_mul_wide(res_mont, b_mont, wide);
            res_mont = flow_u256_mont_reduce(wide, &modulus, n0_inv);
        }
        flow_u256_mul_wide(b_mont, b_mont, wide);
        b_mont = flow_u256_mont_reduce(wide, &modulus, n0_inv);
        exp = flow_u256_shr1(exp);
    }
    flow_u256_mul_wide(res_mont, one, wide);
    return flow_u256_mont_reduce(wide, &modulus, n0_inv);
}
int flow_u256_is_probably_prime(const FlowU256 *n, const uint64_t *bases, size_t base_count) {
    if (!n) return 0;
    FlowU256 one = flow_u256_one(), two = flow_u256_from_u64(2ULL), three = flow_u256_from_u64(3ULL);
    if (flow_u256_is_zero(n) || flow_u256_cmp(n, &one) == 0) return 0;
    if (flow_u256_cmp(n, &two) == 0 || flow_u256_cmp(n, &three) == 0) return 1;
    if (flow_u256_is_even(n)) return 0;
    FlowU256 n_minus_1 = flow_u256_overflowing_sub(*n, one, NULL);
    FlowU256 d = n_minus_1;
    uint32_t s = 0;
    while (flow_u256_is_even(&d)) { d = flow_u256_shr1(d); s++; }
    for (size_t bi = 0; bi < base_count; ++bi) {
        FlowU256 a = flow_u256_from_u64(bases[bi]);
        if (flow_u256_cmp(&a, n) >= 0) continue;
        FlowU256 x = flow_u256_mod_pow(a, d, *n);
        if (flow_u256_cmp(&x, &one) == 0 || flow_u256_cmp(&x, &n_minus_1) == 0) continue;
        int composite = 1;
        for (uint32_t r = 1; r < s; ++r) {
            x = flow_u256_mod_pow(x, two, *n);
            if (flow_u256_cmp(&x, &n_minus_1) == 0) { composite = 0; break; }
        }
        if (composite) return 0;
    }
    return 1;
}
static uint64_t flow_splitmix64_seed(uint64_t state) {
    state += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t z = state;
    z = (z ^ (z >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31U);
}
static inline uint64_t flow_rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}
void flow_xoshiro256ss_init(FlowXoshiro256SS *rng, uint64_t seed) {
    if (!rng) return;
    rng->s[0] = flow_splitmix64_seed(seed);
    rng->s[1] = flow_splitmix64_seed(seed + 1ULL);
    rng->s[2] = flow_splitmix64_seed(seed + 2ULL);
    rng->s[3] = flow_splitmix64_seed(seed + 3ULL);
    if ((rng->s[0] | rng->s[1] | rng->s[2] | rng->s[3]) == 0ULL) rng->s[0] = 1ULL;
}
uint64_t flow_xoshiro256ss_next_u64(FlowXoshiro256SS *rng) {
    if (!rng) return 0ULL;
    uint64_t result = flow_rotl64(rng->s[1] * 5ULL, 7) * 9ULL;
    uint64_t t = rng->s[1] << 17U;
    rng->s[2] ^= rng->s[0];
    rng->s[3] ^= rng->s[1];
    rng->s[1] ^= rng->s[2];
    rng->s[0] ^= rng->s[3];
    rng->s[2] ^= t;
    rng->s[3] = flow_rotl64(rng->s[3], 45);
    return result;
}
int flow_xoshiro256ss_next_bool(FlowXoshiro256SS *rng) {
    return (int)(flow_xoshiro256ss_next_u64(rng) & 1ULL);
}
void flow_fx_mat_mul_ikj(size_t R, size_t K, size_t C, const flow_fx64_t *a, const flow_fx64_t *b, flow_fx64_t *out) {
    if (!a || !b || !out) return;
    memset(out, 0, sizeof(flow_fx64_t) * R * C);
    for (size_t i = 0; i < R; ++i) {
        for (size_t k = 0; k < K; ++k) {
            flow_fx64_t aik = a[i * K + k];
            for (size_t j = 0; j < C; ++j) {
                out[i * C + j] = flow_fx64_add(out[i * C + j], flow_fx64_mul(aik, b[k * C + j]));
            }
        }
    }
}
flow_fx64_t flow_fx_mse_loss(size_t N, size_t OUT, const flow_fx64_t *predictions, const flow_fx64_t *targets) {
    if (!predictions || !targets || N == 0) return 0;
    flow_fx64_t sum_sq = 0;
    for (size_t i = 0; i < N * OUT; ++i) {
        flow_fx64_t diff = flow_fx64_sub(predictions[i], targets[i]);
        sum_sq = flow_fx64_add(sum_sq, flow_fx64_mul(diff, diff));
    }
    return flow_fx64_div(sum_sq, (flow_fx64_t)N << FLOW_FX_SHIFT);
}
void flow_fx_gradient_descent_step(size_t N, size_t IN, size_t OUT,
                                   const flow_fx64_t *inputs, flow_fx64_t *weights,
                                   const flow_fx64_t *predictions, const flow_fx64_t *targets,
                                   flow_fx64_t learning_rate) {
    if (!inputs || !weights || !predictions || !targets || N == 0) return;
    flow_fx64_t factor = flow_fx64_div(2LL << FLOW_FX_SHIFT, (flow_fx64_t)N << FLOW_FX_SHIFT);
    flow_fx64_t scale = flow_fx64_mul(factor, learning_rate);
    for (size_t i = 0; i < IN; ++i) {
        for (size_t j = 0; j < OUT; ++j) {
            flow_fx64_t grad_ij = 0;
            for (size_t n = 0; n < N; ++n) {
                flow_fx64_t diff = flow_fx64_sub(predictions[n * OUT + j], targets[n * OUT + j]);
                grad_ij = flow_fx64_add(grad_ij, flow_fx64_mul(inputs[n * IN + i], diff));
            }
            weights[i * OUT + j] = flow_fx64_sub(weights[i * OUT + j], flow_fx64_mul(grad_ij, scale));
        }
    }
}
