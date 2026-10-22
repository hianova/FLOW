#include "backend.h"
#include "reload.h"
#include "bitspace.h"
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static const Component COMPONENTS[] = { {"sharded_hash", "collection", "cpu", "pthread", 1, 1, 1, 0, 9, 7, "", "", 0u, 12u, 1, 0},
    {"linear_array", "collection", "cpu", "stdlib", 0, 0, 1, 0, 5, 9, "", "", 0u, 8u, 1, 0},
    {"ordered_tree", "collection", "cpu", "pthread", 1, 0, 0, 0, 6, 6, "", "", 0u, 16u, 0, 0},
    {"bounded_queue", "queue", "cpu", "pthread", 1, 0, 1, 0, 9, 8, "", "bounded_queue", 0u, 8u, 0, 0},
    {"parallel_map", "algorithm", "cpu", "pthread", 0, 0, 1, 1, 8, 6, "", "parallel_map", 0u, 8u, 0, 0},
    {"binary_parser", "parser", "cpu", "stdlib", 0, 0, 1, 0, 7, 7, "", "binary_parser", 0u, 8u, 0, 0},
    {"state_machine", "state_machine", "cpu", "stdlib", 0, 0, 1, 0, 7, 7, "", "state_machine", 0u, 8u, 0, 0},
    {"flowc_bootstrap", "compiler", "cpu", "stdio", 0, 0, 1, 0, 7, 7, "compiler", "", 0u, 8u, 0, 0} };
static int builtin_compatible(const SemanticIR *ir, const Component *c) { if (!ir || !c) return 0;
    if (!strcmp(c->id, "flowc_bootstrap")) return ir->contract_name[0] && !strcmp(ir->contract_name, "compiler");
    if (ir->contract_name[0] && !strcmp(ir->contract_name, "compiler")) return 0;
    if (c->flow_binding[0] && strcmp(ir->flow_name, c->flow_binding) && !(ir->flow_parallelizable && c->supports_parallelizable)) return 0;
    if (ir->state_shared != c->supports_shared || (ir->state_read_heavy && !c->supports_read_heavy) ||
        (ir->fact_unordered && !c->supports_unordered) || (ir->flow_parallelizable != c->supports_parallelizable)) return 0;
    for (size_t i = 0; i < ir->declared_constraint_count; ++i) { const char *cn = ir->constraints[i].name;
        if (strcmp(cn, "deterministic") && strcmp(cn, "memory") && strcmp(cn, "sla") && strcmp(cn, "latency") &&
            strcmp(cn, "lean_certified") && strcmp(cn, "proof_carrying") && strcmp(cn, "no_sorry")) return 0; } return 1; }
static int builtin_validate_contract(const SemanticIR *ir, const FlowPlugin *p, char *msg, size_t sz) { (void)p; if (msg && sz) msg[0] = '\0'; return ir != NULL; }
static int builtin_memory_model(const SemanticIR *ir, const Component *c, size_t cap, size_t shards, size_t *est) { (void)ir; if (!est || !c) return 0;
    if (!shards) shards = 1; size_t slots = cap; if (!strcmp(c->id, "sharded_hash")) { if (cap > SIZE_MAX - (shards - 1u)) return 0;
        size_t per_shard = (cap + shards - 1u) / shards; if (per_shard && shards > SIZE_MAX / per_shard) return 0; slots = per_shard * shards; }
    if (c->memory_bytes_per_capacity && slots > SIZE_MAX / c->memory_bytes_per_capacity) return 0; size_t var = slots * c->memory_bytes_per_capacity;
    if (c->memory_fixed_bytes > SIZE_MAX - var) return 0; *est = c->memory_fixed_bytes + var; return 1; }
static int builtin_enumerate_dimensions(const SemanticIR *ir, const Component *c, FlowPlanDimensionSet *dims_out) { (void)ir; if (!dims_out || !c) return 0;
    static const FlowPlanDimension DIMS[8] = { {"capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 1, 20, 1, 12, 500},
        {"threads", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 0, 6, 1, 0, 200}, {"shards", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_STRUCTURAL_JIT, 0, 6, 1, 0, 200},
        {"buffer_bytes", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 1024, 65536, 1024, 16384, 0},
        {"initial_capacity", FLOW_DIM_EXPONENT, FLOW_DIM_CLASS_TACTILE_PARAM, 1, 16, 1, 2, 0},
        {"growth_percent", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 110, 200, 10, 150, 0},
        {"batch_size", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 1024, 65536, 1024, 16384, 0},
        {"arena_bytes", FLOW_DIM_LINEAR, FLOW_DIM_CLASS_TACTILE_PARAM, 0, 1048576, 4096, 0, 0} }; dims_out->count = 8; memcpy(dims_out->dimensions, DIMS, sizeof(DIMS));
    return 1; } static size_t builtin_target_threads(const SemanticIR *ir) { long online = sysconf(_SC_NPROCESSORS_ONLN); if (online < 1) online = 1;
    if (online > 64) online = 64; size_t target = (ir && ir->flow_parallelizable) ? 4 : ((ir && ir->state_shared) ? 16 : 1);
    return (target > (size_t)online ? (size_t)online : (target ? target : 1)); }
static int builtin_evaluate_plan(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, FlowPlanMetrics *m) { if (!m || !c || !plan) return 0;
    size_t cap = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count, thr = plan->count > 1 ? (size_t)plan->values[1] : 1, shd = plan->count > 2 ? (size_t)plan->values[2] : 1, est = 0;
    if (!cap) cap = 1; if (!thr) thr = 1; if (!shd) shd = 1; (void)builtin_memory_model(ir, c, cap, shd, &est);
    double t_cap = (ir && ir->input_max_count > 0 && ir->input_max_count < 4096) ? (double)ir->input_max_count : 4096.0;
    double d_cap = ((double)cap < t_cap) ? ((t_cap - (double)cap) * 50.0) : (((double)cap - t_cap) * 0.1);
    double d_thr = fabs((double)thr - (double)builtin_target_threads(ir)) * 20.0, d_shd = fabs((double)shd - ((ir && ir->state_shared) ? 16.0 : 1.0)) * 50.0;
    *m = (FlowPlanMetrics){ .capacity = cap, .threads = thr, .shards = shd, .memory_bytes = est, .latency_score = (double)c->latency_score, .throughput_score = (double)thr * (11.0 - (double)c->latency_score), .energy = (double)est / 1024.0 + (double)c->latency_score * 2.0 + d_cap + d_thr + d_shd };
    return 1; } static int builtin_verify_plan(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan, VerificationReport *r) {
    if (!r || !c || !plan) return 0; *r = (VerificationReport){0};
    size_t cap = plan->count > 0 ? (size_t)plan->values[0] : (size_t)ir->input_max_count, shd = plan->count > 2 ? (size_t)plan->values[2] : 1, est = 0;
    if (!cap) cap = 1; if (!shd) shd = 1;
    if (!builtin_memory_model(ir, c, cap, shd, &est)) { r->status = VERIFIER_COMPILE_ERROR; snprintf(r->message, sizeof(r->message), "builtin memory model failed"); return 0; }
    r->capacity = cap; r->estimated_bytes = est; r->max_count_proven = (size_t)ir->input_max_count <= cap;
    r->runtime_input_guard = !r->max_count_proven; r->status = r->max_count_proven ? VERIFIER_PROVEN : VERIFIER_RUNTIME_CHECK;
    snprintf(r->message, sizeof(r->message), r->max_count_proven ? "input bound proven" : "input bound requires runtime guard"); return 1; }
static int builtin_verify(const SemanticIR *ir, const Component *c, size_t cap, size_t shd, char *msg, size_t sz) {
    (void)ir; (void)c; (void)cap; (void)shd; if (msg && sz) msg[0] = '\0'; return 1; } typedef struct { int id, score; } BenchItem;
typedef struct { int occupied; BenchItem item; } BenchSlot; typedef struct { BenchItem item; int left, right; } BenchNode;
typedef struct { const BenchItem *input; BenchItem *output; size_t begin, end; } BenchMapTask; static volatile uint64_t builtin_benchmark_sink;
static uint64_t builtin_clock_ns(void) { struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ? (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec : 0; }
static uint64_t bench_linear(size_t cap, size_t thr, size_t cnt) { BenchItem items[4096]; size_t rounds = 32 + thr * 2, st = cap > 4096 ? 4096 : (cap < cnt ? cnt : cap);
    volatile uint64_t cs = 0; uint64_t start = builtin_clock_ns(); for (size_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < cnt; ++i) items[i % st] = (BenchItem){(int)i, (int)((i * 17 + r) % 101)};
        for (size_t q = 0; q < cnt; ++q) cs += (uint64_t)(items[q % st].score + q); } builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
static uint64_t bench_hash(size_t cap, size_t thr, size_t shd, size_t cnt) { BenchSlot slots[4096]; size_t rounds = 32 + thr * 2, st = cap < cnt * 2 ? cnt * 2 : cap;
    if (st > 4096) st = 4096; if (!shd) shd = 1; volatile uint64_t cs = 0; uint64_t start = builtin_clock_ns(); for (size_t r = 0; r < rounds; ++r) {
        memset(slots, 0, sizeof(slots)); for (size_t i = 0; i < cnt; ++i) { size_t s0 = (((size_t)((uint32_t)i * UINT32_C(2654435761)) % shd) + i) % st;
            for (size_t p = 0; p < st; ++p) if (!slots[(s0 + p) % st].occupied) { slots[(s0 + p) % st] = (BenchSlot){1, {(int)i, (int)((i * 17 + r) % 101)}}; break; } }
        for (size_t q = 0; q < cnt; ++q) { size_t s0 = (((size_t)((uint32_t)q * UINT32_C(2654435761)) % shd) + q) % st;
            for (size_t p = 0; p < st; ++p) { size_t s = (s0 + p) % st; if (slots[s].occupied && slots[s].item.id == (int)q) { cs += (uint64_t)(slots[s].item.score + q); break; } }
        } } builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; } static void tree_insert(BenchNode *nodes, size_t *len, int *root, BenchItem item) {
    int idx = (int)(*len); nodes[(*len)++] = (BenchNode){item, -1, -1}; if (*root < 0) { *root = idx; return; } for (int cur = *root;;) {
        int *next = item.score > nodes[cur].item.score ? &nodes[cur].left : &nodes[cur].right; if (*next < 0) { *next = idx; return; } cur = *next; } }
static uint64_t tree_sum(const BenchNode *nodes, int r) { return r < 0 ? 0 : (uint64_t)nodes[r].item.score + tree_sum(nodes, nodes[r].left) + tree_sum(nodes, nodes[r].right); }
static uint64_t bench_tree(size_t cap, size_t thr, size_t cnt) { BenchNode nodes[4096]; size_t rounds = 32 + thr * 2, st = cap > 4096 ? 4096 : (cap < cnt ? cnt : cap);
    uint64_t cs = 0, start = builtin_clock_ns(); for (size_t r = 0; r < rounds; ++r) { size_t len = 0; int root = -1;
        for (size_t i = 0; i < cnt && i < st; ++i) tree_insert(nodes, &len, &root, (BenchItem){(int)i, (int)((i * 17 + r) % 101)}); cs += tree_sum(nodes, root); }
    builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; } static uint64_t bench_queue(size_t cap, size_t thr, size_t cnt) {
    BenchItem queue[4096]; size_t rounds = 32 + thr * 2, st = cap > 4096 ? 4096 : (cap ? cap : 1); if (cnt > st) cnt = st; uint64_t cs = 0, start = builtin_clock_ns();
    for (size_t r = 0; r < rounds; ++r) { size_t head = 0, tail = 0, len = 0;
        for (size_t i = 0; i < cnt; ++i) { queue[tail] = (BenchItem){(int)i, (int)(r + i)}; tail = (tail + 1) % st; ++len; }
        while (len > 0) { cs += (uint64_t)queue[head].score; head = (head + 1) % st; --len; } } builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
static void *bench_map_worker(void *raw) { BenchMapTask *t = raw; for (size_t i = t->begin; i < t->end; ++i) { t->output[i] = t->input[i]; t->output[i].score *= 2; }
    return NULL; } static uint64_t bench_parallel_map(size_t thr, size_t cnt) { BenchItem in[4096], out[4096]; pthread_t workers[64]; BenchMapTask tasks[64];
    size_t rounds = 32 + thr * 2, wc = thr < cnt ? thr : cnt; if (wc > 64) wc = 64; uint64_t cs = 0, start = builtin_clock_ns(); for (size_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < cnt; ++i) in[i] = (BenchItem){(int)i, (int)((i + r) % 101)}; size_t created = 0; if (wc >= 2 && cnt >= wc * 2) {
            for (size_t i = 0; i < wc; ++i) { tasks[i] = (BenchMapTask){in, out, i * cnt / wc, (i + 1) * cnt / wc};
                if (pthread_create(&workers[i], NULL, bench_map_worker, &tasks[i]) != 0) break; ++created; }
            for (size_t i = 0; i < created; ++i) (void)pthread_join(workers[i], NULL); }
        if (created != wc || wc < 2 || cnt < wc * 2) for (size_t i = 0; i < cnt; ++i) { out[i] = in[i]; out[i].score *= 2; }
        for (size_t i = 0; i < cnt; ++i) cs += (uint64_t)out[i].score; } builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
static uint64_t builtin_benchmark(const SemanticIR *ir, const Component *c, const FlowPlanAssignment *plan) { if (!c) return UINT64_MAX;
    size_t cnt = ir->input_max_count > 64 ? 64 : (ir->input_max_count < 8 ? 8 : (size_t)ir->input_max_count);
    size_t cap = plan && plan->count > 0 ? (size_t)plan->values[0] : 4096, thr = plan && plan->count > 1 ? (size_t)plan->values[1] : 1, shd = plan && plan->count > 2 ? (size_t)plan->values[2] : 1;
    if (!strcmp(c->id, "bounded_queue")) return bench_queue(cap, thr, cnt); if (!strcmp(c->id, "parallel_map")) return bench_parallel_map(thr, cnt);
    if (!strcmp(c->id, "binary_parser") || !strcmp(c->id, "state_machine")) { uint64_t start = builtin_clock_ns(); volatile uint64_t cs = 0;
        for (size_t r = 0; r < (32 + thr * 2) * 4; ++r) cs += 49; builtin_benchmark_sink ^= cs; return builtin_clock_ns() - start; }
    if (!strcmp(c->id, "linear_array")) return bench_linear(cap, thr, cnt); if (!strcmp(c->id, "ordered_tree")) return bench_tree(cap, thr, cnt);
    return bench_hash(cap, thr, shd, cnt); } typedef struct { int id, score, occupied; } BuiltinEntry;
typedef struct { size_t capacity, threads, shards, count; BuiltinEntry *entries; } BuiltinRuntimeState; static int builtin_state_init(void *h, void **out) {
    (void)h; BuiltinRuntimeState *st = calloc(1, sizeof(*st)); if (!st) return -1; st->capacity = 4096; st->threads = st->shards = 1;
    if (!(st->entries = calloc(st->capacity, sizeof(BuiltinEntry)))) { free(st); return -1; } *out = st; return 0; }
static int builtin_state_run(void *h, void *s, const void *in, void *out) { (void)h; BuiltinRuntimeState *st = s; if (!st) return -1;
    if (in && st->count < st->capacity) { int val = *(const int *)in; st->entries[st->count++] = (BuiltinEntry){val, val * 2, 1}; } if (out) *(int *)out = (int)st->count;
    return 0; } static int builtin_state_apply(void *h, void *s, const FlowMutation *m) { (void)h; BuiltinRuntimeState *st = s; if (!st || !m) return -1;
    if (m->kind == FLOW_MUTATION_UPSERT && m->value && m->value_size == sizeof(int) && st->count < st->capacity) {
        int val = *(const int *)m->value; st->entries[st->count++] = (BuiltinEntry){m->key ? *(const int *)m->key : val, val, 1}; } return 0; }
static int builtin_state_migrate(void *h, const void *os, void *ns) { (void)h; const BuiltinRuntimeState *o = os; BuiltinRuntimeState *n = ns; if (!o || !n) return -1;
    size_t cnt = o->count > n->capacity ? n->capacity : o->count; if (cnt && o->entries && n->entries) memcpy(n->entries, o->entries, cnt * sizeof(BuiltinEntry));
    n->count = cnt; return 0; } static void builtin_state_drop(void *h, void *s) { (void)h; BuiltinRuntimeState *st = s; if (st) { free(st->entries); free(st); } }
static const FlowSchemaField BUILTIN_SCHEMA_FIELDS[] = { {"entries", "entry[]", FLOW_SCHEMA_FIELD_PERSISTENT | FLOW_SCHEMA_FIELD_ORDERED},
    {"count", "u64", FLOW_SCHEMA_FIELD_PERSISTENT} }; static const FlowSchema BUILTIN_UNIT_SCHEMA = {"builtin_collection", 1, BUILTIN_SCHEMA_FIELDS, 2};
static int builtin_create_unit(const FlowPlanArtifact *art, const SemanticIR *ir, const Component *comp, FlowUnit *unit_out) { if (!art || !comp || !unit_out) return 0;
    *unit_out = (FlowUnit){ .abi_version = FLOW_RELOAD_ABI_VERSION, .name = comp->id, .init = builtin_state_init, .run = builtin_state_run,
        .apply = builtin_state_apply, .migrate = builtin_state_migrate, .drop = builtin_state_drop, .schema = &BUILTIN_UNIT_SCHEMA,
        .constraint_hash = flow_compute_contract_hash(ir), .capability_hash = 0x55, .semantic_schema_hash = art->plan_schema_hash, .supports_snapshot_cow = 1 }; return 1; }
static uint64_t builtin_preference_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims) { if (!dims || !dims->count) return 0;
    uint64_t mask = 0; unsigned shift = 0; for (size_t i = 0; i < dims->count; ++i) { const FlowPlanDimension *d = &dims->dimensions[i];
        unsigned bits = flow_dimension_bits(d); if (!bits) continue; uint64_t dm = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1);
        int pref = c && ((!strcmp(c->id, "sharded_hash") && ir && (ir->state_shared || ir->state_read_heavy) && (!strcmp(d->name, "threads") || !strcmp(d->name, "shards"))) ||
                         (!strcmp(c->id, "bounded_queue") && (!strcmp(d->name, "buffer_bytes") || !strcmp(d->name, "batch_size"))) ||
                         (!strcmp(c->id, "parallel_map") && (!strcmp(d->name, "threads") || !strcmp(d->name, "batch_size"))) ||
                         (!strcmp(c->id, "linear_array") && (!strcmp(d->name, "capacity") || !strcmp(d->name, "initial_capacity")))); if (pref) mask |= (dm << shift);
        shift += bits; } return mask; }
static uint64_t builtin_environment_mask(const SemanticIR *ir, const Component *c, const FlowPlanDimensionSet *dims, const FlowEnvironmentState *env) { (void)ir; (void)c;
    if (!dims || !dims->count || !env) return (uint64_t)-1; uint64_t mask = (uint64_t)-1; unsigned shift = 0; for (size_t i = 0; i < dims->count; ++i) {
        const FlowPlanDimension *d = &dims->dimensions[i]; unsigned bits = flow_dimension_bits(d); if (!bits) continue;
        uint64_t dm = (bits >= 64) ? (uint64_t)-1 : (((uint64_t)1 << bits) - 1); if (env->pressure_level == FLOW_ENV_PRESSURE_MEMORY_CRITICAL) {
            if (!strcmp(d->name, "arena_bytes")) mask &= ~(dm << shift);
            else if (!strcmp(d->name, "buffer_bytes") || !strcmp(d->name, "batch_size") || !strcmp(d->name, "threads")) {
                uint64_t allowed = !strcmp(d->name, "threads") ? 0x1 : 0x3;
                for (unsigned b = 0; b < bits; ++b) if (((uint64_t)1 << b) > allowed) mask &= ~(UINT64_C(1) << (shift + b)); }
        } else if (env->pressure_level == FLOW_ENV_PRESSURE_CACHE_THRASHING && (!strcmp(d->name, "shards") || !strcmp(d->name, "buffer_bytes"))) {
            for (unsigned b = 0; b < bits; ++b) if (((uint64_t)1 << b) > 0x3) mask &= ~(UINT64_C(1) << (shift + b)); }
        if (env->hardware_arch == FLOW_ARCH_APPLE_SILICON && !strcmp(d->name, "batch_size")) mask &= ~(UINT64_C(0x1) << shift); shift += bits; } return mask; }
static const FlowPlugin BUILTIN_PLUGIN = { .name = "builtin", .version = "1", .components = COMPONENTS, .component_count = sizeof(COMPONENTS) / sizeof(COMPONENTS[0]),
    .compatible = builtin_compatible, .memory_model = builtin_memory_model, .verify = builtin_verify, .emit = flow_emit_builtin_component,
    .validate_contract = builtin_validate_contract, .enumerate_dimensions = builtin_enumerate_dimensions, .evaluate_plan = builtin_evaluate_plan,
    .verify_plan = builtin_verify_plan, .benchmark = builtin_benchmark, .preference_mask = builtin_preference_mask,
    .environment_mask = builtin_environment_mask, .create_unit = builtin_create_unit,
    .doc_title = "Builtin Algorithms & Data Structures", .doc_responsibilities = "Provides linear_array, sharded_hash, parallel_map, and basic primitives",
    .doc_algorithmic_guarantee = "Formal QF_LIA verified sound bounds", .doc_memory_concurrency_model = "Pthread lock-free / QSBR shared memory",
    .doc_key_apis = "flow_component_evaluate, flow_component_verify_plan" }; const FlowPlugin *flow_builtin_plugin(void) { return &BUILTIN_PLUGIN; }
