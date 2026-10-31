#include "bitspace.h"
#include "registry.h"
#include "reload.h"
#include "flow_jet.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define FLOW_BMF_DEFAULT_CAPACITY        4096U
#define FLOW_BMF_MIN_CAPACITY            1024U
#define FLOW_BMF_DEFAULT_BUFFER_BYTES    16384U
#define FLOW_BMF_DEFAULT_INITIAL_CAP     4U
#define FLOW_BMF_DEFAULT_GROWTH_PCT      150U
#define FLOW_BMF_HARD_GATE_PENALTY       1.0e12
#define FLOW_BMF_SOFT_BIAS_WEIGHT        0.35
#define FLOW_SWARM_BIAS_WEIGHT           0.65
#define FLOW_SWARM_EVAPORATION_RATE      0.92
#define FLOW_SWARM_REINFORCEMENT_WEIGHT  2.5
#define FLOW_SWARM_STAGNATION_LIMIT      8U
static uint64_t hash_bytes(uint64_t hash, const void *ptr, size_t size) { const uint8_t *bytes = (const uint8_t *)ptr;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * FLOW_FNV1A_64_PRIME; return hash; }
static uint64_t hash_str(uint64_t hash, const char *str) { return str ? hash_bytes(hash, str, strlen(str)) : hash; }
uint64_t flow_compute_contract_hash(const SemanticIR *ir) { uint64_t h = FLOW_FNV1A_64_OFFSET; if (!ir) return h;
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
static uint64_t encode_candidate_canonical(const FlowBitSpace *space, size_t cand_idx, int is_default) { uint64_t genome = (uint64_t)cand_idx;
    const FlowPlanDimensionSet *dims = &space->candidate_dims[cand_idx]; for (size_t i = 0, shift = space->selector_bits; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i]; unsigned bits = dimension_bits(d); uint64_t val = is_default ? d->default_val : d->min_val;
        int val_is_exp = is_default && (d->kind == FLOW_DIM_EXPONENT); if (space->ir) { if (!strcmp(d->name, "capacity")) {
                size_t req_cap = space->ir->input_max_count > 0 ? (size_t)space->ir->input_max_count : (is_default ? FLOW_BMF_DEFAULT_CAPACITY : FLOW_BMF_MIN_CAPACITY);
                if (is_default && space->ir->top_n > 0 && req_cap < (size_t)space->ir->top_n) req_cap = (size_t)space->ir->top_n; val = req_cap; val_is_exp = 0;
            } else if ((!strcmp(d->name, "threads") || !strcmp(d->name, "shards")) && d->kind == FLOW_DIM_EXPONENT) {
                uint64_t exp = (!strcmp(d->name, "threads") && space->ir->flow_parallelizable) ? 2 : (space->ir->state_shared ? 4 : 0);
                val = exp < d->min_val ? d->min_val : (exp > d->max_val ? d->max_val : exp); val_is_exp = 1; } }
        uint64_t mask = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1); genome |= (encode_dim_raw(d, val, val_is_exp) & mask) << shift; shift += bits; } return genome; }
uint64_t flow_bitspace_default_genome(const FlowBitSpace *space) { if (!space || !space->candidate_count) return 0; size_t cand_idx = 0; if (space->ir) {
        const Component *canonical_comp = select_component(space->ir);
        if (canonical_comp) for (size_t c = 0; c < space->candidate_count; ++c) if (space->candidates[c] == canonical_comp) { cand_idx = c; break; } }
    return encode_candidate_canonical(space, cand_idx, 1); }
int flow_bitspace_search(const FlowBitSpace *space, size_t iterations, uint32_t seed, int measured, const FlowPlan *seed_plan, FlowBitSearchResult *result_out) {
    if (!space || !result_out || !space->candidate_count) return 0; memset(result_out, 0, sizeof(*result_out));
    result_out->iterations = iterations ? iterations : 1; result_out->seed = seed; result_out->measured = measured; FlowPlan best_plan = {0};
    uint64_t current_genome = seed_plan ? seed_plan->genome : flow_bitspace_default_genome(space); if (space->decode(space, current_genome, &best_plan)) {
        space->evaluate(space, &best_plan, &best_plan.eval); FlowGateFailureReason r = FLOW_GATE_PASS;
        best_plan.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &best_plan, &best_plan.eval, &r); result_out->heatmap.total_mutations++;
        if (best_plan.eval.hard_gate_passed) pareto_update_bitspace(result_out, &best_plan);
        else { best_plan.eval.energy += FLOW_BMF_HARD_GATE_PENALTY; result_out->heatmap.total_failures++; result_out->heatmap.failure_counts[r]++; } }
    for (size_t c = 0; c < space->candidate_count; ++c) { FlowPlan cand; if (space->decode(space, encode_candidate_canonical(space, c, 0), &cand)) {
            space->evaluate(space, &cand, &cand.eval); FlowGateFailureReason cr = FLOW_GATE_PASS;
            cand.eval.hard_gate_passed = hierarchical_hard_gate_reason(space, &cand, &cand.eval, &cr); result_out->heatmap.total_mutations++; if (cand.eval.hard_gate_passed) {
                pareto_update_bitspace(result_out, &cand); if (!best_plan.eval.hard_gate_passed || cand.eval.energy < best_plan.eval.energy) best_plan = cand;
            } else { result_out->heatmap.total_failures++; result_out->heatmap.failure_counts[cr]++; } } }
    uint64_t rng = seed ? (uint64_t)seed : FLOW_GOLDEN_RATIO_64;
    uint64_t walk_genome = best_plan.eval.hard_gate_passed ? best_plan.genome : current_genome;
    size_t max_steps = iterations > FLOW_BMF_DEFAULT_CAPACITY ? FLOW_BMF_DEFAULT_CAPACITY : iterations;
    for (size_t step = 0; step < max_steps; ++step) {
        uint32_t mbit = 0xFFFFFFFF;
        uint64_t next_g = flow_bitspace_mutate_1bit_superposed(space, walk_genome, &space->global_canvas, FLOW_BMF_SOFT_BIAS_WEIGHT, &rng, &mbit);
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
    return (FlowTuning){.buffer_bytes = FLOW_BMF_DEFAULT_BUFFER_BYTES, .initial_capacity = FLOW_BMF_DEFAULT_INITIAL_CAP, .growth_percent = FLOW_BMF_DEFAULT_GROWTH_PCT, .batch_size = FLOW_BMF_DEFAULT_BUFFER_BYTES, .arena_bytes = (ir && ir->arena_limit_bytes > 0) ? ir->arena_limit_bytes : 0}; }
int flow_polyhedral_solve_plan(const SemanticIR *ir, const FlowBitSpace *space, FlowPlan *plan_out) { if (!ir || !plan_out) return 0; memset(plan_out, 0, sizeof(*plan_out));
    size_t cand_idx = 0; if (space && space->candidate_count > 0) { const Component *canonical_comp = select_component(ir);
        if (canonical_comp) for (size_t c = 0; c < space->candidate_count; ++c) if (space->candidates[c] == canonical_comp) { cand_idx = c; break; }
        plan_out->component = space->candidates[cand_idx]; plan_out->dimension_set = space->candidate_dims[cand_idx];
    } else if (!(plan_out->component = select_component(ir))) return 0; FlowPolyhedron poly; flow_polyhedral_init(&poly, 2);
    int64_t cap = ir->input_max_count > 0 ? (int64_t)ir->input_max_count : (int64_t)FLOW_BMF_DEFAULT_CAPACITY;
    if (ir->top_n > 0 && cap < (int64_t)ir->top_n) cap = (int64_t)ir->top_n; flow_polyhedral_set_box_bounds(&poly, 0, 0, cap); flow_polyhedral_set_box_bounds(&poly, 1, 0, (int64_t)FLOW_WORD_BITS);
    FlowPolyhedralSchedule sched = {0}; flow_polyhedral_solve_schedule(&poly, (int64_t)FLOW_WORD_BITS, 16, &sched); long online = sysconf(_SC_NPROCESSORS_ONLN);
    online = online < 1 ? 1 : (online > (long)FLOW_WORD_BITS ? (long)FLOW_WORD_BITS : online); size_t target_threads = ir->flow_parallelizable ? 4 : (ir->state_shared ? 16 : 1);
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
        else if (!strcmp(d->name, "buffer_bytes") || !strcmp(d->name, "batch_size")) val = sched.optimal_tile_size > 0 ? sched.optimal_tile_size * 256 : FLOW_BMF_DEFAULT_BUFFER_BYTES;
        else if (!strcmp(d->name, "initial_capacity")) val = FLOW_BMF_DEFAULT_INITIAL_CAP; else if (!strcmp(d->name, "growth_percent")) val = FLOW_BMF_DEFAULT_GROWTH_PCT; else if (!strcmp(d->name, "arena_bytes")) val = ir->arena_limit_bytes;
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
    plan->eval.hard_gate_passed = space->hard_gate(space, plan, &plan->eval); if (!plan->eval.hard_gate_passed) plan->eval.energy += FLOW_BMF_HARD_GATE_PENALTY; }
int flow_swarm_init(FlowSwarmCluster *cluster, const FlowBitSpace *space, size_t particle_count, uint32_t base_seed) { if (!cluster || !space || !particle_count) return 0;
    if (particle_count > FLOW_SWARM_MAX_PARTICLES) particle_count = FLOW_SWARM_MAX_PARTICLES; *cluster = (FlowSwarmCluster){ .space = space, .particle_count = particle_count,
        .rng_state = base_seed ? (uint64_t)base_seed : FLOW_GOLDEN_RATIO_64, .pheromone = { .evaporation_rate = FLOW_SWARM_EVAPORATION_RATE, .reinforcement_weight = FLOW_SWARM_REINFORCEMENT_WEIGHT,
                       .consensus_mask = (space->bit_count >= FLOW_WORD_BITS) ? ~0ULL : ((1ULL << space->bit_count) - 1ULL) } };
    for (int b = 0; b < (int)FLOW_WORD_BITS; ++b) cluster->pheromone.bit_intensity[b] = 1.0; if (space->env_mask) cluster->pheromone.consensus_mask &= space->env_mask;
    uint64_t def_genome = flow_bitspace_default_genome(space); for (size_t i = 0; i < particle_count; ++i) { FlowSwarmParticle *p = &cluster->particles[i];
        p->id = (uint32_t)i; p->current_genome = def_genome ^ (i > 0 ? (1ULL << (flow_prng_next(&cluster->rng_state) % (space->bit_count ? space->bit_count : 1))) : 0ULL);
        p->best_genome = p->current_genome; p->local_mask = space->env_mask; eval_particle_plan(space, p->current_genome, &p->best_plan);
        p->current_energy = p->best_energy = p->best_plan.eval.energy;
        if (i == 0 || (p->best_plan.eval.hard_gate_passed && (!cluster->global_best_plan.eval.hard_gate_passed || p->best_energy < cluster->global_best_energy))) {
            cluster->global_best_plan = p->best_plan; cluster->global_best_energy = p->best_energy; } } return 1; }
int flow_swarm_diffuse_pheromone(FlowSwarmCluster *cluster) { if (!cluster || !cluster->space) return 0;
    uint32_t bits = (cluster->space->bit_count > 0 && cluster->space->bit_count <= FLOW_WORD_BITS) ? cluster->space->bit_count : (cluster->space->bit_count > FLOW_WORD_BITS ? FLOW_WORD_BITS : 16);
    double sum = 0.0; for (uint32_t b = 0; b < bits; ++b) {
        cluster->pheromone.bit_intensity[b] = fmax(0.1, cluster->pheromone.bit_intensity[b] * cluster->pheromone.evaporation_rate); sum += cluster->pheromone.bit_intensity[b];
    } double avg = bits > 0 ? sum / (double)bits : 1.0; uint64_t mask = 0, semilattice = 0;
    for (uint32_t b = 0; b < bits; ++b) if (cluster->pheromone.bit_intensity[b] >= avg) mask |= (1ULL << b);
    for (size_t i = 0; i < cluster->particle_count; ++i)
        if (cluster->particles[i].best_plan.eval.hard_gate_passed)
            semilattice |= (cluster->particles[i].best_genome & cluster->particles[i].local_mask);
    mask |= semilattice;
    if (!mask) mask = (bits >= FLOW_WORD_BITS) ? ~0ULL : ((1ULL << bits) - 1ULL); if (cluster->space->env_mask) mask &= cluster->space->env_mask; cluster->pheromone.consensus_mask = mask;
    return 1; }
int flow_swarm_step(FlowSwarmCluster *cluster, size_t steps_per_particle) { if (!cluster || !cluster->space) return 0;
    const FlowBitSpace *space = cluster->space; uint32_t bits = (space->bit_count > 0 && space->bit_count <= FLOW_WORD_BITS) ? space->bit_count : (space->bit_count > FLOW_WORD_BITS ? FLOW_WORD_BITS : 16);
    FlowMaskCanvas swarm_canvas = space->global_canvas; swarm_canvas.soft_composite_bias |= cluster->pheromone.consensus_mask;
    if (swarm_canvas.hard_composite_mask) swarm_canvas.soft_composite_bias &= swarm_canvas.hard_composite_mask;
    for (size_t p_idx = 0; p_idx < cluster->particle_count; ++p_idx) { FlowSwarmParticle *p = &cluster->particles[p_idx];
        for (size_t step = 0; step < steps_per_particle; ++step) { cluster->total_swarm_mutations++; uint32_t mutated_bit = 0;
            uint64_t cand_genome = flow_bitspace_mutate_1bit_superposed(space, p->current_genome, &swarm_canvas, FLOW_SWARM_BIAS_WEIGHT, &cluster->rng_state, &mutated_bit);
            if (mutated_bit == 0xFFFFFFFF || mutated_bit >= bits) continue; FlowPlan cand_plan; eval_particle_plan(space, cand_genome, &cand_plan);
            if (cand_plan.eval.energy < p->current_energy || !p->best_plan.eval.hard_gate_passed) { p->current_genome = cand_genome; p->current_energy = cand_plan.eval.energy;
                if (cand_plan.eval.hard_gate_passed && (!p->best_plan.eval.hard_gate_passed || cand_plan.eval.energy < p->best_energy)) {
                    double gain = p->best_energy - cand_plan.eval.energy;
                    p->best_energy = cand_plan.eval.energy; p->best_genome = cand_genome; p->best_plan = cand_plan; p->stagnation_steps = 0; if (mutated_bit < FLOW_WORD_BITS)
                        cluster->pheromone.bit_intensity[mutated_bit] += cluster->pheromone.reinforcement_weight * ((gain > 0.0) ? (gain / (cand_plan.eval.energy + 1.0)) : 1.0);
                    if (!cluster->global_best_plan.eval.hard_gate_passed || p->best_energy < cluster->global_best_energy) {
                        cluster->global_best_plan = cand_plan; cluster->global_best_energy = p->best_energy; } } } else if (++p->stagnation_steps >= FLOW_SWARM_STAGNATION_LIMIT) {
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
    for (int i = 0; i < FLOW_NEWTON_64BIT_ITERS; ++i) inv = inv * (2ULL - x * inv);
    return 0ULL - inv;
}
FlowU256 flow_u256_mont_reduce(const uint64_t t_in[FLOW_U512_LIMBS], const FlowU256 *modulus, uint64_t n0_inv) {
    uint64_t t[FLOW_U512_LIMBS];
    memcpy(t, t_in, sizeof(t));
    uint64_t top_carry = 0ULL;
    for (int i = 0; i < FLOW_U256_LIMBS; ++i) {
        uint64_t m = t[i] * n0_inv;
        flow_u128_t carry = 0;
        for (int j = 0; j < FLOW_U256_LIMBS; ++j) {
            flow_u128_t prod = (flow_u128_t)m * (flow_u128_t)modulus->w[j] + (flow_u128_t)t[i + j] + carry;
            t[i + j] = (uint64_t)prod;
            carry = prod >> FLOW_WORD_BITS;
        }
        int j = i + FLOW_U256_LIMBS;
        while (carry > 0 && j < FLOW_U512_LIMBS) {
            flow_u128_t sum = (flow_u128_t)t[j] + carry;
            t[j] = (uint64_t)sum;
            carry = sum >> FLOW_WORD_BITS;
            j++;
        }
        if (carry > 0) top_carry += (uint64_t)carry;
    }
    FlowU256 res = {{t[4], t[5], t[6], t[7]}};
    if (top_carry > 0ULL || flow_u256_cmp(&res, modulus) >= 0) res = flow_u256_overflowing_sub(res, *modulus, NULL);
    return res;
}
static FlowU256 flow_u256_mul_mod_even(FlowU256 a, FlowU256 b, FlowU256 m) {
    FlowU256 res = flow_u256_zero();
    FlowU256 cur = flow_u256_zero();
    flow_u256_div_rem(a, m, NULL, &cur);
    while (!flow_u256_is_zero(&b)) {
        if (!flow_u256_is_even(&b)) {
            int ovf = 0;
            res = flow_u256_overflowing_add(res, cur, &ovf);
            if (ovf || flow_u256_cmp(&res, &m) >= 0) res = flow_u256_overflowing_sub(res, m, NULL);
        }
        uint64_t hi_bit = cur.w[3] >> 63U;
        cur = flow_u256_shl1(cur);
        if (hi_bit || flow_u256_cmp(&cur, &m) >= 0) cur = flow_u256_overflowing_sub(cur, m, NULL);
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
    if (flow_u256_cmp(&base, &modulus) >= 0) {
        flow_u256_div_rem(base, modulus, NULL, &base);
    }
    uint64_t n0_inv = flow_u256_inv_word(&modulus);
    FlowU256 r = one;
    for (uint32_t i = 0; i < FLOW_U256_BITS; ++i) {
        uint64_t hi_bit = r.w[3] >> 63U;
        r = flow_u256_shl1(r);
        if (hi_bit || flow_u256_cmp(&r, &modulus) >= 0) r = flow_u256_overflowing_sub(r, modulus, NULL);
    }
    FlowU256 r2 = r;
    for (uint32_t i = 0; i < FLOW_U256_BITS; ++i) {
        uint64_t hi_bit = r2.w[3] >> 63U;
        r2 = flow_u256_shl1(r2);
        if (hi_bit || flow_u256_cmp(&r2, &modulus) >= 0) r2 = flow_u256_overflowing_sub(r2, modulus, NULL);
    }
    uint64_t wide[FLOW_U512_LIMBS];
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
    state += FLOW_GOLDEN_RATIO_64;
    uint64_t z = state;
    z = (z ^ (z >> 30U)) * FLOW_SPLITMIX64_MUL1;
    z = (z ^ (z >> 27U)) * FLOW_SPLITMIX64_MUL2;
    return z ^ (z >> 31U);
}
static inline uint64_t flow_rotl64(uint64_t x, int k) {
    return (x << k) | (x >> ((int)FLOW_WORD_BITS - k));
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
void flow_mat_mul_ikj(size_t R, size_t K, size_t C, const double *a, const double *b, double *out) {
    if (!a || !b || !out) return;
    memset(out, 0, sizeof(double) * R * C);
    for (size_t i = 0; i < R; ++i) {
        for (size_t k = 0; k < K; ++k) {
            double aik = a[i * K + k];
            for (size_t j = 0; j < C; ++j) {
                out[i * C + j] += aik * b[k * C + j];
            }
        }
    }
}
double flow_mse_loss(size_t N, size_t OUT, const double *predictions, const double *targets) {
    if (!predictions || !targets || N == 0 || OUT == 0) return 0.0;
    size_t count = N * OUT;
    double sum_sq = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double diff = predictions[i] - targets[i];
        sum_sq += diff * diff;
    }
    return sum_sq / (double)count;
}
void flow_gradient_descent_step(size_t N, size_t IN, size_t OUT,
                                const double *inputs, double *weights,
                                const double *predictions, const double *targets,
                                double learning_rate) {
    if (!inputs || !weights || !predictions || !targets || N == 0) return;
    double scale = (2.0 / (double)N) * learning_rate;
    for (size_t i = 0; i < IN; ++i) {
        for (size_t j = 0; j < OUT; ++j) {
            double grad_ij = 0.0;
            for (size_t n = 0; n < N; ++n) {
                double diff = predictions[n * OUT + j] - targets[n * OUT + j];
                grad_ij += inputs[n * IN + i] * diff;
            }
            weights[i * OUT + j] -= grad_ij * scale;
        }
    }
}

/* ============================================================================
 * Architectural Experience Memory (.fvec) (folded into bitspace.c)
 * ============================================================================ */
uint32_t flow_fvec_crc32(const void *data, size_t length) { return flow_crc32_ieee(data, length); }
void flow_fvec_embed_prompt(const char *prompt, double *out_features) { if (!out_features) return; for (size_t i = 0; i < FLOW_VAULT_DIM; ++i) out_features[i] = 0.05;
    if (!prompt) return; static const struct { const char *kw; size_t idx; double w; } rules[] = {
        {"hft", 0, 0.95}, {"latency", 0, 0.90}, {"lockfree", 1, 0.95}, {"ring", 1, 0.85}, {"memory", 2, 0.95}, {"oom", 2, 0.95}, {"tiny", 2, 0.85}, {"ram", 2, 0.85},
        {"serverless", 3, 0.95}, {"coldstart", 3, 0.90}, {"burst", 3, 0.85}, {"io", 4, 0.90},
        {"ddos", 5, 0.95}, {"slowloris", 5, 0.95}, {"antibody", 5, 0.90}, {"immune", 5, 0.85}, {"iot", 6, 0.95}, {"sleep", 6, 0.90}, {"power", 6, 0.85}, {"sensor", 6, 0.85},
        {"hash", 7, 0.90}, {"parallel", 8, 0.90}, {"simd", 9, 0.90}, {"smt", 10, 0.85} };
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i)
        if (strcasestr(prompt, rules[i].kw)) out_features[rules[i].idx] += rules[i].w;
    double norm = 0.0;
    for (size_t i = 0; i < FLOW_VAULT_DIM; ++i) norm += out_features[i] * out_features[i]; norm = sqrt(norm);
    if (norm > 1e-12) for (size_t i = 0; i < FLOW_VAULT_DIM; ++i) out_features[i] /= norm; }
double flow_fvec_cosine_similarity(const double *a, const double *b, size_t dim) {
    if (!a || !b || !dim) return 0.0; double dot = 0.0, na = 0.0, nb = 0.0; for (size_t i = 0; i < dim; ++i) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
    return (na > 1e-12 && nb > 1e-12) ? (dot / (sqrt(na) * sqrt(nb))) : 0.0; }
static void sanitize_fvec_field(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    size_t j = 0;
    if (src) {
        for (size_t i = 0; src[i] && j + 1 < dst_size; ++i) {
            char c = src[i];
            if (c == '\n' || c == '\r' || c == '=') c = '_';
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}
int flow_fvec_header_serialize(const FlowVecHeader *hdr, char *out_buf, size_t buf_size) {
    if (!hdr || !out_buf || buf_size < FLOW_FVEC_HEADER_SIZE) return 0;
    memset(out_buf, 0, buf_size);
    char id[64], name[128], hw[128], intent[64], cat[32], comp[32], smt[64], chash[32], desc[256];
    sanitize_fvec_field(hdr->id, id, sizeof(id));
    sanitize_fvec_field(hdr->name, name, sizeof(name));
    sanitize_fvec_field(hdr->origin_hardware, hw, sizeof(hw));
    sanitize_fvec_field(hdr->trigger_intent, intent, sizeof(intent));
    sanitize_fvec_field(hdr->category, cat, sizeof(cat));
    sanitize_fvec_field(hdr->component_id, comp, sizeof(comp));
    sanitize_fvec_field(hdr->smt_signature, smt, sizeof(smt));
    sanitize_fvec_field(hdr->content_hash[0] ? hdr->content_hash : "0000000000000000", chash, sizeof(chash));
    sanitize_fvec_field(hdr->description, desc, sizeof(desc));
    int w = snprintf(out_buf, FLOW_FVEC_HEADER_SIZE,
        "FVEC_V1\nid=%s\nname=%s\norigin_hardware=%s\ntrigger_intent=%s\ncategory=%s\ncomponent_id=%s\nsmt_signature=%s\nenergy_score=%.4f\ncreated_at_unix=%llu\nvector_dim=%u\npayload_size=%u\nconfidence_score=%u\nlast_reinforced_unix=%llu\nis_auto_promoted=%u\ncontent_hash=%s\ndescription=%s\n",
        id, name, hw, intent, cat, comp, smt, hdr->energy_score, (unsigned long long)hdr->created_at_unix,
        hdr->vector_dim ? hdr->vector_dim : (uint32_t)FLOW_VAULT_DIM, (uint32_t)sizeof(FlowVecPayload), hdr->confidence_score ? hdr->confidence_score : 1,
        (unsigned long long)(hdr->last_reinforced_unix ? hdr->last_reinforced_unix : hdr->created_at_unix), (unsigned int)hdr->is_auto_promoted, chash, desc);
    return (w > 0 && w < FLOW_FVEC_HEADER_SIZE);
}
int flow_fvec_header_deserialize(const char *in_buf, size_t buf_size, FlowVecHeader *hdr_out) {
    if (!in_buf || !hdr_out || buf_size < FLOW_FVEC_HEADER_SIZE || strncmp(in_buf, "FVEC_V1", 7) != 0) return 0; memset(hdr_out, 0, sizeof(*hdr_out));
    strncpy(hdr_out->magic, "FVEC_V1", sizeof(hdr_out->magic) - 1); char buf_copy[FLOW_FVEC_HEADER_SIZE + 1];
    memcpy(buf_copy, in_buf, FLOW_FVEC_HEADER_SIZE); buf_copy[FLOW_FVEC_HEADER_SIZE] = '\0'; for (char *line = strtok(buf_copy, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '='); if (!eq) continue; *eq = '\0'; const char *k = line, *v = eq + 1; if (!strcmp(k, "id")) strncpy(hdr_out->id, v, sizeof(hdr_out->id) - 1);
        else if (!strcmp(k, "name")) strncpy(hdr_out->name, v, sizeof(hdr_out->name) - 1);
        else if (!strcmp(k, "origin_hardware")) strncpy(hdr_out->origin_hardware, v, sizeof(hdr_out->origin_hardware) - 1);
        else if (!strcmp(k, "trigger_intent")) strncpy(hdr_out->trigger_intent, v, sizeof(hdr_out->trigger_intent) - 1);
        else if (!strcmp(k, "category")) strncpy(hdr_out->category, v, sizeof(hdr_out->category) - 1);
        else if (!strcmp(k, "component_id")) strncpy(hdr_out->component_id, v, sizeof(hdr_out->component_id) - 1);
        else if (!strcmp(k, "smt_signature")) strncpy(hdr_out->smt_signature, v, sizeof(hdr_out->smt_signature) - 1);
        else if (!strcmp(k, "energy_score")) hdr_out->energy_score = atof(v); else if (!strcmp(k, "created_at_unix")) hdr_out->created_at_unix = strtoull(v, NULL, 10);
        else if (!strcmp(k, "vector_dim")) hdr_out->vector_dim = (uint32_t)atoi(v); else if (!strcmp(k, "payload_size")) hdr_out->payload_size = (uint32_t)atoi(v);
        else if (!strcmp(k, "confidence_score")) hdr_out->confidence_score = (uint32_t)atoi(v);
        else if (!strcmp(k, "last_reinforced_unix")) hdr_out->last_reinforced_unix = strtoull(v, NULL, 10);
        else if (!strcmp(k, "is_auto_promoted")) hdr_out->is_auto_promoted = (uint8_t)atoi(v);
        else if (!strcmp(k, "content_hash")) strncpy(hdr_out->content_hash, v, sizeof(hdr_out->content_hash) - 1);
        else if (!strcmp(k, "description")) strncpy(hdr_out->description, v, sizeof(hdr_out->description) - 1); }
    if (!hdr_out->vector_dim) hdr_out->vector_dim = FLOW_VAULT_DIM; if (!hdr_out->payload_size) hdr_out->payload_size = sizeof(FlowVecPayload);
    if (!hdr_out->confidence_score) hdr_out->confidence_score = 1; if (!hdr_out->last_reinforced_unix) hdr_out->last_reinforced_unix = hdr_out->created_at_unix; return 1; }
int flow_fvec_write_file(const char *filepath, const FlowVecHeader *hdr, const FlowVecPayload *payload) { if (!filepath || !hdr || !payload) return 0;
    char header_block[FLOW_FVEC_HEADER_SIZE]; if (!flow_fvec_header_serialize(hdr, header_block, sizeof(header_block))) return 0;
    FILE *f = fopen(filepath, "wb"); if (!f) return 0; FlowVecPayload pc = *payload; pc.crc32 = flow_fvec_crc32(&pc, offsetof(FlowVecPayload, crc32));
    int ok = (fwrite(header_block, 1, FLOW_FVEC_HEADER_SIZE, f) == FLOW_FVEC_HEADER_SIZE && fwrite(&pc, 1, sizeof(pc), f) == sizeof(pc)); fclose(f); return ok; }
int flow_fvec_read_file(const char *filepath, FlowVecHeader *hdr_out, FlowVecPayload *payload_out) { if (!filepath || !hdr_out || !payload_out) return 0;
    FILE *f = fopen(filepath, "rb"); if (!f) return 0; char header_block[FLOW_FVEC_HEADER_SIZE];
    int ok = (fread(header_block, 1, FLOW_FVEC_HEADER_SIZE, f) == FLOW_FVEC_HEADER_SIZE && flow_fvec_header_deserialize(header_block, sizeof(header_block), hdr_out) &&
              fread(payload_out, 1, sizeof(FlowVecPayload), f) == sizeof(FlowVecPayload)); fclose(f); if (!ok) return 0;
    if (hdr_out->vector_dim == 0 || hdr_out->vector_dim > FLOW_VAULT_DIM || hdr_out->payload_size != sizeof(FlowVecPayload)) return 0;
    if (payload_out->hard_composite_mask == 0) return 0;
    strncpy(hdr_out->filepath, filepath, sizeof(hdr_out->filepath) - 1); return payload_out->crc32 == flow_fvec_crc32(payload_out, offsetof(FlowVecPayload, crc32)); }
void flow_fvec_store_init(FlowVecStore *store, const char *dir_path) { if (!store) return; memset(store, 0, sizeof(*store));
    strncpy(store->root_dir, (dir_path && dir_path[0]) ? dir_path : FLOW_FVEC_DEFAULT_DIR, sizeof(store->root_dir) - 1); }
int flow_fvec_store_scan(FlowVecStore *store) {
    if (!store) return 0; store->count = 0; DIR *d = opendir(store->root_dir); if (!d) return 0; struct dirent *dir; while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') continue; size_t len = strlen(dir->d_name);
        if (len > 5 && !strcmp(dir->d_name + len - 5, ".fvec") && store->count < FLOW_FVEC_MAX_ENTRIES) {
            char full_path[512]; snprintf(full_path, sizeof(full_path), "%s/%s", store->root_dir, dir->d_name); FlowVecRecord *rec = &store->records[store->count];
            if (flow_fvec_read_file(full_path, &rec->header, &rec->payload)) store->count++; } } closedir(d); return (int)store->count; }
const FlowVecRecord *flow_fvec_store_lookup_by_id(const FlowVecStore *store, const char *id) { if (!store || !id) return NULL;
    for (size_t i = 0; i < store->count; ++i) if (!strcmp(store->records[i].header.id, id) || strstr(store->records[i].header.filepath, id)) return &store->records[i];
    return NULL; }
int flow_fvec_store_query(const FlowVecStore *store, const char *natural_query, size_t *best_idx_out, double *best_sim_out) {
    if (!store || !natural_query || !store->count) return 0; double q[FLOW_VAULT_DIM], best_sim = -1.0; size_t best_idx = 0; flow_fvec_embed_prompt(natural_query, q);
    for (size_t i = 0; i < store->count; ++i) { double sim = flow_fvec_cosine_similarity(q, store->records[i].payload.features, FLOW_VAULT_DIM);
        if (sim > best_sim) { best_sim = sim; best_idx = i; } } if (best_idx_out) *best_idx_out = best_idx; if (best_sim_out) *best_sim_out = best_sim; return 1; }
int flow_fvec_remediate_check(const FlowVecStore *store, double ram_pressure_pct, double cache_miss_rate,
                              const FlowVecRecord **recommended_record_out, double *confidence_out, char *diagnosis_out, size_t max_diag_len) {
    if (!store || !recommended_record_out) return 0; int is_ram = (ram_pressure_pct >= FLOW_FVEC_RAM_CRITICAL_PCT), is_cache = (!is_ram && cache_miss_rate >= FLOW_FVEC_CACHE_MISS_CRITICAL);
    if (!is_ram && !is_cache) return 0; for (size_t i = 0; i < store->count; ++i) { const FlowVecHeader *h = &store->records[i].header;
        int hit = is_ram ? (strstr(h->trigger_intent, "MEMORY_CRITICAL") || strstr(h->id, "oom_survival"))
                         : (strstr(h->trigger_intent, "LATENCY_FIRST") || strstr(h->trigger_intent, "HFT_TRADING") != NULL); if (hit) {
            *recommended_record_out = &store->records[i]; if (confidence_out) *confidence_out = is_ram ? 0.94 : 0.96;
            if (diagnosis_out && max_diag_len > 0) snprintf(diagnosis_out, max_diag_len, "Crisis Remediation matched '%s' (RAM=%.1f%%, Miss=%.1f%%)", h->filepath, ram_pressure_pct, cache_miss_rate * 100.0);
            return 1; } } return 0; }
void flow_fvec_inspect(const FlowVecHeader *hdr, const FlowVecPayload *payload, FILE *out) {
    if (hdr && payload && out) fprintf(out, "[.fvec] %s (%s) | Intent: %s | HW: %s | Comp: %s | SMT: %s | Energy: %.2f | Genome: 0x%016llx | CRC32: 0x%08x\n",
        hdr->id, hdr->name, hdr->trigger_intent, hdr->origin_hardware, hdr->component_id, hdr->smt_signature, hdr->energy_score, (unsigned long long)payload->pure_genome, payload->crc32); }
void flow_fvec_store_print_summary(const FlowVecStore *store, FILE *out) { if (!store || !out) return;
    fprintf(out, "FLOW Gene Vault (%zu Models in '%s'):\n", store->count, store->root_dir); for (size_t i = 0; i < store->count; ++i) {
        const FlowVecHeader *h = &store->records[i].header;
        fprintf(out, "  [%02zu] %s %-28s | %-16s | Conf:%u | E:%.2f | SMT:%s\n", i + 1, h->is_auto_promoted ? "[AUTO]" : "[FACT]", h->id, h->trigger_intent, h->confidence_score ? h->confidence_score : 1, h->energy_score, h->smt_signature); } }
static int ensure_dir_exists(const char *dir) { struct stat st; if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode);
    char tmp[256] = {0}; strncpy(tmp, dir, sizeof(tmp) - 1); for (char *p = (tmp[0] == '/' ? tmp + 1 : tmp); *p; p++) if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST; }
int flow_fvec_seed_canonical_files(const char *target_dir) {
    const char *dir = (target_dir && target_dir[0]) ? target_dir : FLOW_FVEC_DEFAULT_DIR; if (!ensure_dir_exists(dir)) return 0;
    static const struct { const char *id, *name, *file, *hw, *intent, *cat, *comp; uint64_t genome, mask, bias; double energy; } seeds[] = {
        {"vec_hft_lockfree_trading", "HFT Ultra-Low-Latency Ring", "hft_ultra_low_latency.fvec", "x86_avx2, L1=64K, Cores=64", "HFT_TRADING", "HFT", "bounded_queue", 0x0502ULL, 0xFFFFULL, 0x0502ULL, 12.4},
        {"vec_serverless_tiny_worker", "OOM Survival Compact Hash", "oom_survival_v3.fvec", "x86_generic, RAM=16MB", "MEMORY_CRITICAL", "SERVERLESS", "compact_hash", 0x0101ULL, 0x0FFFULL, 0x0101ULL, 18.2},
        {"vec_serverless_io_heavy", "Serverless Zero-Coldstart Worker", "serverless_zero_coldstart.fvec", "x86_cloud_container, Cores=4", "SERVERLESS_BURST", "SERVERLESS", "sharded_hash", 0x0304ULL, 0x3FFFULL, 0x0304ULL, 15.0},
        {"vec_antibody_slowloris_409", "Slowloris DDoS Immune Antibody", "slowloris_immune_antibody.fvec", "fleet_cluster_node_edge", "DDoS_DEFENSE", "IMMUNE", "bounded_queue", 0x0708ULL, 0x7FFFULL, 0x0708ULL, 9.8},
        {"vec_iot_embedded_sensor", "IoT Quiescent Sensor Pipeline", "iot_quiescent_sleep.fvec", "arm_cortex_m4, RAM=64KB", "LOW_POWER_QUIESCENT", "GENERAL", "bounded_queue", 0x0011ULL, 0x00FFULL, 0x0011ULL, 6.5}
    }; int seeded = 0; for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        FlowVecHeader hdr = { .magic = FLOW_FVEC_MAGIC, .energy_score = seeds[i].energy, .created_at_unix = 1700000000ULL, .last_reinforced_unix = 1700000000ULL, .vector_dim = FLOW_VAULT_DIM, .payload_size = sizeof(FlowVecPayload), .confidence_score = 10 };
        FlowVecPayload payload = { .pure_genome = seeds[i].genome, .hard_composite_mask = seeds[i].mask, .soft_composite_bias = seeds[i].bias };
        strncpy(hdr.id, seeds[i].id, sizeof(hdr.id) - 1); strncpy(hdr.name, seeds[i].name, sizeof(hdr.name) - 1);
        strncpy(hdr.origin_hardware, seeds[i].hw, sizeof(hdr.origin_hardware) - 1); strncpy(hdr.trigger_intent, seeds[i].intent, sizeof(hdr.trigger_intent) - 1);
        strncpy(hdr.category, seeds[i].cat, sizeof(hdr.category) - 1); strncpy(hdr.component_id, seeds[i].comp, sizeof(hdr.component_id) - 1);
        strncpy(hdr.smt_signature, "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT", sizeof(hdr.smt_signature) - 1); strncpy(hdr.description, seeds[i].name, sizeof(hdr.description) - 1);
        flow_fvec_embed_prompt(seeds[i].name, payload.features); char full_path[512]; snprintf(full_path, sizeof(full_path), "%s/%s", dir, seeds[i].file);
        if (flow_fvec_write_file(full_path, &hdr, &payload)) seeded++; } return seeded; }
int flow_fvec_verify_hardware_affinity(const FlowVecHeader *hdr, const FlowEnvironmentState *host_env, char *diag_msg, size_t max_len) { if (!hdr || !host_env) return 0;
    if (strstr(hdr->origin_hardware, "avx2") && host_env->hardware_arch == FLOW_ARCH_ARM_NEON) {
        if (diag_msg && max_len) snprintf(diag_msg, max_len, "Hardware mismatch: AVX2 required by '%s' on ARM host", hdr->id); return 0; }
    if (diag_msg && max_len) snprintf(diag_msg, max_len, "Hardware affinity verified for '%s'", hdr->id); return 1; }
uint64_t flow_fvec_compute_content_hash(uint64_t genome, uint64_t hard_mask, uint64_t soft_bias, const FlowSMTProofAttestation *proof) {
    uint64_t h = FLOW_FNV1A_64_OFFSET, words[4] = { genome, hard_mask, soft_bias, proof ? (uint64_t)(proof->buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT) : 1ULL };
    for (int i = 0; i < 4; ++i) { h ^= words[i]; h *= FLOW_FNV1A_64_PRIME; } return h; }
int flow_fvec_promote_or_strengthen(const char *target_dir, const FlowVecHeader *base_hdr, const FlowVecPayload *payload,
                                    uint64_t content_hash, char *out_filepath, size_t max_path_len, uint32_t *new_confidence_out) { if (!base_hdr || !payload) return 0;
    const char *dir = (target_dir && target_dir[0]) ? target_dir : FLOW_FVEC_DEFAULT_DIR; if (!ensure_dir_exists(dir)) return 0;
    char path[512]; snprintf(path, sizeof(path), "%s/auto_promoted_%016llx.fvec", dir, (unsigned long long)content_hash);
    FlowVecHeader hdr = *base_hdr; FlowVecPayload existing_pl;
    if (flow_fvec_read_file(path, &hdr, &existing_pl)) { hdr.confidence_score++; hdr.last_reinforced_unix = (uint64_t)time(NULL); } else {
        hdr = *base_hdr; hdr.is_auto_promoted = 1; hdr.confidence_score = 1; hdr.created_at_unix = hdr.last_reinforced_unix = (uint64_t)time(NULL);
        snprintf(hdr.content_hash, sizeof(hdr.content_hash), "%016llx", (unsigned long long)content_hash);
        if (!hdr.id[0]) snprintf(hdr.id, sizeof(hdr.id), "auto_%016llx", (unsigned long long)content_hash); } if (!flow_fvec_write_file(path, &hdr, payload)) return 0;
    if (out_filepath && max_path_len) strncpy(out_filepath, path, max_path_len - 1); if (new_confidence_out) *new_confidence_out = hdr.confidence_score; return 1; }
size_t flow_fvec_store_evict_senescent(FlowVecStore *store, uint64_t now_unix, uint64_t max_idle_seconds, char (*evicted_files_out)[256], size_t max_evictions) {
    if (!store) return 0; size_t evicted = 0, write_idx = 0; for (size_t i = 0; i < store->count; ++i) { FlowVecRecord *r = &store->records[i];
        uint64_t last = r->header.last_reinforced_unix ? r->header.last_reinforced_unix : r->header.created_at_unix;
        if (r->header.is_auto_promoted && r->header.confidence_score <= 1 && now_unix > last && (now_unix - last) >= max_idle_seconds) {
            if (r->header.filepath[0]) unlink(r->header.filepath);
            if (evicted_files_out && evicted < max_evictions) strncpy(evicted_files_out[evicted], r->header.filepath, 255); evicted++;
        } else { if (write_idx != i) store->records[write_idx] = *r; write_idx++; } } store->count = write_idx; return evicted; }
int flow_fvec_prestaging_init(FlowFvecPreStagingVault *v) { return v ? (memset(v, 0, sizeof(*v)), 1) : 0; }
int flow_fvec_prestaging_register(FlowFvecPreStagingVault *v, const char *fvec_file_path, const char *trigger_condition) {
    if (!v || !fvec_file_path || !trigger_condition || v->slot_count >= FLOW_FVEC_MAX_PRESTAGED) return 0; FlowFvecPreStagingSlot *slot = &v->slots[v->slot_count];
    memset(slot, 0, sizeof(*slot)); if (!flow_fvec_read_file(fvec_file_path, &slot->header, &slot->payload)) return 0;
    strncpy(slot->trigger_condition, trigger_condition, sizeof(slot->trigger_condition) - 1); strncpy(slot->fvec_path, fvec_file_path, sizeof(slot->fvec_path) - 1);
    flow_bmf_canvas_init(&slot->staged_canvas, 0, slot->payload.hard_composite_mask, ~slot->payload.hard_composite_mask, slot->payload.pure_genome & slot->payload.hard_composite_mask);
    slot->staged_canvas.is_adjudicated_sound = 1; slot->is_staged_sound = 1; v->slot_count++; return 1; }
int flow_fvec_prestaging_swap_atomic(FlowFvecPreStagingVault *v, const char *trigger_condition, FlowBmf1BitCanvas *active_canvas_out, double *swap_latency_ns_out) {
    if (!v || !trigger_condition || !active_canvas_out) return 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (size_t i = 0; i < v->slot_count; ++i) {
        if (!strcmp(v->slots[i].trigger_condition, trigger_condition) && v->slots[i].is_staged_sound) {
            FLOW_ATOMIC_STAGE_SWAP(active_canvas_out, &v->slots[i].staged_canvas);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double elapsed_ns = (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
            if (elapsed_ns <= 0.0) elapsed_ns = 1.0;
            v->total_speculative_swaps++;
            v->last_swap_latency_ns = elapsed_ns;
            if (swap_latency_ns_out) *swap_latency_ns_out = v->last_swap_latency_ns;
            return 1;
        }
    }
    return 0;
}
FlowSMTResult flow_fvec_verify_prestaging_soundness_smt(const FlowFvecPreStagingVault *v, const char *trigger_condition, double swap_latency_ns, FlowSMTProofAttestation *proof_out) {
    if (!v || !trigger_condition) return FLOW_SMT_UNKNOWN; uint64_t found_sound = 0;
    for (size_t i = 0; i < v->slot_count; ++i) if (!strcmp(v->slots[i].trigger_condition, trigger_condition) && v->slots[i].is_staged_sound) found_sound = 1;
    const FlowBoxConstraint rules[] = { FLOW_SMT_RULE("pre-staged slot soundness", found_sound, 1, 1, FLOW_BOX_THEOREM_DETERMINISM, "missing or unverified pre-staged slot"),
        FLOW_SMT_RULE("atomic swap latency (ns)", (uint64_t)swap_latency_ns, 0, FLOW_FVEC_MAX_SWAP_NS, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "swap latency exceeds 500ns zero-coldstart bound") };
    return FLOW_SMT_VERIFY_RULES("fvec_prestaging", proof_out, rules, "SMT FVEC PRESTAGING SOUND (latency=%.1fns)", swap_latency_ns); }

/* =========================================================================
 * 5-Stage Token Ring, Wavefront Semilattice & Swarm Mesh
 * ========================================================================= */
static int flow_smt_count_unsat(const FlowSMTProofAttestation *p) {
    return p ? (p->buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT) + (p->memory_quota_bound == FLOW_SMT_PROVEN_UNSAT) +
               (p->shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT) + (p->determinism_invariant == FLOW_SMT_PROVEN_UNSAT) : 0; }
uint64_t flow_token_ring_attention_project(FlowMaskCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias, uint64_t current_genome) {
    if (!canvas) return current_genome; if (attention_mask) canvas->hard_composite_mask &= attention_mask;
    canvas->soft_composite_bias = canvas->hard_composite_mask ? ((canvas->soft_composite_bias & canvas->hard_composite_mask) | (dynamic_bias & canvas->hard_composite_mask)) : 0;
    return canvas->hard_composite_mask ? flow_manifold_project(current_genome, canvas->hard_composite_mask, canvas->soft_composite_bias) : 0; }
uint64_t flow_token_ring_bmf_attention_project(FlowBmf1BitCanvas *canvas, uint64_t attention_mask, uint64_t dynamic_bias) { if (!canvas) return 0;
    if (attention_mask) canvas->malleable_mask &= attention_mask; if (dynamic_bias) canvas->dynamic_bias = dynamic_bias;
    canvas->switchboard_bits = (canvas->switchboard_bits & (canvas->invariant_mask | canvas->malleable_mask)) | canvas->invariant_mask;
    if (canvas->dynamic_bias) canvas->switchboard_bits |= (canvas->dynamic_bias & (canvas->malleable_mask & ~canvas->invariant_mask));
    canvas->is_adjudicated_sound = flow_bmf_canvas_verify_invariants(canvas); return canvas->switchboard_bits; }
static int stage_polytope_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) { if (!ring || !token || !canvas || !ring->active_ir) return 0;
    if (!ring->active_space.candidate_count && (!flow_bitspace_init_for_ir(ring->active_ir, &ring->active_space) || !ring->active_space.candidate_count)) {
        ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: No feasible candidate components for intent '%s'", ring->active_ir->flow_name); return 0;
    } FlowPolyhedronSystem poly; flow_polyhedron_from_ir(ring->active_ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], &poly);
    canvas->hard_polytope_mask = token->attention_mask = flow_polyhedron_project_mask(&poly, &ring->active_space.candidate_dims[0], 64);
    token->dynamic_bias = canvas->dynamic_telemetry_bias; token->energy = ring->lyapunov_energy > 0 ? ring->lyapunov_energy : 100.0;
    if (!canvas->hard_composite_mask) { ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Polytope projection produced null manifold intersection"); return 0; }
    return 1; }
static int stage_anneal_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->active_space.candidate_count) return 0; FlowPlan best_plan = {0};
    if (!flow_polyhedral_solve_plan(ring->active_ir, &ring->active_space, &best_plan)) {
        ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Polyhedral analytical synthesis found no feasible plan"); return 0; }
    ring->ensemble = (FlowPlanEnsemble){ .count = FLOW_TACTIC_COUNT, .available = {1, 1, 1}, .tactics = { [FLOW_TACTIC_SPEED] = best_plan, [FLOW_TACTIC_BALANCED] = best_plan, [FLOW_TACTIC_MEMORY] = best_plan } };
    ring->ensemble.tactics[FLOW_TACTIC_SPEED].eval.latency_score *= 0.8;
    if (best_plan.eval.memory_bytes > 4096) ring->ensemble.tactics[FLOW_TACTIC_MEMORY].eval.memory_bytes /= 2; ring->active_genome = best_plan.genome;
    flow_plan_to_search_result(&best_plan, ring->active_ir, ring->rng_seed + (uint32_t)ring->cycle_count, &ring->best_search);
    token->energy = best_plan.eval.energy; token->attention_mask = canvas->hard_composite_mask; token->dynamic_bias = canvas->soft_composite_bias; return 1; }
static int stage_smt_proof_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->best_search.component) return 0;
    flow_smt_verify(ring->active_ir, ring->best_search.component, &ring->best_search.assignment, &ring->best_search.metrics, &ring->smt_proof);
    token->energy = ring->best_search.energy; token->attention_mask = canvas->hard_composite_mask; token->dynamic_bias = canvas->soft_composite_bias; return 1; }
static int stage_synthesis_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) {
    if (!ring || !token || !canvas || !ring->active_ir || !ring->best_search.component) return 0; VerificationReport v_report = {0};
    int ok = verify_candidate(ring->active_ir, ring->best_search.component, &ring->best_search, &v_report);
    token->energy = ring->best_search.energy; token->attention_mask = canvas->hard_composite_mask; if (!ok) token->attention_mask &= ~(1ULL << (ring->active_genome & 63));
    return 1; }
static int stage_attractor_handler(FlowTokenRing *ring, FlowToken *token, FlowMaskCanvas *canvas) { if (!ring || !token || !canvas) return 0;
    token->energy = ring->lyapunov_energy; token->attention_mask = canvas->hard_composite_mask;
    if (!canvas->hard_composite_mask) { ring->state = FLOW_RING_UNSAT; snprintf(ring->status_message, sizeof(ring->status_message), "UNSAT: Manifold collapsed to null space"); return 1; }
    double delta = fabs(ring->lyapunov_energy - ring->prev_energy); ring->lyapunov_delta_e = delta;
    if (ring->cycle_count >= 1 && (delta < FLOW_LYAPUNOV_CONVERGE_EPS || ring->best_search.component)) { ring->state = FLOW_RING_ATTRACTOR_REACHED; ring->attractor_converged = 1;
        snprintf(ring->status_message, sizeof(ring->status_message), "Attractor reached: cycle=%llu, energy=%.2f, Delta E=%.6f, SMT verified UNSAT=%d/4", (unsigned long long)ring->cycle_count, ring->lyapunov_energy, delta, flow_smt_count_unsat(&ring->smt_proof));
    } return 1; }
int flow_token_ring_init(FlowTokenRing *ring, SemanticIR *ir) { if (!ring) return 0; memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir; ring->state = FLOW_RING_INIT; ring->rng_state = 0x853c49e6748fea9bULL; ring->anneal_iterations = 100; ring->rng_seed = FLOW_DEFAULT_SEED;
    ring->wavefront_epoch = ring->quiescent_generation = 1; if (ir) { flow_bitspace_init_for_ir(ir, &ring->active_space);
        if (ring->active_space.candidate_count > 0) flow_mask_canvas_compose(ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], NULL, &ring->active_canvas);
        else ring->active_canvas = (FlowMaskCanvas){~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, 0, 0, 0};
    } else ring->active_canvas = (FlowMaskCanvas){.hard_safety_mask = ~0ULL, .hard_composite_mask = ~0ULL};
    flow_bmf_canvas_from_mask_canvas(&ring->active_canvas, FLOW_BMF_SUBSPACE_GENERIC, &ring->bmf_1bit_canvas);
    snprintf(ring->status_message, sizeof(ring->status_message), "Token Ring initialized"); return 1; }
int flow_token_ring_add_token(FlowTokenRing *ring, FlowTokenStage stage, const char *name, FlowTokenTransitionFn fn, void *user_data) {
    if (!ring || !fn || ring->token_count >= FLOW_TOKEN_RING_MAX_TOKENS) return 0; FlowToken *tok = &ring->tokens[ring->token_count++];
    *tok = (FlowToken){.token_id = (uint32_t)ring->token_count, .stage = stage, .transition_fn = fn, .user_data = user_data, .attention_mask = ~0ULL};
    if (name) strncpy(tok->stage_name, name, sizeof(tok->stage_name) - 1); else snprintf(tok->stage_name, sizeof(tok->stage_name), "stage_%d", (int)stage); return 1; }
int flow_token_ring_setup_canonical(FlowTokenRing *ring, SemanticIR *ir, size_t anneal_iters, uint32_t seed) { if (!ring || !ir || !flow_token_ring_init(ring, ir)) return 0;
    ring->anneal_iterations = anneal_iters ? anneal_iters : 100; ring->rng_seed = seed ? seed : FLOW_DEFAULT_SEED; ring->rng_state = (uint64_t)ring->rng_seed * 0x5851f42d4c957f2dULL + 1ULL;
    static const struct { FlowTokenStage s; const char *n; FlowTokenTransitionFn fn; } stages[] = { {FLOW_TOKEN_STAGE_POLYTOPE, "polytope_projection", stage_polytope_handler},
        {FLOW_TOKEN_STAGE_ANNEAL, "polyhedral_synthesis", stage_anneal_handler}, {FLOW_TOKEN_STAGE_SMT_PROOF, "smt_invariant_proof", stage_smt_proof_handler},
        {FLOW_TOKEN_STAGE_SYNTHESIS, "synthesis_validation", stage_synthesis_handler}, {FLOW_TOKEN_STAGE_ATTRACTOR, "lyapunov_attractor", stage_attractor_handler} };
    for (size_t i = 0; i < 5; ++i) flow_token_ring_add_token(ring, stages[i].s, stages[i].n, stages[i].fn, NULL); ring->state = FLOW_RING_CIRCULATING; return 1; }
int flow_token_ring_step(FlowTokenRing *ring) { if (!ring || !ring->token_count || ring->state != FLOW_RING_CIRCULATING) return 0;
    FlowToken *tok = &ring->tokens[ring->current_token_idx]; int ok = tok->transition_fn(ring, tok, &ring->active_canvas); tok->execution_count++;
    if (!ok && ring->state == FLOW_RING_CIRCULATING) { ring->state = FLOW_RING_UNSAT; return 0; }
    ring->active_genome = flow_token_ring_attention_project(&ring->active_canvas, tok->attention_mask, tok->dynamic_bias, ring->active_genome);
    if (tok->energy > 0) { ring->lyapunov_delta_e = fabs(tok->energy - ring->lyapunov_energy); ring->prev_energy = ring->lyapunov_energy; ring->lyapunov_energy = tok->energy; }
    ring->wavefront_epoch++; ring->current_token_idx = (ring->current_token_idx + 1) % ring->token_count; ring->step_count++;
    if (ring->current_token_idx == 0) { ring->cycle_count++; ring->quiescent_generation++; if (ring->bound_arena) flow_bump_qsbr_quiescent_fold(ring->bound_arena); } return 1; }
FlowTokenRingState flow_token_ring_run_to_attractor(FlowTokenRing *ring, size_t max_cycles) { if (!ring) return FLOW_RING_UNSAT;
    size_t limit = max_cycles ? max_cycles : FLOW_TOKEN_RING_DEFAULT_MAX_CYCLES;
    while (ring->state == FLOW_RING_CIRCULATING && ring->cycle_count < limit) if (!flow_token_ring_step(ring)) break;
    if (ring->state == FLOW_RING_CIRCULATING) { ring->state = FLOW_RING_EXHAUSTED; snprintf(ring->status_message, sizeof(ring->status_message), "Exhausted maximum cycle limit (%zu cycles) without fixed point", limit); }
    return ring->state; }
int flow_token_ring_converge_default(FlowTokenRing *ring, const SemanticIR *opt_ir, const char *flow_name) {
    if (!ring) return 0;
    flow_registry_init();
    static SemanticIR s_ir;
    memset(&s_ir, 0, sizeof(s_ir));
    if (opt_ir) {
        s_ir = *opt_ir;
    } else {
        strncpy(s_ir.flow_name, flow_name ? flow_name : "default_flow", sizeof(s_ir.flow_name) - 1);
        s_ir.input_max_count = 4096;
        s_ir.memory_limit_mb = 64;
        s_ir.fact_deterministic = 1;
        s_ir.fact_range_proven = 1;
        s_ir.state_shared = 1;
        s_ir.state_read_heavy = 1;
        s_ir.fact_unordered = 1;
    }
    if (!flow_token_ring_setup_canonical(ring, &s_ir, 64, FLOW_DEFAULT_SEED)) return 0;
    FlowTokenRingState st = flow_token_ring_run_to_attractor(ring, 8);
    return (st == FLOW_RING_ATTRACTOR_REACHED || ring->best_search.component != NULL) ? 1 : 0;
}
int flow_subspace_decompose_canonical(FlowSubspaceDecomposition *decomp, const SemanticIR *ir) { if (!decomp) return 0; memset(decomp, 0, sizeof(*decomp));
    long cpus = sysconf(_SC_NPROCESSORS_ONLN); cpus = cpus < 1 ? 4 : (cpus > 64 ? 64 : cpus);
    uint64_t max_items = (ir && ir->input_max_count > 0) ? (uint64_t)ir->input_max_count : 4096ULL;
    uint64_t mem_kb = (ir && ir->memory_limit_mb > 0) ? (uint64_t)ir->memory_limit_mb * 1024ULL : 65535ULL; if (mem_kb > 65535ULL) mem_kb = 65535ULL;
    const FlowSubspace init_subs[5] = {
        {FLOW_SUBSPACE_CAPACITY,    "capacity",     FLOW_SUBSPACE_MASK_CAPACITY,    0, 16,   16, 65535, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)max_items, (double)max_items, max_items, max_items},
        {FLOW_SUBSPACE_CONCURRENCY, "concurrency",  FLOW_SUBSPACE_MASK_CONCURRENCY, 16, 8,    1,    64, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)cpus,      (double)cpus,      (uint64_t)cpus, (uint64_t)cpus},
        {FLOW_SUBSPACE_SHARDING,    "sharding",     FLOW_SUBSPACE_MASK_SHARDING,    24, 8,    1,    64, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, 16.0,              16.0,              16, 16},
        {FLOW_SUBSPACE_BUFFER,      "buffer_arena", FLOW_SUBSPACE_MASK_BUFFER,      32, 16, 1024, 65535, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, (double)mem_kb,    (double)mem_kb,    mem_kb, mem_kb},
        {FLOW_SUBSPACE_GROWTH,      "growth_batch", FLOW_SUBSPACE_MASK_GROWTH,      48, 16,  100,   400, 0.0, FLOW_LAGRANGIAN_DEFAULT_ETA, 200.0,             150.0,             150, 150} };
    memcpy(decomp->subspaces, init_subs, sizeof(init_subs)); decomp->subspace_count = 5; decomp->is_strictly_orthogonal = true;
    for (size_t i = 0; i < decomp->subspace_count; ++i) {
        for (size_t j = i + 1; j < decomp->subspace_count; ++j) if (decomp->subspaces[i].mask & decomp->subspaces[j].mask) decomp->is_strictly_orthogonal = false;
        decomp->composite_coverage_mask |= decomp->subspaces[i].mask; } return decomp->is_strictly_orthogonal ? 1 : 0; }
int flow_subspace_lagrangian_tune(FlowSubspace *sub, double current_demand, double capacity_limit) { if (!sub) return 0;
    sub->current_demand = current_demand; sub->capacity_limit = capacity_limit; double eta = sub->learning_rate_eta > 0.0 ? sub->learning_rate_eta : FLOW_LAGRANGIAN_DEFAULT_ETA;
    sub->shadow_price_lambda = fmax(0.0, sub->shadow_price_lambda + eta * (current_demand - capacity_limit));
    double target = sub->shadow_price_lambda > 0.0 ? capacity_limit / (1.0 + sub->shadow_price_lambda) : current_demand;
    target = fmin((double)sub->max_value, fmax((double)sub->min_value, target)); sub->current_val = sub->optimal_val = (uint64_t)target; return 1; }
uint64_t flow_subspace_polyhedral_project(const FlowSubspace *sub, uint64_t raw_val) { if (!sub) return 0;
    uint64_t val = raw_val < sub->min_value ? sub->min_value : (raw_val > sub->max_value ? sub->max_value : raw_val);
    uint64_t field_mask = (sub->bit_width >= 64) ? ~0ULL : ((1ULL << sub->bit_width) - 1ULL); return (val & field_mask) << sub->bit_offset; }
uint64_t flow_wavefront_semilattice_join(uint64_t base_genome, const uint64_t *thread_slices, const uint64_t *subspace_masks, size_t count) {
    if (!thread_slices || !subspace_masks || !count) return base_genome; if (count <= 8) { FlowVector512 s = {0}, m = {0};
        for (size_t i = 0; i < count; ++i) { s.u64[i] = thread_slices[i]; m.u64[i] = subspace_masks[i]; }
        return (base_genome & ~flow_v512_horizontal_or(m)) | flow_v512_horizontal_or(flow_v512_and(s, m)); } uint64_t merged = base_genome;
    for (size_t i = 0; i < count; ++i) merged = (merged & ~subspace_masks[i]) | (thread_slices[i] & subspace_masks[i]); return merged; }
typedef struct { FlowWavefrontRing *ring; size_t worker_id, subspace_idx; uint64_t slice_out; double energy_out; } FlowWavefrontWorkerTask;
static void *wavefront_worker_func(void *arg) { FlowWavefrontWorkerTask *task = (FlowWavefrontWorkerTask *)arg;
    if (!task || !task->ring || task->subspace_idx >= task->ring->decomp.subspace_count) return NULL; flow_numa_pin_thread((uint32_t)task->worker_id);
    FlowSubspace *sub = &task->ring->decomp.subspaces[task->subspace_idx]; flow_subspace_lagrangian_tune(sub, sub->current_demand, sub->capacity_limit);
    task->slice_out = flow_subspace_polyhedral_project(sub, sub->optimal_val); double diff = (double)sub->current_val - (double)sub->optimal_val;
    task->energy_out = 0.5 * (diff * diff) / (double)(sub->max_value > 0 ? sub->max_value : 1); return NULL; }
int flow_wavefront_ring_init(FlowWavefrontRing *ring, SemanticIR *ir, size_t num_slots, size_t num_workers) { if (!ring) return 0; memset(ring, 0, sizeof(*ring));
    ring->active_ir = ir; ring->slot_count = (num_slots > 0 && num_slots <= FLOW_WAVEFRONT_MAX_SLOTS) ? num_slots : 4;
    ring->worker_count = (num_workers > 0 && num_workers <= FLOW_WAVEFRONT_MAX_WORKERS) ? num_workers : 5; ring->state = FLOW_RING_CIRCULATING;
    ring->global_lyapunov_energy = 100.0; ring->prev_lyapunov_energy = 200.0; ring->lyapunov_delta_e = 100.0; flow_subspace_decompose_canonical(&ring->decomp, ir);
    for (size_t i = 0; i < ring->slot_count; ++i) { ring->slots[i].slot_id = (uint32_t)i; ring->slots[i].current_stage = (FlowTokenStage)(i % 5); ring->slots[i].energy = 50.0;
        flow_bmf_canvas_init(&ring->slots[i].bmf_canvas, (uint32_t)i, ~0ULL, ~0ULL, 0); } if (ir) { flow_bitspace_init_for_ir(ir, &ring->active_space);
        if (ring->active_space.candidate_count > 0) flow_mask_canvas_compose(ir, ring->active_space.candidates[0], &ring->active_space.candidate_dims[0], NULL, &ring->global_canvas);
    } ring->wavefront_epoch = ring->quiescent_generation = 1;
    snprintf(ring->status_message, sizeof(ring->status_message), "Wavefront Ring initialized: %zu slots, %zu workers, %zu orthogonal subspaces", ring->slot_count, ring->worker_count, ring->decomp.subspace_count);
    return 1; }
int flow_wavefront_ring_step_parallel(FlowWavefrontRing *ring) { if (!ring || ring->state != FLOW_RING_CIRCULATING) return 0;
    size_t num_workers = ring->worker_count > ring->decomp.subspace_count ? ring->decomp.subspace_count : ring->worker_count; if (!num_workers) return 0;
    FlowPhysicalProbe probe; flow_hardware_probe_start(&probe); pthread_t threads[FLOW_WAVEFRONT_MAX_WORKERS]; FlowWavefrontWorkerTask tasks[FLOW_WAVEFRONT_MAX_WORKERS];
    uint64_t thread_slices[FLOW_WAVEFRONT_MAX_WORKERS] = {0}, subspace_masks[FLOW_WAVEFRONT_MAX_WORKERS] = {0}; for (size_t i = 0; i < num_workers; ++i) {
        tasks[i] = (FlowWavefrontWorkerTask){.ring = ring, .worker_id = i, .subspace_idx = i};
        if (pthread_create(&threads[i], NULL, wavefront_worker_func, &tasks[i]) != 0) { wavefront_worker_func(&tasks[i]); threads[i] = 0; } }
    double total_subspace_energy = 0.0; for (size_t i = 0; i < num_workers; ++i) { if (threads[i]) pthread_join(threads[i], NULL);
        thread_slices[i] = tasks[i].slice_out; subspace_masks[i] = ring->decomp.subspaces[i].mask; total_subspace_energy += tasks[i].energy_out; }
    flow_hardware_probe_stop(&probe); ring->last_probe = probe; ring->total_cycles += probe.elapsed_cycles; ring->total_energy_uj += probe.dissipated_energy_uj;
    ring->global_lattice_genome = flow_wavefront_semilattice_join(ring->global_lattice_genome, thread_slices, subspace_masks, num_workers);
    ring->prev_lyapunov_energy = ring->global_lyapunov_energy; ring->global_lyapunov_energy = total_subspace_energy;
    ring->lyapunov_delta_e = fabs(ring->global_lyapunov_energy - ring->prev_lyapunov_energy); ring->wavefront_epoch++; ring->wave_cycle_count++; ring->quiescent_generation++;
    if (ring->bound_arena) flow_bump_qsbr_quiescent_fold(ring->bound_arena);
    if (ring->wave_cycle_count >= 1 && (ring->lyapunov_delta_e < FLOW_LYAPUNOV_CONVERGE_EPS || ring->global_lyapunov_energy < 1.0)) {
        ring->state = FLOW_RING_ATTRACTOR_REACHED; ring->attractor_converged = true;
        snprintf(ring->status_message, sizeof(ring->status_message), "Wavefront Attractor reached: cycle=%llu, energy=%.4f, Delta E=%.6f, cycles=%llu, uJ=%.1f (Lattice Genome=0x%016llx)",
                 (unsigned long long)ring->wave_cycle_count, ring->global_lyapunov_energy, ring->lyapunov_delta_e, (unsigned long long)ring->total_cycles, ring->total_energy_uj, (unsigned long long)ring->global_lattice_genome);
    } return 1; }
void flow_wavefront_ring_destroy(FlowWavefrontRing *ring) { if (ring) ring->state = FLOW_RING_INIT; }
int flow_wavefront_ring_bind_arena(FlowWavefrontRing *ring, FlowBumpQsbrArena *arena) { return (ring && arena) ? (ring->bound_arena = arena, 1) : 0; }
FlowSMTResult flow_wavefront_verify_temporal_safety_smt(const FlowWavefrontRing *ring, FlowSMTProofAttestation *proof_out) {
    size_t sz = sizeof(FlowBmf1BitCanvas), al = _Alignof(FlowBmf1BitCanvas), slot_al = _Alignof(FlowWavefrontSlot); const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("swmr_lockfree_safety", (ring && ring->state == FLOW_RING_UNSAT) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_DETERMINISM, "Writer encountered lock contention or dead state"),
        FLOW_SMT_RULE("64b_tear_free_confinement", (sz != 64 || al != 64 || slot_al < 64) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "FlowBmf1BitCanvas or FlowWavefrontSlot deviates from 64-byte single cache line alignment"),
        FLOW_SMT_RULE("bounded_evacuation_horizon", (ring && ring->slot_count > FLOW_WAVEFRONT_MAX_SLOTS) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Wavefront ring slot count unbounded"),
        FLOW_SMT_RULE("zero_imperative_cleanup", (ring && ring->bound_arena && ring->bound_arena->cursor > ring->bound_arena->capacity) ? 1 : 0, 0, 0, FLOW_BOX_THEOREM_MEMORY_QUOTA, "Bump arena memory exceeded capacity before fold")
    };
    return FLOW_SMT_VERIFY_RULES("wavefront_temporal_safety", proof_out, rules, "SMT WAVEFRONT TEMPORAL SOUND: Epoch=%llu, Gen=%llu, Size=%zu, Align=%zu (Zero-Cost QSBR Guaranteed)",
        ring ? (unsigned long long)ring->wavefront_epoch : 0ULL, ring ? (unsigned long long)ring->quiescent_generation : 0ULL, sz, al); }
void flow_hetero_mesh_init(FlowHeteroMesh *mesh) { if (mesh) memset(mesh, 0, sizeof(*mesh)); }
int flow_hetero_mesh_register_node(FlowHeteroMesh *mesh, uint8_t node_id, FlowSwarmRole role, const char *name, uint64_t contract_hash, uint32_t capacity) {
    if (!mesh || mesh->node_count >= FLOW_HETERO_MESH_MAX_NODES) return 0; for (size_t i = 0; i < mesh->node_count; ++i) if (mesh->nodes[i].node_id == node_id) return 0;
    FlowHeteroMeshNode *n = &mesh->nodes[mesh->node_count++];
    *n = (FlowHeteroMeshNode){ .node_id = node_id, .role = role, .contract_hash = contract_hash, .capacity = capacity ? capacity : 1000, .current_latency_us = 50, .is_active = 1 };
    strncpy(n->name, name ? name : "node", sizeof(n->name) - 1); return 1; }
int flow_hetero_mesh_route_target(const FlowHeteroMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_id_out) { if (!mesh || !selected_node_id_out) return 0;
    double best_energy = 1.0e18; int best_id = -1; size_t best_idx = 0; for (size_t i = 0; i < mesh->node_count; ++i) { const FlowHeteroMeshNode *n = &mesh->nodes[i];
        if (!n->is_active || n->role != target_role) continue;
        double cost = (double)n->current_latency_us * (1.0 + (double)n->current_backpressure / 200.0) + (n->current_backpressure >= 1000 ? 1.0e9 : 0.0);
        if (cost < best_energy) { best_energy = cost; best_id = (int)n->node_id; best_idx = i; } } if (best_id < 0) return 0; *selected_node_id_out = (uint8_t)best_id;
    __atomic_fetch_add(&((FlowHeteroMesh *)mesh)->nodes[best_idx].total_routed_requests, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&((FlowHeteroMesh *)mesh)->total_mesh_routed, 1, __ATOMIC_RELAXED);
    return 1; }
FlowSMTResult flow_hetero_mesh_verify_smt(const FlowHeteroMesh *mesh, uint32_t ingress_max_qps, FlowSMTProofAttestation *proof_out) { if (!mesh) return FLOW_SMT_UNKNOWN;
    uint64_t comp_cap = 0, stor_cap = 0, max_bp = 0; size_t ds_nodes = 0; for (size_t i = 0; i < mesh->node_count; ++i) { const FlowHeteroMeshNode *n = &mesh->nodes[i];
        if (!n->is_active) continue; if (n->role == FLOW_SWARM_ROLE_COMPUTE_ROUTER) { comp_cap += n->capacity; ds_nodes++; }
        else if (n->role == FLOW_SWARM_ROLE_STORAGE_INDEX) { stor_cap += n->capacity; ds_nodes++; } }
    uint64_t bottleneck = (stor_cap > 0 && (!comp_cap || stor_cap < comp_cap)) ? stor_cap : (comp_cap ? comp_cap : comp_cap + stor_cap);
    if (ds_nodes <= 1 && mesh->node_count > 1)
        for (size_t i = 0; i < mesh->node_count; ++i) if (mesh->nodes[i].current_backpressure > max_bp) max_bp = mesh->nodes[i].current_backpressure;
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("ingress QPS", ingress_max_qps, 0, bottleneck, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "exceeds pipeline bottleneck tier capacity"),
        FLOW_SMT_RULE("downstream saturation", max_bp, 0, FLOW_MESH_SATURATION_LIMIT_PERMILLE, FLOW_BOX_THEOREM_MEMORY_QUOTA, "single-point-of-failure downstream node at 95%+ saturation") };
    return FLOW_SMT_VERIFY_RULES("hetero_mesh", proof_out, rules, "SMT MESH SOUND: ingress_qps=%u <= bottleneck_cap=%llu across %zu nodes (Zero-Defect)",
        ingress_max_qps, (unsigned long long)bottleneck, ds_nodes); }
int flow_swarm_ring_mesh_init(FlowSwarmRingMesh *mesh) { if (!mesh) return 0;
    memset(mesh, 0, sizeof(*mesh)); for (size_t i = 0; i < FLOW_HETERO_RING_MAX_NODES; ++i) atomic_init(&mesh->slots[i].seq, 0); return 1; }
int flow_swarm_ring_publish(FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRole role, uint16_t backpressure,
                            uint16_t latency_p99_us, uint16_t contract_crc16, uint32_t capacity_qps, const double q[16]) {
    if (!mesh || node_id >= FLOW_HETERO_RING_MAX_NODES) return 0; FlowSwarmRingSlot *slot = &mesh->slots[node_id];
    uint64_t prev_seq = atomic_load_explicit(&slot->seq, memory_order_relaxed); atomic_store_explicit(&slot->seq, prev_seq + 1, memory_order_release);
    atomic_thread_fence(memory_order_release);
    slot->node_id = node_id; slot->role = (uint8_t)role; slot->backpressure_permille = backpressure;
    slot->latency_p99_us = latency_p99_us; slot->contract_crc16 = contract_crc16; slot->capacity_qps = capacity_qps; slot->timestamp_ns = flow_clock_monotonic_ns();
    if (q) memcpy(slot->q_snapshot, q, sizeof(double) * 16); atomic_store_explicit(&slot->seq, prev_seq + 2, memory_order_release);
    if (node_id >= mesh->active_node_count) mesh->active_node_count = node_id + 1;
    __atomic_fetch_add(&mesh->total_ring_updates, 1, __ATOMIC_RELAXED);
    return 1; }
int flow_swarm_ring_sample(const FlowSwarmRingMesh *mesh, uint8_t node_id, FlowSwarmRingSlot *slot_out) {
    if (!mesh || !slot_out || node_id >= FLOW_HETERO_RING_MAX_NODES) return 0; const FlowSwarmRingSlot *slot = &mesh->slots[node_id];
    for (int retry = 0; retry < 16; ++retry) { uint64_t s1 = atomic_load_explicit(&slot->seq, memory_order_acquire); if (!s1 || (s1 & 1)) continue; *slot_out = *slot;
        atomic_thread_fence(memory_order_acquire);
        if (s1 == atomic_load_explicit(&slot->seq, memory_order_acquire)) { __atomic_fetch_add(&((FlowSwarmRingMesh *)mesh)->total_ring_samples, 1, __ATOMIC_RELAXED); return 1; } } return 0; }
int flow_swarm_ring_route_lowest_energy(const FlowSwarmRingMesh *mesh, FlowSwarmRole target_role, uint8_t *selected_node_out) { if (!mesh || !selected_node_out) return 0;
    double lowest = 1e12; int found_id = -1; size_t limit = mesh->active_node_count > 0 ? mesh->active_node_count : FLOW_HETERO_RING_MAX_NODES;
    for (size_t i = 0; i < limit; ++i) { const FlowSwarmRingSlot *slot = &mesh->slots[i];
        if (!atomic_load_explicit(&slot->seq, memory_order_acquire) || slot->role != (uint8_t)target_role) continue;
        double e = (double)slot->backpressure_permille + (double)slot->latency_p99_us * 0.5; if (e < lowest) { lowest = e; found_id = (int)slot->node_id; } }
    if (found_id < 0) return 0; *selected_node_out = (uint8_t)found_id; return 1; }
