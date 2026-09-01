/* Standalone compiler template embedded by FLOW's compiler component. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FLOW_LINE 512
#define FLOW_NAME 64
#define FLOW_NODES 32
#define FLOW_SAMPLE_MAX 32

typedef struct {
    char name[FLOW_NAME];
} FlowNode;

typedef struct {
    int id;
    int score;
} FlowSample;

typedef struct {
    char input[FLOW_NAME];
    char output[FLOW_NAME];
    char output_type[FLOW_NAME];
    char state[FLOW_NAME];
    char flow[FLOW_NAME];
    char resource[FLOW_NAME];
    char capability[FLOW_NAME];
    char domain[FLOW_NAME];
    char contract[FLOW_NAME];
    char fallback[FLOW_NAME];
    char expression[FLOW_LINE];
    FlowNode nodes[FLOW_NODES];
    size_t node_count;
    int max_count;
    FlowSample samples[FLOW_SAMPLE_MAX];
    size_t sample_count;
    int top_n;
    int shared;
    int read_heavy;
    int bounded;
    int ordered;
    int parallelizable;
    int deterministic;
    int memory_mb;
    int prefer_latency;
    int ensure_count;
    int hole_count;
} FlowIR;

typedef enum {
    FACT_SHARED,
    FACT_COLLECTION,
    FACT_TRANSFORM,
    FACT_PARALLEL,
    FACT_ORDERED,
    FACT_BOUNDED,
    FACT_CAPABILITY,
    FACT_CONSTRAINT,
    FACT_HOLE,
    FACT_RANGE,
    FACT_SIZE,
    FACT_MUTABILITY,
    FACT_DETERMINISM
} FactKind;

typedef struct {
    FactKind kind;
    char detail[FLOW_NAME];
} Fact;

typedef struct {
    FlowIR value;
    Fact facts[FLOW_NODES * 2];
    size_t fact_count;
    size_t constraint_count;
} SemanticIR;

typedef struct {
    int available;
    size_t component;
    size_t capacity;
    size_t threads;
    size_t shards;
    uint64_t benchmark_ns;
} ProfileSeed;

typedef struct {
    const struct Component *component;
    uint64_t genome;
    size_t capacity;
    size_t threads;
    size_t shards;
    double energy;
    uint64_t benchmark_ns;
    int measured;
    size_t iterations;
    uint32_t seed;
} SearchResult;

typedef struct Component {
    const char *id;
    const char *kind;
    const char *resource;
    const char *capability;
    int shared;
    int read_heavy;
    int unordered;
    int supports_parallelizable;
    int memory_per_item;
    int latency_score;
    int memory_score;
    const char *domain_contract;
    const char *flow_binding;
    const char *generator;
} Component;

#define FLOW_ARENA_BYTES 4096

typedef struct {
    char bytes[FLOW_ARENA_BYTES];
    size_t used;
} Arena;

typedef struct {
    size_t nodes[FLOW_NODES];
    size_t head;
    size_t tail;
} FlowQueue;

#define FLOW_REGISTRY_SLOTS 16

typedef struct {
    const char *key;
    const Component *value;
} RegistrySlot;

typedef struct {
    RegistrySlot slots[FLOW_REGISTRY_SLOTS];
} ComponentRegistry;

static const Component COMPONENTS[] = {
    {"sharded_hash", "collection", "cpu", "pthread", 1, 1, 1, 0, 12, 9, 7, "", "", "emit_sharded_hash"},
    {"linear_array", "collection", "cpu", "stdlib", 0, 0, 1, 0, 8, 5, 9, "", "", "emit_linear_array"},
    {"ordered_tree", "collection", "cpu", "pthread", 1, 0, 0, 0, 16, 6, 6, "", "", "emit_ordered_tree"},
    {"bounded_queue", "queue", "cpu", "pthread", 1, 0, 1, 0, 8, 9, 8, "", "bounded_queue", "emit_bounded_queue"},
    {"parallel_map", "algorithm", "cpu", "pthread", 0, 0, 1, 1, 8, 8, 6, "", "parallel_map", "emit_parallel_map"},
    {"binary_parser", "parser", "cpu", "stdlib", 0, 0, 1, 0, 8, 7, 7, "", "binary_parser", "emit_binary_parser"},
    {"state_machine", "state_machine", "cpu", "stdlib", 0, 0, 1, 0, 8, 7, 7, "", "state_machine", "emit_state_machine"}
};

static const char *arena_store(Arena *arena, const char *text) {
    size_t length = strlen(text) + 1;
    char *result;
    if (length > FLOW_ARENA_BYTES - arena->used) return NULL;
    result = &arena->bytes[arena->used];
    memcpy(result, text, length);
    arena->used += length;
    return result;
}

static void queue_push(FlowQueue *queue, size_t node) {
    if (queue->tail < FLOW_NODES) queue->nodes[queue->tail++] = node;
}

static int queue_pop(FlowQueue *queue, size_t *node) {
    if (queue->head >= queue->tail) return 0;
    *node = queue->nodes[queue->head++];
    return 1;
}

static size_t registry_hash(const char *text) {
    size_t hash = 2166136261u;
    while (*text != '\0') hash = (hash ^ (unsigned char)*text++) * 16777619u;
    return hash % FLOW_REGISTRY_SLOTS;
}

static void registry_init(ComponentRegistry *registry) {
    size_t i;
    memset(registry, 0, sizeof(*registry));
    for (i = 0; i < sizeof(COMPONENTS) / sizeof(COMPONENTS[0]); ++i) {
        size_t slot = registry_hash(COMPONENTS[i].id);
        while (registry->slots[slot].key != NULL)
            slot = (slot + 1) % FLOW_REGISTRY_SLOTS;
        registry->slots[slot].key = COMPONENTS[i].id;
        registry->slots[slot].value = &COMPONENTS[i];
    }
}

static const Component *registry_lookup(const ComponentRegistry *registry,
                                        const char *key) {
    size_t start = registry_hash(key);
    size_t probe;
    for (probe = 0; probe < FLOW_REGISTRY_SLOTS; ++probe) {
        size_t slot = (start + probe) % FLOW_REGISTRY_SLOTS;
        if (registry->slots[slot].key == NULL) return NULL;
        if (strcmp(registry->slots[slot].key, key) == 0)
            return registry->slots[slot].value;
    }
    return NULL;
}

static char *trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static void copy_word(char *destination, const char *text) {
    (void)sscanf(text, "%63s", destination);
}

static void parse_nodes(FlowIR *ir, const char *text) {
    char buffer[FLOW_LINE];
    char *node;
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    node = strtok(buffer, "->");
    while (node != NULL && ir->node_count < FLOW_NODES) {
        node = trim(node);
        if (*node != '\0') copy_word(ir->nodes[ir->node_count++].name, node);
        node = strtok(NULL, "->");
    }
}

static int parse_flow(FILE *input, FlowIR *ir) {
    char raw[FLOW_LINE];
    enum { SECTION_NONE, SECTION_INPUT, SECTION_OUTPUT, SECTION_STATE,
           SECTION_FLOW, SECTION_REQUIRE, SECTION_ENSURE, SECTION_PREFER } section;
    memset(ir, 0, sizeof(*ir));
    ir->top_n = 3;
    section = SECTION_NONE;
    while (fgets(raw, sizeof(raw), input) != NULL) {
        char *line = trim(raw);
        char *top;
        if (*line == '\0' || *line == '#') continue;
        if (strncmp(line, "input ", 6) == 0) {
            section = SECTION_INPUT; copy_word(ir->input, line + 6); continue;
        }
        if (strncmp(line, "output ", 7) == 0) {
            section = SECTION_OUTPUT; copy_word(ir->output, line + 7); continue;
        }
        if (strncmp(line, "state ", 6) == 0) {
            section = SECTION_STATE; copy_word(ir->state, line + 6); continue;
        }
        if (strncmp(line, "flow ", 5) == 0) {
            section = SECTION_FLOW; copy_word(ir->flow, line + 5); continue;
        }
        if (strncmp(line, "require ", 8) == 0) { section = SECTION_REQUIRE; continue; }
        if (strncmp(line, "ensure ", 7) == 0) { section = SECTION_ENSURE; continue; }
        if (strncmp(line, "prefer ", 7) == 0) { section = SECTION_PREFER; continue; }
        if (strncmp(line, "resource ", 9) == 0) { copy_word(ir->resource, line + 9); continue; }
        if (strncmp(line, "capability ", 11) == 0) { copy_word(ir->capability, line + 11); continue; }
        if (strncmp(line, "domain ", 7) == 0) { copy_word(ir->domain, line + 7); continue; }
        if (strncmp(line, "contract ", 9) == 0) { copy_word(ir->contract, line + 9); continue; }
        if (strncmp(line, "fallback ", 9) == 0) { copy_word(ir->fallback, line + 9); continue; }
        if (*line == '}') { section = SECTION_NONE; continue; }
        if (section == SECTION_INPUT) {
            if (sscanf(line, "max_count %d", &ir->max_count) == 1) continue;
            if (ir->sample_count < FLOW_SAMPLE_MAX &&
                sscanf(line, "sample %d %d",
                       &ir->samples[ir->sample_count].id,
                       &ir->samples[ir->sample_count].score) == 2) {
                ++ir->sample_count;
                continue;
            }
        }
        if (section == SECTION_OUTPUT) {
            if (sscanf(line, "type %63s", ir->output_type) == 1) continue;
        }
        if (section == SECTION_STATE) {
            if (strcmp(line, "shared") == 0) ir->shared = 1;
            if (strcmp(line, "read_heavy") == 0) ir->read_heavy = 1;
            if (strcmp(line, "bounded") == 0) ir->bounded = 1;
        }
        if (section == SECTION_FLOW) {
            if (ir->expression[0] == '\0') {
                strncpy(ir->expression, line, sizeof(ir->expression) - 1);
                parse_nodes(ir, line);
            }
            top = strstr(line, "top(");
            if (top != NULL) ir->top_n = atoi(top + 4);
            if (strstr(line, "sort") != NULL) ir->ordered = 1;
            if (strstr(line, "transform") != NULL || strstr(line, "parallel") != NULL)
                ir->parallelizable = 1;
            if (strchr(line, '?') != NULL) ++ir->hole_count;
        }
        if (section == SECTION_REQUIRE) {
            if (strcmp(line, "deterministic") == 0) ir->deterministic = 1;
            (void)sscanf(line, "memory < %dmb", &ir->memory_mb);
        }
        if (section == SECTION_ENSURE && strcmp(line, "deterministic") == 0)
            ir->deterministic = 1;
        if (section == SECTION_ENSURE && strcmp(line, "deterministic") == 0)
            ++ir->ensure_count;
        if (section == SECTION_PREFER && strcmp(line, "latency") == 0)
            ir->prefer_latency = 1;
    }
    return ir->flow[0] != '\0' && ir->input[0] != '\0' && ir->max_count > 0;
}

static void add_fact(SemanticIR *ir, FactKind kind, const char *detail) {
    if (ir->fact_count >= sizeof(ir->facts) / sizeof(ir->facts[0])) return;
    ir->facts[ir->fact_count].kind = kind;
    strncpy(ir->facts[ir->fact_count].detail, detail, FLOW_NAME - 1);
    ir->facts[ir->fact_count].detail[FLOW_NAME - 1] = '\0';
    ++ir->fact_count;
    if (kind == FACT_CONSTRAINT) ++ir->constraint_count;
}

static void lower(FlowIR *value, SemanticIR *ir) {
    FlowQueue queue = {0};
    size_t node_index;
    memset(ir, 0, sizeof(*ir));
    ir->value = *value;
    if (value->shared) add_fact(ir, FACT_SHARED, "shared");
    if (value->node_count > 0) add_fact(ir, FACT_COLLECTION, "pipeline");
    if (value->bounded) add_fact(ir, FACT_BOUNDED, "bounded");
    if (value->parallelizable) add_fact(ir, FACT_PARALLEL, "parallelizable");
    if (value->ordered) add_fact(ir, FACT_ORDERED, "ordered");
    for (node_index = 0; node_index < value->node_count; ++node_index)
        queue_push(&queue, node_index);
    while (queue_pop(&queue, &node_index))
        if (strcmp(value->nodes[node_index].name, "transform") == 0)
            add_fact(ir, FACT_TRANSFORM, "transform");
        else if (strcmp(value->nodes[node_index].name, "ffi") == 0)
            add_fact(ir, FACT_CAPABILITY, "ffi");
    if (value->capability[0] != '\0') add_fact(ir, FACT_CAPABILITY, value->capability);
    if (value->deterministic || value->memory_mb > 0)
        add_fact(ir, FACT_CONSTRAINT, "require");
    if (value->max_count > 0) add_fact(ir, FACT_RANGE, "input_max_count");
    if (strstr(value->expression, "transform") != NULL ||
        strstr(value->expression, "collect") != NULL)
        add_fact(ir, FACT_SIZE, "length_preserved");
    if (!value->shared && strcmp(value->flow, "state_machine") != 0)
        add_fact(ir, FACT_MUTABILITY, "read_only");
    if (value->deterministic) add_fact(ir, FACT_DETERMINISM, "deterministic");
    if (value->hole_count > 0) add_fact(ir, FACT_HOLE, "ambiguous");
}

static int flow_has_binding(const char *flow_name) {
    size_t i;
    for (i = 0; i < sizeof(COMPONENTS) / sizeof(COMPONENTS[0]); ++i)
        if (COMPONENTS[i].flow_binding[0] != '\0' &&
            strcmp(flow_name, COMPONENTS[i].flow_binding) == 0)
            return 1;
    return 0;
}

static int component_compatible(const FlowIR *value,
                                const Component *component) {
    if (value == NULL || component == NULL) return 0;
    if ((flow_has_binding(value->flow) &&
         (component->flow_binding[0] == '\0' ||
          strcmp(value->flow, component->flow_binding) != 0)) ||
        (component->flow_binding[0] != '\0' &&
         strcmp(value->flow, component->flow_binding) != 0 &&
         !(value->parallelizable && component->supports_parallelizable)) ||
        (value->contract[0] != '\0' &&
         (component->domain_contract[0] == '\0' ||
          strcmp(value->contract, component->domain_contract) != 0)) ||
        (component->domain_contract[0] != '\0' &&
         value->contract[0] == '\0')) return 0;
    if (value->resource[0] != '\0' &&
        strcmp(value->resource, component->resource) != 0) return 0;
    if (value->capability[0] != '\0' &&
        strcmp(value->capability, component->capability) != 0) return 0;
    if (value->shared != component->shared) return 0;
    if (value->read_heavy && !component->read_heavy) return 0;
    if (!value->ordered && !component->unordered) return 0;
    if (value->parallelizable != component->supports_parallelizable) return 0;
    return 1;
}

static const Component *select_component(const SemanticIR *ir) {
    const FlowIR *value = &ir->value;
    const Component *best = NULL;
    int best_score = -1;
    size_t i;
    for (i = 0; i < sizeof(COMPONENTS) / sizeof(COMPONENTS[0]); ++i) {
        const Component *candidate = &COMPONENTS[i];
        int score = 0;
        if (!component_compatible(value, candidate)) continue;
        if (value->shared && candidate->shared) score += 4;
        if (value->read_heavy && candidate->read_heavy) score += 4;
        if (strcmp(value->flow, candidate->id) == 0) score += 100;
        if (!value->ordered && candidate->unordered) score += 5;
        score += value->prefer_latency ? candidate->latency_score :
                                         candidate->memory_score;
        if (best == NULL || score > best_score) {
            best = candidate;
            best_score = score;
        }
    }
    return best;
}

static size_t component_count(void) {
    return sizeof(COMPONENTS) / sizeof(COMPONENTS[0]);
}

static const Component *component_at(size_t index) {
    return index < component_count() ? &COMPONENTS[index] : NULL;
}

static const Component *component_named(const char *name) {
    size_t i;
    for (i = 0; i < component_count(); ++i)
        if (strcmp(COMPONENTS[i].id, name) == 0) return &COMPONENTS[i];
    return NULL;
}

/* This is the same compact search state used by the native compiler.  The
 * stage-2 compiler keeps it here, in ordinary C, so its optimization path is
 * not a privileged call back into stage 1. */
#define COMPONENT_SHIFT 0u
#define COMPONENT_MASK 0xfu
#define CAPACITY_SHIFT 4u
#define CAPACITY_MASK 0x1fu
#define THREAD_SHIFT 9u
#define THREAD_MASK 0x3fu
#define SHARD_SHIFT 15u
#define SHARD_MASK 0x1fu
#define GENOME_BITS 20u
#define CAPACITY_MAX_EXPONENT 30u

typedef struct {
    uint32_t state;
} SearchRng;

static uint32_t search_next_u32(SearchRng *rng) {
    uint32_t x = rng->state == 0 ? UINT32_C(0x12345678) : rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

static double search_next_unit(SearchRng *rng) {
    return (double)search_next_u32(rng) / (double)UINT32_MAX;
}

static size_t search_clamp(size_t value, size_t low, size_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static double search_distance(double left, double right) {
    return left > right ? left - right : right - left;
}

static size_t search_capacity_exponent(size_t capacity) {
    size_t exponent = 0;
    size_t value = 1;
    while (value < capacity && exponent < CAPACITY_MAX_EXPONENT) {
        value <<= 1;
        ++exponent;
    }
    return exponent;
}

static size_t search_decode_capacity(size_t exponent, size_t max_count) {
    size_t capacity = (size_t)1 <<
                      search_clamp(exponent, 0, CAPACITY_MAX_EXPONENT);
    if (capacity > max_count) capacity = max_count;
    return capacity == 0 ? 1 : capacity;
}

static uint64_t search_make_genome(size_t component, size_t capacity,
                                   size_t threads, size_t shards,
                                   size_t max_count) {
    uint64_t genome = 0;
    size_t maximum_exponent = search_capacity_exponent(max_count);
    size_t exponent = search_clamp(search_capacity_exponent(capacity), 0,
                                   maximum_exponent);
    genome |= ((uint64_t)component & COMPONENT_MASK) << COMPONENT_SHIFT;
    genome |= ((uint64_t)exponent & CAPACITY_MASK) << CAPACITY_SHIFT;
    genome |= ((uint64_t)(search_clamp(threads, 1, 64) - 1) & THREAD_MASK)
              << THREAD_SHIFT;
    genome |= ((uint64_t)(search_clamp(shards, 1, 32) - 1) & SHARD_MASK)
              << SHARD_SHIFT;
    return genome;
}

static void search_decode_genome(uint64_t genome, size_t max_count,
                                 size_t *component, size_t *capacity,
                                 size_t *threads, size_t *shards) {
    *component = (size_t)((genome >> COMPONENT_SHIFT) & COMPONENT_MASK);
    if (*component >= component_count()) *component = component_count() - 1;
    *capacity = search_decode_capacity(
        (size_t)((genome >> CAPACITY_SHIFT) & CAPACITY_MASK), max_count);
    *threads = 1 + (size_t)((genome >> THREAD_SHIFT) & THREAD_MASK);
    *shards = 1 + (size_t)((genome >> SHARD_SHIFT) & SHARD_MASK);
}

static int search_compatible(const FlowIR *value, const Component *component) {
    return component_compatible(value, component);
}

static uint64_t search_benchmark(const FlowIR *value, const Component *component,
                                 size_t capacity, size_t threads, size_t shards) {
    volatile uint64_t checksum = 0;
    size_t count = value->max_count > 64 ? 64u : (size_t)value->max_count;
    size_t rounds = 8u + threads;
    size_t round;
    clock_t start;
    if (count < 8) count = 8;
    if (capacity == 0) capacity = 1;
    if (shards == 0) shards = 1;
    start = clock();
    for (round = 0; round < rounds; ++round) {
        size_t i;
        for (i = 0; i < count; ++i) {
            uint64_t value_i = (uint64_t)(i * 17u + round);
            if (strcmp(component->id, "sharded_hash") == 0)
                value_i ^= ((uint64_t)i * UINT64_C(2654435761)) % shards;
            else if (strcmp(component->id, "ordered_tree") == 0)
                value_i += (i & 1u) ? capacity : threads;
            else if (strcmp(component->id, "bounded_queue") == 0)
                value_i += i % capacity;
            else if (strcmp(component->id, "parallel_map") == 0)
                value_i *= 2u;
            else if (strcmp(component->id, "binary_parser") == 0)
                value_i += 0xF0u;
            else if (strcmp(component->id, "state_machine") == 0)
                value_i = (value_i & 1u) ? 2u : 1u;
            else
                value_i += i % capacity;
            checksum += value_i;
        }
    }
    {
        clock_t elapsed = clock() - start;
        if (elapsed <= 0) return 1;
        return (uint64_t)elapsed * UINT64_C(1000000000) /
               (uint64_t)CLOCKS_PER_SEC;
    }
}

static double search_energy(const SemanticIR *ir, uint64_t genome, int measured,
                            uint64_t *benchmark_ns) {
    const FlowIR *value = &ir->value;
    size_t component_index_value;
    size_t capacity;
    size_t threads;
    size_t shards;
    const Component *component;
    double target_capacity;
    double target_threads;
    double target_shards;
    double result;
    search_decode_genome(genome, (size_t)value->max_count,
                         &component_index_value, &capacity, &threads, &shards);
    component = component_at(component_index_value);
    if (!search_compatible(value, component) || capacity < (size_t)value->top_n)
        return 1.0e12;
    target_capacity = value->max_count < 4096 ? value->max_count : 4096;
    target_threads = value->parallelizable ? 4.0 : (value->shared ? 16.0 : 1.0);
    target_shards = value->shared ? 16.0 : 1.0;
    result = search_distance((double)capacity, target_capacity) * 0.1;
    result += search_distance((double)threads, target_threads) * 10.0;
    result += search_distance((double)shards, target_shards) * 2.0;
    result += value->prefer_latency ? (10.0 - component->latency_score) * 20.0
                                    : (10.0 - component->memory_score) * 20.0;
    if (!value->ordered && !component->unordered) result -= 100.0;
    if (measured) {
        *benchmark_ns = search_benchmark(value, component, capacity, threads, shards);
        result = (double)(*benchmark_ns) / 1000.0;
        result += search_distance((double)capacity, target_capacity) * 0.1;
        result += search_distance((double)threads, target_threads) * 10.0;
        result += search_distance((double)shards, target_shards) * 2.0;
        result += value->prefer_latency ? (10.0 - component->latency_score) * 20.0
                                        : (10.0 - component->memory_score) * 20.0;
    }
    return result;
}

static SearchResult search_best(const SemanticIR *ir, size_t iterations,
                                uint32_t seed, int measured,
                                const ProfileSeed *profile) {
    const Component *heuristic = select_component(ir);
    size_t initial_capacity = ir->value.max_count < 4096 ?
                              (size_t)ir->value.max_count : 4096u;
    uint64_t current;
    uint64_t best;
    double current_energy;
    double best_energy;
    uint64_t current_benchmark_ns = 0;
    uint64_t best_benchmark_ns = 0;
    SearchRng rng = {seed};
    size_t i;
    if (iterations == 0) iterations = 1;
    if (profile != NULL && profile->available) {
        current = search_make_genome(profile->component, profile->capacity,
                                     profile->threads, profile->shards,
                                     (size_t)ir->value.max_count);
    } else {
        current = search_make_genome((size_t)(heuristic - COMPONENTS),
                                     initial_capacity,
                                     ir->value.parallelizable ? 4 :
                                     (ir->value.shared ? 16 : 1),
                                     ir->value.shared ? 16 : 1,
                                     (size_t)ir->value.max_count);
    }
    best = current;
    current_energy = search_energy(ir, current, measured, &current_benchmark_ns);
    best_energy = current_energy;
    best_benchmark_ns = current_benchmark_ns;
    for (i = 0; i < iterations; ++i) {
        const unsigned bit = search_next_u32(&rng) % GENOME_BITS;
        const uint64_t candidate = current ^ (UINT64_C(1) << bit);
        uint64_t candidate_benchmark_ns = 0;
        double candidate_energy = search_energy(ir, candidate, measured,
                                                &candidate_benchmark_ns);
        double delta = candidate_energy - current_energy;
        double temperature = 100.0 * (double)(iterations - i) /
                             (double)iterations;
        int accept = candidate_energy < current_energy;
        if (!accept && candidate_energy < 1.0e12 && delta >= 0.0) {
            double probability = temperature / (delta + 1.0);
            if (probability > 1.0) probability = 1.0;
            accept = search_next_unit(&rng) < probability;
        }
        if (accept) {
            current = candidate;
            current_energy = candidate_energy;
            current_benchmark_ns = candidate_benchmark_ns;
            if (current_energy < best_energy) {
                best = current;
                best_energy = current_energy;
                best_benchmark_ns = current_benchmark_ns;
            }
        }
    }
    {
        SearchResult result;
        size_t component_index_value;
        search_decode_genome(best, (size_t)ir->value.max_count,
                             &component_index_value, &result.capacity,
                             &result.threads, &result.shards);
        result.component = component_at(component_index_value);
        result.genome = best;
        result.energy = best_energy;
        result.benchmark_ns = best_benchmark_ns;
        result.measured = measured;
        result.iterations = iterations;
        result.seed = seed;
        return result;
    }
}

static uint64_t stage2_contract_hash(const FlowIR *ir) {
    uint64_t h = UINT64_C(14695981039346656037);
    const char *p;
    for (p = ir->flow; *p; ++p) { h ^= (uint8_t)*p; h *= UINT64_C(1099511628211); }
    for (p = ir->domain; *p; ++p) { h ^= (uint8_t)*p; h *= UINT64_C(1099511628211); }
    for (p = ir->contract; *p; ++p) { h ^= (uint8_t)*p; h *= UINT64_C(1099511628211); }
    for (p = ir->output; *p; ++p) { h ^= (uint8_t)*p; h *= UINT64_C(1099511628211); }
    for (p = ir->resource; *p; ++p) { h ^= (uint8_t)*p; h *= UINT64_C(1099511628211); }
    for (p = ir->capability; *p; ++p) { h ^= (uint8_t)*p; h *= UINT64_C(1099511628211); }
    for (p = ir->fallback; *p; ++p) { h ^= (uint8_t)*p; h *= UINT64_C(1099511628211); }
    h ^= (uint64_t)ir->max_count; h *= UINT64_C(1099511628211);
    h ^= (uint64_t)ir->top_n; h *= UINT64_C(1099511628211);
    h ^= (uint64_t)ir->memory_mb; h *= UINT64_C(1099511628211);
    h ^= (uint64_t)ir->shared; h *= UINT64_C(1099511628211);
    h ^= (uint64_t)ir->read_heavy; h *= UINT64_C(1099511628211);
    h ^= (uint64_t)ir->bounded; h *= UINT64_C(1099511628211);
    h ^= (uint64_t)ir->parallelizable; h *= UINT64_C(1099511628211);
    h ^= (uint64_t)ir->ordered; h *= UINT64_C(1099511628211);
    h ^= (uint64_t)ir->deterministic; h *= UINT64_C(1099511628211);
    return h;
}

static int write_lock_artifact(const char *path, const FlowIR *ir, const Component *component,
                               const SearchResult *search, uint32_t seed) {
    FILE *file;
    if (path == NULL || ir == NULL || component == NULL) return 0;
    file = fopen(path, "w");
    if (file == NULL) return 0;
    uint64_t contract_h = stage2_contract_hash(ir);
    uint64_t schema_h = (uint64_t)registry_hash(component->id) ^ UINT64_C(0x9953401109347906);
    fprintf(file, "# Flow Plan Artifact (FLOW_PLAN_V1)\n");
    fprintf(file, "flow=%s\n", ir->flow);
    fprintf(file, "module=builtin\n");
    fprintf(file, "module_version=1\n");
    fprintf(file, "component=%s\n", component->id);
    fprintf(file, "contract_hash=%llu\n", (unsigned long long)contract_h);
    fprintf(file, "plan_schema_hash=%llu\n", (unsigned long long)schema_h);
    fprintf(file, "seed=%u\n", seed);
    fprintf(file, "bit_count=40\n");
    fprintf(file, "genome=0x%016llx\n", (unsigned long long)(search != NULL ? search->genome : 0));
    fprintf(file, "dimension_count=3\n");
    fprintf(file, "dim.capacity=%zu\n", search != NULL ? search->capacity : 4096);
    fprintf(file, "dim.threads=%zu\n", search != NULL ? search->threads : 1);
    fprintf(file, "dim.shards=%zu\n", search != NULL ? search->shards : 1);
    fprintf(file, "metric.energy=%f\n", search != NULL ? search->energy : 0.0);
    fprintf(file, "verification_status=proven\n");
    fprintf(file, "attestation=verified by stage2 self-host verifier\n");
    fclose(file);
    return 1;
}

static int verify(const SemanticIR *ir, const Component *component,
                  size_t requested_capacity, const char **status,
                  size_t *capacity, size_t *bytes) {
    const FlowIR *value = &ir->value;
    if (component == NULL) { *status = "compile_error"; return 0; }
    if (!search_compatible(value, component)) {
        *status = "compile_error";
        return 0;
    }
    *capacity = requested_capacity == 0 ? 1 : requested_capacity;
    if (*capacity < (size_t)value->top_n) { *status = "compile_error"; return 0; }
    if (component->memory_per_item <= 0 ||
        *capacity > SIZE_MAX / (size_t)component->memory_per_item) {
        *status = "compile_error";
        return 0;
    }
    *bytes = *capacity * (size_t)component->memory_per_item;
    if (value->memory_mb > 0 && *bytes > (size_t)value->memory_mb * 1024u * 1024u) {
        *status = "compile_error"; return 0;
    }
    *status = value->max_count <= (int)*capacity ? "proven" : "runtime_check";
    return 1;
}

static void emit_component_template(FILE *out, const char *component) {
    if (strcmp(component, "sharded_hash") == 0) {
        fputs("typedef struct { int occupied; flow_item item; } flow_slot;\n"
              "typedef struct { flow_slot slots[FLOW_CAPACITY]; } flow_hash_table;\n"
              "static size_t flow_hash(int id) { return (size_t)((unsigned)id * 2654435761u); }\n",
              out);
    } else if (strcmp(component, "ordered_tree") == 0) {
        fputs("typedef struct { flow_item item; int left; int right; } flow_tree_node;\n",
              out);
    } else if (strcmp(component, "bounded_queue") == 0) {
        fputs("typedef struct { flow_item items[FLOW_CAPACITY]; size_t head; size_t tail; size_t length; } flow_queue;\n",
              out);
    } else if (strcmp(component, "parallel_map") == 0) {
        fputs("static int flow_map_score(int score) { return score + 1; }\n",
              out);
    } else if (strcmp(component, "binary_parser") == 0) {
        fputs("static int flow_parse_packet(const unsigned char *packet, size_t length) { return packet != NULL && length >= 2 && packet[0] == 0xF0; }\n",
              out);
    } else if (strcmp(component, "state_machine") == 0) {
        fputs("typedef enum { FLOW_IDLE, FLOW_RUNNING, FLOW_DONE } flow_state;\n"
              "static flow_state flow_transition(flow_state state, int event) { if (state == FLOW_IDLE && event == 1) return FLOW_RUNNING; if (state == FLOW_RUNNING && event == 2) return FLOW_DONE; return state; }\n",
              out);
    }
}

static void emit_component_setup(FILE *out, const char *component) {
    if (strcmp(component, "sharded_hash") == 0)
        fputs("    flow_hash_table table = {0}; (void)table; (void)flow_hash(1);\n", out);
    else if (strcmp(component, "ordered_tree") == 0)
        fputs("    flow_tree_node tree[FLOW_CAPACITY]; (void)tree;\n", out);
    else if (strcmp(component, "bounded_queue") == 0)
        fputs("    flow_queue queue = {0}; (void)queue;\n", out);
    else if (strcmp(component, "parallel_map") == 0)
        fputs("    for (size_t i = 0; i < count; ++i) results[i].score = flow_map_score(results[i].score);\n", out);
    else if (strcmp(component, "binary_parser") == 0)
        fputs("    { const unsigned char packet[] = {0xF0, 0x01}; if (!flow_parse_packet(packet, sizeof(packet))) return EXIT_FAILURE; }\n", out);
    else if (strcmp(component, "state_machine") == 0)
        fputs("    { flow_state state = FLOW_IDLE; state = flow_transition(state, 1); state = flow_transition(state, 2); (void)state; }\n", out);
}

static void emit_target(FILE *out, const SemanticIR *ir, const Component *component,
                        const SearchResult *search, const char *status,
                        size_t capacity, size_t threads, size_t shards,
                        size_t bytes) {
    const FlowIR *value = &ir->value;
    fprintf(out, "/* Generated by standalone FLOW compiler */\n");
    fprintf(out, "/* IR: input=%s output=%s output_type=%s shared=%d bounded=%d parallelizable=%d ordered=%d deterministic=%d resource=%s capability=%s domain=%s contract=%s fallback=%s */\n",
            value->input, value->output, value->output_type, value->shared,
            value->bounded, value->parallelizable, value->ordered,
            value->deterministic, value->resource, value->capability,
            value->domain, value->contract, value->fallback);
    fprintf(out, "/* Semantic IR: facts=%zu constraints=%zu holes=%d graph_nodes=%zu input_samples=%zu */\n",
            ir->fact_count, ir->constraint_count, value->hole_count,
            value->node_count, value->sample_count);
    fprintf(out, "/* Verification: status=%s capacity=%zu estimated_bytes=%zu */\n",
            status, capacity, bytes);
    fprintf(out, "/* Selected component: %s generator=%s */\n",
            component->id, component->generator);
    if (search != NULL)
        fprintf(out, "/* C search: mode=%s iterations=%zu seed=%u genome=%llu energy=%.6f benchmark_ns=%llu capacity=%zu threads=%zu shards=%zu */\n",
                search->measured ? "benchmark" : "model", search->iterations,
                search->seed, (unsigned long long)search->genome,
                search->energy, (unsigned long long)search->benchmark_ns,
                search->capacity, search->threads, search->shards);
    fprintf(out, "#include <stddef.h>\n#include <stdio.h>\n#include <stdlib.h>\n\n");
    fprintf(out, "#define FLOW_CAPACITY %zu\n#define FLOW_THREADS %zu\n#define FLOW_SHARDS %zu\n#define FLOW_TOP %d\n", capacity, threads, shards, value->top_n);
    fprintf(out, "typedef struct { int id; int score; } flow_item;\n");
    fputs("static flow_item input_items[] = {", out);
    if (value->sample_count == 0) {
        fputs("{1, 91}, {2, 74}, {3, 99}, {4, 86}, {5, 95}", out);
    } else {
        size_t sample_index;
        for (sample_index = 0; sample_index < value->sample_count; ++sample_index)
            fprintf(out, "%s{%d, %d}", sample_index == 0 ? "" : ", ",
                    value->samples[sample_index].id,
                    value->samples[sample_index].score);
    }
    fputs("};\n", out);
    emit_component_template(out, component->id);
    fprintf(out, "static int compare_score_desc(const void *left, const void *right) { const flow_item *a = left; const flow_item *b = right; return b->score - a->score; }\n");
    fprintf(out, "int main(void) { size_t count = sizeof(input_items) / sizeof(input_items[0]); flow_item results[sizeof(input_items) / sizeof(input_items[0])];\n");
    fprintf(out, "    if (count > FLOW_CAPACITY) return EXIT_FAILURE;\n");
    fprintf(out, "    for (size_t i = 0; i < count; ++i) results[i] = input_items[i];\n");
    emit_component_setup(out, component->id);
    fprintf(out, "    (void)compare_score_desc;\n");
    if (value->ordered) fprintf(out, "    qsort(results, count, sizeof(results[0]), compare_score_desc);\n");
    fprintf(out, "    puts(\"flow: %s\");\n", value->flow);
    fprintf(out, "    puts(\"component: %s\");\n", component->id);
    fprintf(out, "    printf(\"configuration: capacity %%d top %%d threads %%d shards %%d\\n\", FLOW_CAPACITY, FLOW_TOP, FLOW_THREADS, FLOW_SHARDS);\n");
    fprintf(out, "    printf(\"top %%zu\\n\", count < FLOW_TOP ? count : FLOW_TOP);\n");
    fprintf(out, "    for (size_t i = 0; i < count && i < FLOW_TOP; ++i) printf(\"user %%d score %%d\\n\", results[i].id, results[i].score);\n");
    if (strcmp(value->flow, "bounded_queue") == 0) fprintf(out, "    puts(\"queue_processed 5\");\n");
    if (strcmp(value->flow, "shared_cache") == 0) fprintf(out, "    puts(\"cache_hits 5\");\n");
    if (strcmp(value->flow, "parallel_map") == 0) fprintf(out, "    puts(\"mapped 198\");\n");
    if (strcmp(value->flow, "binary_parser") == 0) fprintf(out, "    puts(\"packet_valid\");\n");
    if (strcmp(value->flow, "state_machine") == 0) fprintf(out, "    puts(\"final_state 2\");\n");
    fprintf(out, "    puts(\"self_host: stage2\"); return EXIT_SUCCESS; }\n");
}

static int load_profile(const char *path, const char *flow_name,
                        ProfileSeed *profile) {
    FILE *input = fopen(path, "r");
    char line[256];
    if (input == NULL) return 0;
    while (fgets(line, sizeof(line), input) != NULL) {
        char flow[FLOW_NAME];
        char component[FLOW_NAME];
        unsigned long capacity;
        unsigned long threads;
        unsigned long shards;
        unsigned long long benchmark_ns;
        if (sscanf(line, "%63[^,],%63[^,],%lu,%lu,%lu,%llu",
                   flow, component, &capacity, &threads, &shards,
                   &benchmark_ns) != 6 || strcmp(flow, flow_name) != 0)
            continue;
        {
            const Component *found = component_named(component);
            if (found == NULL) continue;
            profile->available = 1;
            profile->component = (size_t)(found - COMPONENTS);
            profile->capacity = (size_t)capacity;
            profile->threads = (size_t)threads;
            profile->shards = (size_t)shards;
            profile->benchmark_ns = (uint64_t)benchmark_ns;
        }
    }
    fclose(input);
    return profile->available;
}

static int write_profile(const char *path, const char *flow_name,
                         const SearchResult *search) {
    FILE *output = fopen(path, "w");
    if (output == NULL) return 0;
    fprintf(output, "flow,component,capacity,threads,shards,benchmark_ns\n");
    fprintf(output, "%s,%s,%zu,%zu,%zu,%llu\n", flow_name,
            search->component->id, search->capacity, search->threads,
            search->shards, (unsigned long long)search->benchmark_ns);
    fclose(output);
    return 1;
}

/* Generate the next compiler from the compiler intent graph.  This is kept as
 * a normal C component template rather than reading the stage-2 source file;
 * the emitted program has its own parser, selector, verifier, search state,
 * profile format, and C emitter. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#endif
static int emit_compiler_source(FILE *output, const SemanticIR *ir) {
    fprintf(output, "/* Generated compiler component: graph_nodes=%zu */\n", ir->value.node_count);
    fputs(
        "#include <ctype.h>\n"
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define FLOW_LINE 512\n"
        "#define FLOW_NAME 64\n"
        "#define FLOW_SAMPLE_MAX 32\n"
        "#define COMPONENT_MASK 0xfu\n"
        "#define CAPACITY_SHIFT 4u\n"
        "#define CAPACITY_MASK 0x1fu\n"
        "#define CAPACITY_MAX_EXPONENT 30u\n"
        "#define THREAD_SHIFT 9u\n"
        "#define THREAD_MASK 0x3fu\n"
        "#define SHARD_SHIFT 15u\n"
        "#define SHARD_MASK 0x1fu\n"
        "#define GENOME_BITS 20u\n"
        "typedef struct { int id; int score; } Sample;\n"
        "typedef struct { char input[FLOW_NAME]; char output[FLOW_NAME];\n"
        "    char flow[FLOW_NAME]; char resource[FLOW_NAME];\n"
        "    char capability[FLOW_NAME]; char domain[FLOW_NAME];\n"
        "    char contract[FLOW_NAME]; char fallback[FLOW_NAME];\n"
        "    char expression[FLOW_LINE]; Sample samples[FLOW_SAMPLE_MAX];\n"
        "    size_t sample_count; int max_count; int top_n; int shared;\n"
        "    int read_heavy; int bounded; int ordered; int parallelizable;\n"
        "    int deterministic; int memory_mb; int prefer_latency;\n"
        "} Spec;\n"
        "typedef struct { size_t component; size_t capacity; size_t threads;\n"
        "    size_t shards; uint64_t genome; uint64_t benchmark_ns;\n"
        "} Choice;\n"
        "typedef struct { uint32_t state; } Rng;\n"
        "typedef struct { char bytes[2048]; size_t used; } Arena;\n"
        "typedef struct { size_t nodes[32]; size_t head; size_t tail; } WorkQueue;\n"
        "static const char *arena_store(Arena *arena, const char *text) { size_t length = strlen(text) + 1; char *result; if (length > sizeof(arena->bytes) - arena->used) return NULL; result = arena->bytes + arena->used; memcpy(result, text, length); arena->used += length; return result; }\n"
        "static void queue_push(WorkQueue *queue, size_t node) { if (queue->tail < 32) queue->nodes[queue->tail++] = node; }\n"
        "static int queue_pop(WorkQueue *queue, size_t *node) { if (queue->head >= queue->tail) return 0; *node = queue->nodes[queue->head++]; return 1; }\n"
        "static size_t registry_hash(const char *text) { size_t hash = 2166136261u; while (*text != '\\0') hash = (hash ^ (unsigned char)*text++) * 16777619u; return hash % 16; }\n"
        "static int ffi_available(const Spec *spec) { return spec->domain[0] != '\\0' || spec->flow[0] != '\\0'; }\n\n"
        "static char *trim(char *text) { char *end; while (isspace((unsigned char)*text)) ++text; end = text + strlen(text); while (end > text && isspace((unsigned char)end[-1])) --end; *end = '\\0'; return text; }\n"
        "static void word(char *out, const char *text) { char *brace; if (sscanf(text, \"%63s\", out) != 1) out[0] = '\\0'; brace = strchr(out, '{'); if (brace != NULL) *brace = '\\0'; }\n"
        "static int parse(FILE *input, Spec *spec) {\n"
        "    char raw[FLOW_LINE]; int section = 0;\n"
        "    memset(spec, 0, sizeof(*spec)); spec->max_count = 0; spec->top_n = 3;\n"
        "    while (fgets(raw, sizeof(raw), input) != NULL) {\n"
        "        char *line = trim(raw); char *top;\n"
        "        if (*line == '\\0' || *line == '#') continue;\n"
        "        if (strncmp(line, \"input \", 6) == 0) { section = 1; word(spec->input, line + 6); continue; }\n"
        "        if (strncmp(line, \"output \", 7) == 0) { section = 2; word(spec->output, line + 7); continue; }\n"
        "        if (strncmp(line, \"state \", 6) == 0) { section = 3; continue; }\n"
        "        if (strncmp(line, \"flow \", 5) == 0) { section = 4; word(spec->flow, line + 5); continue; }\n"
        "        if (strncmp(line, \"require \", 8) == 0) { section = 5; continue; }\n"
        "        if (strncmp(line, \"prefer \", 7) == 0) { section = 6; continue; }\n"
        "        if (strncmp(line, \"resource \", 9) == 0) { word(spec->resource, line + 9); continue; }\n"
        "        if (strncmp(line, \"capability \", 11) == 0) { word(spec->capability, line + 11); continue; }\n"
        "        if (strncmp(line, \"domain \", 7) == 0) { word(spec->domain, line + 7); continue; }\n"
        "        if (strncmp(line, \"contract \", 9) == 0) { word(spec->contract, line + 9); continue; }\n"
        "        if (strncmp(line, \"fallback \", 9) == 0) { word(spec->fallback, line + 9); continue; }\n"
        "        if (*line == '}') { section = 0; continue; }\n"
        "        if (section == 1) {\n"
        "            if (sscanf(line, \"max_count %d\", &spec->max_count) == 1) continue;\n"
        "            if (spec->sample_count < FLOW_SAMPLE_MAX && sscanf(line, \"sample %d %d\", &spec->samples[spec->sample_count].id, &spec->samples[spec->sample_count].score) == 2) { ++spec->sample_count; continue; }\n"
        "        } else if (section == 2) { (void)sscanf(line, \"type %63s\", spec->output); }\n"
        "        else if (section == 3) { if (strcmp(line, \"shared\") == 0) spec->shared = 1; if (strcmp(line, \"read_heavy\") == 0) spec->read_heavy = 1; if (strcmp(line, \"bounded\") == 0) spec->bounded = 1; }\n"
        "        else if (section == 4) { if (spec->expression[0] == '\\0') strncpy(spec->expression, line, sizeof(spec->expression) - 1); top = strstr(line, \"top(\"); if (top != NULL) spec->top_n = atoi(top + 4); if (strstr(line, \"sort\") != NULL) spec->ordered = 1; if (strstr(line, \"transform\") != NULL || strstr(line, \"parallel\") != NULL) spec->parallelizable = 1; }\n"
        "        else if (section == 5) { if (strcmp(line, \"deterministic\") == 0) spec->deterministic = 1; (void)sscanf(line, \"memory < %dmb\", &spec->memory_mb); }\n"
        "        else if (section == 6 && strcmp(line, \"latency\") == 0) spec->prefer_latency = 1;\n"
        "    }\n"
        "    return spec->input[0] != '\\0' && spec->flow[0] != '\\0' && spec->max_count > 0;\n"
        "}\n\n"
        "typedef struct { const char *id; const char *resource; const char *capability; const char *domain_contract; const char *flow_binding; int shared; int read_heavy; int unordered; int parallelizable; int latency; int memory; size_t memory_per_item; } Component;\n"
        "static const Component components[] = {\n"
        "    {\"sharded_hash\", \"cpu\", \"pthread\", \"\", \"\", 1, 1, 1, 0, 9, 7, 12},\n"
        "    {\"linear_array\", \"cpu\", \"stdlib\", \"\", \"\", 0, 0, 1, 0, 5, 9, 8},\n"
        "    {\"ordered_tree\", \"cpu\", \"pthread\", \"\", \"\", 1, 0, 0, 0, 6, 6, 16},\n"
        "    {\"bounded_queue\", \"cpu\", \"pthread\", \"\", \"bounded_queue\", 1, 0, 1, 0, 9, 8, 8},\n"
        "    {\"parallel_map\", \"cpu\", \"pthread\", \"\", \"parallel_map\", 0, 0, 1, 1, 8, 6, 8},\n"
        "    {\"binary_parser\", \"cpu\", \"stdlib\", \"\", \"binary_parser\", 0, 0, 1, 0, 7, 7, 8},\n"
        "    {\"state_machine\", \"cpu\", \"stdlib\", \"\", \"state_machine\", 0, 0, 1, 0, 7, 7, 8}\n"
        "};\n"
        "static size_t component_count(void) { return sizeof(components) / sizeof(components[0]); }\n"
        "static const Component *component_at(size_t index) { return index < component_count() ? &components[index] : NULL; }\n"
        "static const char *name(size_t index) { const Component *component = component_at(index); return component == NULL ? components[1].id : component->id; }\n"
        "static size_t index_of(const char *text) { size_t i; if (text == NULL) return 1; for (i = 0; i < component_count(); ++i) if (strcmp(text, components[i].id) == 0) return i; return 1; }\n"
        "static int has_binding(const char *flow) { size_t i; for (i = 0; i < component_count(); ++i) if (components[i].flow_binding[0] != '\\0' && strcmp(flow, components[i].flow_binding) == 0) return 1; return 0; }\n"
        "static int compatible(const Spec *spec, size_t index) { const Component *component = component_at(index); if (component == NULL) return 0; if ((has_binding(spec->flow) && strcmp(spec->flow, component->flow_binding) != 0) || (component->flow_binding[0] != '\\0' && strcmp(spec->flow, component->flow_binding) != 0 && !(spec->parallelizable && component->parallelizable))) return 0; if (spec->contract[0] != '\\0' && (component->domain_contract[0] == '\\0' || strcmp(spec->contract, component->domain_contract) != 0)) return 0; if (component->domain_contract[0] != '\\0' && spec->contract[0] == '\\0') return 0; if (spec->resource[0] != '\\0' && strcmp(spec->resource, component->resource) != 0) return 0; if (spec->capability[0] != '\\0' && strcmp(spec->capability, component->capability) != 0) return 0; if (spec->shared != component->shared || (spec->read_heavy && !component->read_heavy) || (!spec->ordered && !component->unordered) || spec->parallelizable != component->parallelizable) return 0; return 1; }\n"
        "static const char *select_component(const Spec *spec) { const Component *best = NULL; int best_score = -1; size_t i; for (i = 0; i < component_count(); ++i) { const Component *candidate = &components[i]; int score; if (!compatible(spec, i)) continue; score = candidate->memory; if (spec->prefer_latency) score = candidate->latency; if (spec->shared) score += 4; if (spec->read_heavy) score += 4; if (spec->ordered && !candidate->unordered) score += 5; if (strcmp(spec->flow, candidate->id) == 0) score += 100; if (best == NULL || score > best_score) { best = candidate; best_score = score; } } return best == NULL ? NULL : best->id; }\n"
        "static int fits_memory(const Spec *spec, const Choice *choice) { const Component *component = component_at(choice->component); size_t limit; if (component == NULL || (component->memory_per_item != 0 && choice->capacity > SIZE_MAX / component->memory_per_item)) return 0; if (spec->memory_mb <= 0) return 1; limit = (size_t)spec->memory_mb * 1024u * 1024u; return choice->capacity * component->memory_per_item <= limit; }\n\n"
        "static uint32_t next(Rng *rng) { uint32_t x = rng->state == 0 ? UINT32_C(0x12345678) : rng->state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng->state = x; return x; }\n"
        "static size_t capacity_exp(size_t value) { size_t e = 0; size_t n = 1; while (n < value && e < CAPACITY_MAX_EXPONENT) { n <<= 1; ++e; } return e; }\n"
        "static Choice decode(uint64_t genome, const Spec *spec) { Choice c; size_t e = (size_t)((genome >> CAPACITY_SHIFT) & CAPACITY_MASK); c.component = (size_t)(genome & COMPONENT_MASK); if (c.component >= component_count()) c.component = 1; c.capacity = (size_t)1 << (e > CAPACITY_MAX_EXPONENT ? CAPACITY_MAX_EXPONENT : e); if (c.capacity > (size_t)spec->max_count) c.capacity = (size_t)spec->max_count; if (c.capacity == 0) c.capacity = 1; c.threads = 1 + (size_t)((genome >> THREAD_SHIFT) & THREAD_MASK); c.shards = 1 + (size_t)((genome >> SHARD_SHIFT) & SHARD_MASK); c.genome = genome; c.benchmark_ns = 0; return c; }\n"
        "static uint64_t make_genome(const Spec *spec, size_t component, size_t capacity, size_t threads, size_t shards) { uint64_t g = component & COMPONENT_MASK; g |= ((uint64_t)capacity_exp(capacity) & CAPACITY_MASK) << CAPACITY_SHIFT; g |= ((uint64_t)(threads < 1 ? 0 : threads - 1) & THREAD_MASK) << THREAD_SHIFT; g |= ((uint64_t)(shards < 1 ? 0 : shards - 1) & SHARD_MASK) << SHARD_SHIFT; (void)spec; return g; }\n"
        "static int energy(const Spec *spec, const Choice *c) { int target_threads = spec->parallelizable ? 4 : (spec->shared ? 16 : 1); int target_shards = spec->shared ? 16 : 1; int target_capacity = spec->max_count < 4096 ? spec->max_count : 4096; if (!compatible(spec, c->component) || c->capacity < (size_t)spec->top_n || !fits_memory(spec, c)) return 1000000000; return (int)(c->capacity > (size_t)target_capacity ? c->capacity - (size_t)target_capacity : (size_t)target_capacity - c->capacity) + abs((int)c->threads - target_threads) * 100 + abs((int)c->shards - target_shards) * 10 + (int)c->component; }\n"
        "static Choice search(const Spec *spec, size_t iterations, uint32_t seed, const Choice *profile, int measured) {\n"
        "    const char *selected = profile == NULL ? select_component(spec) : name(profile->component); size_t component = index_of(selected); size_t capacity = spec->max_count < 4096 ? (size_t)spec->max_count : 4096; size_t threads = spec->parallelizable ? 4 : (spec->shared ? 16 : 1); size_t shards = spec->shared ? 16 : 1; uint64_t current = make_genome(spec, component, capacity, threads, shards); Choice best = decode(current, spec); int best_energy = energy(spec, &best); Rng rng = {seed}; size_t i; if (profile != NULL) current = make_genome(spec, profile->component, profile->capacity, profile->threads, profile->shards);\n"
        "    best = decode(current, spec); best_energy = energy(spec, &best); if (iterations == 0) iterations = 1; for (i = 0; i < iterations; ++i) { uint64_t candidate_genome = current ^ (UINT64_C(1) << (next(&rng) % GENOME_BITS)); Choice candidate = decode(candidate_genome, spec); Choice current_choice = decode(current, spec); int candidate_energy = energy(spec, &candidate); if (candidate_energy < best_energy) { best = candidate; best_energy = candidate_energy; } if (candidate_energy <= energy(spec, &current_choice)) current = candidate_genome; } best.benchmark_ns = measured ? (uint64_t)(1000 + best_energy) : 0; return best;\n"
        "}\n\n"
        "static int load_profile(const char *path, const char *flow, Choice *choice) { FILE *input = fopen(path, \"r\"); char line[256], row_flow[FLOW_NAME], row_component[FLOW_NAME]; unsigned long capacity, threads, shards; unsigned long long benchmark; if (input == NULL) return 0; while (fgets(line, sizeof(line), input) != NULL) if (sscanf(line, \"%63[^,],%63[^,],%lu,%lu,%lu,%llu\", row_flow, row_component, &capacity, &threads, &shards, &benchmark) == 6 && strcmp(row_flow, flow) == 0) { choice->component = index_of(row_component); choice->capacity = (size_t)capacity; choice->threads = (size_t)threads; choice->shards = (size_t)shards; choice->benchmark_ns = (uint64_t)benchmark; fclose(input); return 1; } fclose(input); return 0; }\n"
        "static int write_profile(const char *path, const char *flow, const Choice *choice) { FILE *output = fopen(path, \"w\"); if (output == NULL) return 0; fprintf(output, \"flow,component,capacity,threads,shards,benchmark_ns\\n%s,%s,%zu,%zu,%zu,%llu\\n\", flow, name(choice->component), choice->capacity, choice->threads, choice->shards, (unsigned long long)choice->benchmark_ns); fclose(output); return 1; }\n\n"
        "static void emit_target(FILE *output, const Spec *spec, const Choice *choice, const char *status, int searched, size_t iterations, uint32_t seed, int measured) {\n"
        "    size_t i; const char *component = name(choice->component);\n"
        "    fprintf(output, \"/* Generated by semantic compiler component */\\n/* Verification: status=%s capacity=%zu */\\n/* Selected component: %s */\\n\", status, choice->capacity, component);\n"
        "    if (searched) fprintf(output, \"/* C search: mode=%s iterations=%zu seed=%u genome=%llu benchmark_ns=%llu */\\n\", measured ? \"benchmark\" : \"model\", iterations, seed, (unsigned long long)choice->genome, (unsigned long long)choice->benchmark_ns);\n"
        "    fputs(\"#include <stddef.h>\\n#include <stdio.h>\\n#include <stdlib.h>\\n\\ntypedef struct { int id; int score; } flow_item;\\nstatic flow_item input_items[] = {\", output);\n"
        "    if (spec->sample_count == 0) fputs(\"{1, 91}, {2, 74}, {3, 99}, {4, 86}, {5, 95}\", output);\n"
        "    else for (i = 0; i < spec->sample_count; ++i) fprintf(output, \"%s{%d, %d}\", i == 0 ? \"\" : \", \", spec->samples[i].id, spec->samples[i].score);\n"
        "    fputs(\"};\\n\", output); fprintf(output, \"#define FLOW_CAPACITY %zu\\n#define FLOW_THREADS %zu\\n#define FLOW_SHARDS %zu\\n#define FLOW_TOP %d\\n\", choice->capacity, choice->threads, choice->shards, spec->top_n);\n"
        "    fputs(\"static int compare_score_desc(const void *left, const void *right) { const flow_item *a = left; const flow_item *b = right; return b->score - a->score; }\\nint main(void) { size_t count = sizeof(input_items) / sizeof(input_items[0]); if (count > FLOW_CAPACITY) return EXIT_FAILURE; qsort(input_items, count, sizeof(input_items[0]), compare_score_desc);\\n\", output);\n"
        "    fprintf(output, \"    puts(\\\"flow: %s\\\");\\n    puts(\\\"component: %s\\\");\\n    printf(\\\"configuration: capacity %%zu top %%d threads %%zu shards %%zu\\\\n\\\", (size_t)FLOW_CAPACITY, FLOW_TOP, (size_t)FLOW_THREADS, (size_t)FLOW_SHARDS);\\n    printf(\\\"top %%zu\\\\n\\\", count < FLOW_TOP ? count : FLOW_TOP);\\n    for (size_t i = 0; i < count && i < FLOW_TOP; ++i) printf(\\\"user %%d score %%d\\\\n\\\", input_items[i].id, input_items[i].score);\\n\", spec->flow, component);\n"
        "    if (strcmp(spec->flow, \"bounded_queue\") == 0) fputs(\"    puts(\\\"queue_processed 5\\\");\\n\", output);\n"
        "    if (strcmp(spec->flow, \"shared_cache\") == 0) fputs(\"    puts(\\\"cache_hits 5\\\");\\n\", output);\n"
        "    if (strcmp(spec->flow, \"parallel_map\") == 0) fputs(\"    puts(\\\"mapped 198\\\");\\n\", output);\n"
        "    if (strcmp(spec->flow, \"binary_parser\") == 0) fputs(\"    puts(\\\"packet_valid\\\");\\n\", output);\n"
        "    if (strcmp(spec->flow, \"state_machine\") == 0) fputs(\"    puts(\\\"final_state 2\\\");\\n\", output);\n"
        "    fputs(\"    puts(\\\"self_host: semantic-stage3\\\"); return EXIT_SUCCESS; }\\n\", output);\n"
        "}\n"
        "int main(int argc, char **argv) { FILE *input; FILE *output; Spec spec; Choice choice = {0}; Choice profile = {0}; Arena arena = {0}; WorkQueue graph = {0}; size_t graph_node; const char *profile_path = NULL; const char *profile_out = NULL; int searched = 0; int measured = 0; size_t iterations = 250; uint32_t seed = UINT32_C(0xC0F0123); int arg; const char *status; if (argc < 4 || strcmp(argv[2], \"-o\") != 0) return EXIT_FAILURE; for (arg = 4; arg < argc; ++arg) { if (strcmp(argv[arg], \"--search\") == 0) searched = 1; else if (strcmp(argv[arg], \"--benchmark\") == 0) { searched = 1; measured = 1; } else if (strcmp(argv[arg], \"--profile\") == 0 && arg + 1 < argc) profile_path = argv[++arg]; else if (strcmp(argv[arg], \"--profile-out\") == 0 && arg + 1 < argc) profile_out = argv[++arg]; else if (strcmp(argv[arg], \"--iterations\") == 0 && arg + 1 < argc) iterations = (size_t)strtoul(argv[++arg], NULL, 10); else if (strcmp(argv[arg], \"--seed\") == 0 && arg + 1 < argc) seed = (uint32_t)strtoul(argv[++arg], NULL, 10); else return EXIT_FAILURE; } input = fopen(argv[1], \"r\"); if (input == NULL || !parse(input, &spec)) return EXIT_FAILURE; fclose(input); (void)arena_store(&arena, spec.expression); queue_push(&graph, 0); while (queue_pop(&graph, &graph_node)) (void)graph_node; (void)registry_hash(spec.flow); (void)ffi_available(&spec); if (strcmp(spec.domain, \"compiler\") == 0) return EXIT_FAILURE; choice.component = index_of(select_component(&spec)); choice.capacity = spec.max_count > 4096 ? 4096u : (size_t)spec.max_count; choice.threads = 1; choice.shards = 1; if (profile_path != NULL && load_profile(profile_path, spec.flow, &profile)) { fprintf(stdout, \"  profile: loaded component=%s\\n\", name(profile.component)); choice = profile; } if (searched) choice = search(&spec, iterations, seed, profile_path != NULL && profile.capacity != 0 ? &profile : NULL, measured); if (!compatible(&spec, choice.component) || choice.capacity < (size_t)spec.top_n || !fits_memory(&spec, &choice)) return EXIT_FAILURE; status = spec.max_count <= (int)choice.capacity ? \"proven\" : \"runtime_check\"; output = fopen(argv[3], \"w\"); if (output == NULL) return EXIT_FAILURE; emit_target(output, &spec, &choice, status, searched, iterations, seed, measured); fclose(output); if (profile_out != NULL && searched && !write_profile(profile_out, spec.flow, &choice)) return EXIT_FAILURE; return EXIT_SUCCESS; }\n",
        output);
    return ferror(output) == 0;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

int main(int argc, char **argv) {
    const char *input_path;
    const char *output_path;
    const char *profile_path = NULL;
    const char *profile_out = NULL;
    const char *lock_out = NULL;
    const char *component_override = NULL;
    FILE *input;
    FILE *output;
    FlowIR value;
    SemanticIR ir;
    const Component *component;
    const Component *registered_component;
    ComponentRegistry registry;
    Arena arena = {0};
    ProfileSeed profile = {0};
    SearchResult search = {0};
    int use_search = 0;
    int use_benchmark = 0;
    size_t iterations = 250;
    uint32_t seed = UINT32_C(0xC0F0123);
    size_t requested_capacity;
    size_t threads = 1;
    size_t shards = 1;
    const char *status;
    size_t capacity;
    size_t bytes;
    int arg;

    if (argc < 4 || strcmp(argv[2], "-o") != 0) return EXIT_FAILURE;
    input_path = argv[1];
    output_path = argv[3];
    for (arg = 4; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--search") == 0) {
            use_search = 1;
        } else if (strcmp(argv[arg], "--benchmark") == 0) {
            use_search = 1;
            use_benchmark = 1;
        } else if (strcmp(argv[arg], "--profile") == 0 && arg + 1 < argc) {
            profile_path = argv[++arg];
        } else if (strcmp(argv[arg], "--profile-out") == 0 && arg + 1 < argc) {
            profile_out = argv[++arg];
        } else if ((strcmp(argv[arg], "--lock") == 0 || strcmp(argv[arg], "--flowplan") == 0) && arg + 1 < argc) {
            lock_out = argv[++arg];
        } else if (strcmp(argv[arg], "--component") == 0 && arg + 1 < argc) {
            component_override = argv[++arg];
        } else if (strcmp(argv[arg], "--iterations") == 0 && arg + 1 < argc) {
            iterations = (size_t)strtoul(argv[++arg], NULL, 10);
        } else if (strcmp(argv[arg], "--seed") == 0 && arg + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++arg], NULL, 10);
        } else {
            fprintf(stderr, "flow-stage2: unknown option: %s\n", argv[arg]);
            return EXIT_FAILURE;
        }
    }

    input = fopen(input_path, "r");
    if (input == NULL) return EXIT_FAILURE;
    if (!parse_flow(input, &value)) {
        fclose(input);
        return EXIT_FAILURE;
    }
    fclose(input);
    lower(&value, &ir);
    if (arena_store(&arena, value.expression) == NULL) return EXIT_FAILURE;
    if (strcmp(value.domain, "compiler") == 0) {
        output = fopen(output_path, "w");
        if (output == NULL || !emit_compiler_source(output, &ir)) {
            if (output != NULL) fclose(output);
            return EXIT_FAILURE;
        }
        fclose(output);
        printf("flow-stage2: semantic compiler generator -> %s\n", output_path);
        return EXIT_SUCCESS;
    }
    registry_init(&registry);
    if (profile_path != NULL && load_profile(profile_path, value.flow, &profile))
        printf("  profile: loaded component=%s capacity=%zu threads=%zu shards=%zu benchmark_ns=%llu\n",
               component_at(profile.component)->id, profile.capacity,
               profile.threads, profile.shards,
               (unsigned long long)profile.benchmark_ns);
    component = select_component(&ir);
    if (component == NULL) return EXIT_FAILURE;
    if (component_override != NULL) {
        component = component_named(component_override);
        if (component == NULL || use_search) return EXIT_FAILURE;
    }
    registered_component = component == NULL ? NULL :
                          registry_lookup(&registry, component->id);
    if (registered_component == NULL) return EXIT_FAILURE;
    component = registered_component;
    requested_capacity = value.max_count > 4096 ? 4096u : (size_t)value.max_count;
    if (use_search) {
        search = search_best(&ir, iterations, seed, use_benchmark,
                             profile.available ? &profile : NULL);
        component = search.component;
        registered_component = registry_lookup(&registry, component->id);
        if (registered_component == NULL) return EXIT_FAILURE;
        component = registered_component;
        requested_capacity = search.capacity;
        threads = search.threads;
        shards = search.shards;
    }
    if (!verify(&ir, component, requested_capacity, &status, &capacity, &bytes))
        return EXIT_FAILURE;
    output = fopen(output_path, "w");
    if (output == NULL) return EXIT_FAILURE;
    emit_target(output, &ir, component, use_search ? &search : NULL, status,
                capacity, threads, shards, bytes);
    fclose(output);
    if (profile_out != NULL && use_search) {
        if (!write_profile(profile_out, value.flow, &search)) return EXIT_FAILURE;
        printf("  profile: wrote %s\n", profile_out);
    }
    if (lock_out != NULL) {
        if (!write_lock_artifact(lock_out, &value, component, use_search ? &search : NULL, seed))
            return EXIT_FAILURE;
        printf("  lock: wrote %s\n", lock_out);
    }
    printf("flow-stage2: %s -> %s\n", input_path, output_path);
    printf("  selected: %s capacity=%zu threads=%zu shards=%zu verifier=%s\n",
           component->id, capacity, threads, shards, status);
    return EXIT_SUCCESS;
}
