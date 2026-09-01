#include "registry.h"
#include "search.h"
#include "backend.h"
#include "verifier.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* 1. Legacy External Plugin Test                                            */
/* ========================================================================= */

static int plugin_validate_contract(const SemanticIR *ir,
                                     const FlowPlugin *plugin,
                                     char *message, size_t message_size) {
    (void)plugin;
    if (ir == NULL || ir->declared_constraint_count != 1u ||
        strcmp(ir->constraints[0].name, "external_mode") != 0) {
        if (message != NULL && message_size != 0)
            snprintf(message, message_size, "requires external_mode constraint");
        return 0;
    }
    return 1;
}

static int plugin_compatible(const SemanticIR *ir,
                             const Component *component) {
    if (ir == NULL || ir->declared_constraint_count != 1u ||
        strcmp(ir->constraints[0].name, "external_mode") != 0)
        return 0;
    (void)component;
    return strcmp(ir->domain_name, "external_test") == 0;
}

static int plugin_memory(const SemanticIR *ir, const Component *component,
                         size_t capacity, size_t shards,
                         size_t *estimated_bytes) {
    (void)ir;
    (void)component;
    (void)shards;
    if (estimated_bytes == NULL || capacity > (size_t)-1 / 3u) return 0;
    *estimated_bytes = 64u + capacity * 3u;
    return 1;
}

static int plugin_dimensions(const SemanticIR *ir, const Component *component,
                             FlowPlanDimensionSet *dims_out) {
    (void)ir;
    (void)component;
    if (dims_out == NULL) return 0;
    dims_out->count = 2;
    dims_out->dimensions[0] = (FlowPlanDimension){"capacity", FLOW_DIM_EXPONENT, 1, 20, 1, 4};
    dims_out->dimensions[1] = (FlowPlanDimension){"custom_batch", FLOW_DIM_LINEAR, 1, 128, 8, 16};
    return 1;
}

static int plugin_evaluate_plan(const SemanticIR *ir,
                               const Component *component,
                               const FlowPlanAssignment *plan,
                               FlowPlanMetrics *metrics_out) {
    size_t cap;
    (void)ir;
    if (metrics_out == NULL || component == NULL || plan == NULL) return 0;
    cap = plan->count > 0 ? (size_t)plan->values[0] : 16u;
    metrics_out->capacity = cap;
    metrics_out->threads = 1;
    metrics_out->shards = 1;
    metrics_out->memory_bytes = 64u + cap * 3u;
    metrics_out->latency_score = 1.0;
    metrics_out->throughput_score = 10.0;
    metrics_out->energy = 1.0;
    return 1;
}

static int plugin_verify_plan(const SemanticIR *ir,
                              const Component *component,
                              const FlowPlanAssignment *plan,
                              VerificationReport *report_out) {
    size_t cap;
    (void)ir;
    (void)component;
    if (report_out == NULL || plan == NULL) return 0;
    cap = plan->count > 0 ? (size_t)plan->values[0] : 0u;
    if (cap < 4u) {
        report_out->status = VERIFIER_COMPILE_ERROR;
        snprintf(report_out->message, sizeof(report_out->message), "external capacity too small");
        return 0;
    }
    report_out->capacity = cap;
    report_out->estimated_bytes = 64u + cap * 3u;
    report_out->max_count_proven = 1;
    report_out->status = VERIFIER_PROVEN;
    snprintf(report_out->message, sizeof(report_out->message), "external plan proven");
    return 1;
}

static int plugin_verify(const SemanticIR *ir, const Component *component,
                         size_t capacity, size_t shards,
                         char *message, size_t message_size) {
    (void)ir;
    (void)component;
    (void)shards;
    if (capacity < 4u) {
        if (message != NULL && message_size != 0)
            snprintf(message, message_size, "external capacity too small");
        return 0;
    }
    return 1;
}

static int plugin_emit(FILE *output, const SemanticIR *ir,
                       const Component *component,
                       const struct FlowSearchResult *search,
                       const struct FlowVerificationReport *verification,
                       int reload_adapter) {
    (void)ir;
    (void)component;
    (void)search;
    (void)verification;
    if (reload_adapter) return 0;
    return fputs("/* external plugin emitted */\n", output) >= 0;
}

static int plugin_oracle(const char *fixture_path, char *message,
                         size_t message_size) {
    if (fixture_path == NULL) return 0;
    if (message != NULL && message_size != 0)
        snprintf(message, message_size, "checked %s", fixture_path);
    return 1;
}

static const Component COMPONENTS[] = {
    {"external_test", "test", "cpu", "stdlib", 0, 0, 1, 0, 1, 99,
     "", "", 64u, 3u, 0}
};

static const FlowPlugin PLUGIN = {
    "external_test",
    "1",
    COMPONENTS,
    1u,
    plugin_compatible,
    plugin_memory,
    plugin_verify,
    plugin_emit,
    plugin_oracle,
    NULL,
    plugin_validate_contract,
    NULL,
    NULL,
    plugin_dimensions,
    plugin_evaluate_plan,
    plugin_verify_plan,
    NULL
};

/* ========================================================================= */
/* 2. Custom Domain Plugin with tile/batch/layout & 2 Candidates             */
/* ========================================================================= */

typedef struct {
    int validated_tag;
    char domain_feature[32];
} CustomTileDomainCtx;

static void custom_tile_free_ctx(void *ctx) {
    free(ctx);
}

static void custom_tile_lower_semantics(const FlowSpec *spec, SemanticIR *ir,
                                        const FlowPlugin *plugin) {
    (void)spec;
    (void)plugin;
    CustomTileDomainCtx *ctx = (CustomTileDomainCtx *)malloc(sizeof(CustomTileDomainCtx));
    if (ctx != NULL) {
        ctx->validated_tag = 42;
        snprintf(ctx->domain_feature, sizeof(ctx->domain_feature), "tile_engine_v1");
        ir->domain_ctx = ctx;
        ir->domain_ctx_free = custom_tile_free_ctx;
    }
}

static int custom_tile_validate_contract(const SemanticIR *ir,
                                         const FlowPlugin *plugin,
                                         char *message, size_t message_size) {
    (void)plugin;
    if (ir == NULL) return 0;
    if (ir->domain_ctx == NULL) {
        if (message && message_size) snprintf(message, message_size, "missing domain ctx");
        return 0;
    }
    CustomTileDomainCtx *ctx = (CustomTileDomainCtx *)ir->domain_ctx;
    if (ctx->validated_tag != 42) {
        if (message && message_size) snprintf(message, message_size, "corrupted domain tag");
        return 0;
    }
    return 1;
}

static int custom_tile_compatible(const SemanticIR *ir,
                                  const Component *component) {
    if (ir == NULL || component == NULL) return 0;
    return strcmp(ir->plugin_name, "custom_tile") == 0;
}

static int custom_tile_dimensions(const SemanticIR *ir, const Component *component,
                                  FlowPlanDimensionSet *dims_out) {
    (void)ir;
    if (dims_out == NULL || component == NULL) return 0;
    dims_out->count = 3;
    dims_out->dimensions[0] = (FlowPlanDimension){"tile", FLOW_DIM_LINEAR, 8, 64, 8, 16};
    dims_out->dimensions[1] = (FlowPlanDimension){"batch", FLOW_DIM_LINEAR, 1, 1024, 16, 32};
    dims_out->dimensions[2] = (FlowPlanDimension){"layout", FLOW_DIM_DISCRETE, 0, 1, 1, 0};
    return 1;
}

static int custom_tile_evaluate_plan(const SemanticIR *ir,
                                    const Component *component,
                                    const FlowPlanAssignment *plan,
                                    FlowPlanMetrics *metrics_out) {
    uint64_t tile, batch, layout;
    (void)ir;
    if (metrics_out == NULL || component == NULL || plan == NULL) return 0;
    tile = plan->count > 0 ? plan->values[0] : 16;
    batch = plan->count > 1 ? plan->values[1] : 32;
    layout = plan->count > 2 ? plan->values[2] : 0;

    memset(metrics_out, 0, sizeof(*metrics_out));
    metrics_out->capacity = (size_t)(tile * batch);
    metrics_out->threads = 1;
    metrics_out->shards = 1;
    metrics_out->memory_bytes = (size_t)(tile * batch * 4 + (layout == 1 ? 512 : 128));

    if (strcmp(component->id, "tile_gemm_a") == 0) {
        metrics_out->latency_score = 2.0 + (double)tile * 0.1;
        metrics_out->throughput_score = (double)batch * 2.0;
        metrics_out->energy = metrics_out->latency_score * 5.0 + (double)metrics_out->memory_bytes / 512.0;
    } else {
        /* tile_gemm_b */
        metrics_out->latency_score = 4.0 + (double)tile * 0.05;
        metrics_out->throughput_score = (double)batch * 3.0;
        metrics_out->energy = metrics_out->latency_score * 3.0 + (double)metrics_out->memory_bytes / 1024.0;
    }
    if (ir != NULL && (size_t)ir->input_max_count > metrics_out->capacity) {
        metrics_out->energy += 1000.0;
    }
    return 1;
}

static int custom_tile_verify_plan(const SemanticIR *ir,
                                  const Component *component,
                                  const FlowPlanAssignment *plan,
                                  VerificationReport *report_out) {
    uint64_t tile, batch;
    (void)ir;
    (void)component;
    if (report_out == NULL || plan == NULL) return 0;
    tile = plan->count > 0 ? plan->values[0] : 0;
    batch = plan->count > 1 ? plan->values[1] : 0;

    if (tile < 8 || batch == 0) {
        report_out->status = VERIFIER_COMPILE_ERROR;
        snprintf(report_out->message, sizeof(report_out->message), "invalid tile or batch");
        return 0;
    }
    report_out->capacity = (size_t)(tile * batch);
    report_out->estimated_bytes = (size_t)(tile * batch * 4);
    report_out->max_count_proven = 1;
    report_out->status = VERIFIER_PROVEN;
    snprintf(report_out->message, sizeof(report_out->message), "custom tile plan proven");
    return 1;
}

static uint64_t custom_tile_benchmark(const SemanticIR *ir,
                                      const Component *component,
                                      const FlowPlanAssignment *plan) {
    uint64_t tile = plan && plan->count > 0 ? plan->values[0] : 16;
    uint64_t batch = plan && plan->count > 1 ? plan->values[1] : 32;
    (void)ir;
    if (strcmp(component->id, "tile_gemm_a") == 0) {
        return tile * 100 + batch * 20;
    }
    return tile * 50 + batch * 30;
}

static int custom_tile_emit(FILE *output, const SemanticIR *ir,
                            const Component *component,
                            const struct FlowSearchResult *search,
                            const struct FlowVerificationReport *verification,
                            int reload_adapter) {
    (void)ir;
    (void)component;
    (void)search;
    (void)verification;
    (void)reload_adapter;
    return fputs("/* custom_tile: kernel emitted successfully */\n"
                 "int tile_run(void) { return 42; }\n", output) >= 0;
}

static const Component CUSTOM_TILE_COMPONENTS[] = {
    {"tile_gemm_a", "kernel", "gpu", "simd", 0, 0, 1, 1, 9, 8, "", "", 128u, 4u, 0},
    {"tile_gemm_b", "kernel", "gpu", "simd", 0, 0, 1, 1, 7, 9, "", "", 512u, 4u, 0}
};

static const FlowPlugin CUSTOM_TILE_PLUGIN = {
    "custom_tile",
    "1.0",
    CUSTOM_TILE_COMPONENTS,
    2u,
    custom_tile_compatible,
    NULL,
    NULL,
    custom_tile_emit,
    NULL,
    NULL,
    custom_tile_validate_contract,
    custom_tile_lower_semantics,
    custom_tile_free_ctx,
    custom_tile_dimensions,
    custom_tile_evaluate_plan,
    custom_tile_verify_plan,
    custom_tile_benchmark
};

/* ========================================================================= */
/* Main Test Runner                                                          */
/* ========================================================================= */

int main(void) {
    FlowSpec spec;
    SemanticIR ir;
    const Component *selected;
    const FlowPlugin *owner;
    const FlowPlugin *lookup;
    size_t before;
    size_t estimated = 0;
    char message[64];
    FILE *output;
    char line[128];
    FILE *spec_input;
    FlowPlanDimensionSet dims;
    FlowPlanAssignment plan;
    FlowPlanMetrics metrics;
    VerificationReport v_report;

    if (!flow_registry_init()) return 1;

    /* --------------------------------------------------------------------- */
    /* Part 1: Register and test legacy external plugin                      */
    /* --------------------------------------------------------------------- */
    before = component_count();
    if (!flow_registry_register(&PLUGIN) || component_count() != before + 1u)
        return 1;
    lookup = flow_registry_lookup("external_test");
    if (lookup != &PLUGIN) return 1;

    spec_input = tmpfile();
    if (spec_input == NULL) return 1;
    fputs("input items {\n"
          " max_count 16\n"
          "}\n"
          "flow external_flow {\n"
          " items -> consume\n"
          "}\n"
          "require {\n"
          " external_mode fast\n"
          "}\n"
          "import external_test\n"
          "domain external_test {\n"
          "}\n", spec_input);
    rewind(spec_input);
    if (!parse_spec(spec_input, &spec)) {
        fclose(spec_input);
        return 1;
    }
    fclose(spec_input);
    lower_to_ir(&spec, &ir);
    selected = select_component(&ir);
    if (selected != &COMPONENTS[0]) return 1;
    owner = flow_component_plugin(selected);
    if (owner != &PLUGIN) return 1;

    if (!flow_component_dimensions(&ir, selected, &dims) || dims.count != 2)
        return 1;

    plan.count = 2;
    plan.values[0] = 16u;
    plan.values[1] = 32u;
    if (!flow_component_evaluate(&ir, selected, &plan, &metrics) ||
        metrics.memory_bytes != 112u)
        return 1;

    if (!flow_component_verify_plan(&ir, selected, &plan, &v_report) ||
        v_report.status != VERIFIER_PROVEN)
        return 1;

    if (!flow_component_memory(&ir, selected, 16u, 1u, &estimated) ||
        estimated != 112u)
        return 1;
    if (!flow_component_verify(&ir, selected, 16u, 1u, message,
                               sizeof(message)))
        return 1;
    output = tmpfile();
    if (output == NULL || !flow_component_emit(output, &ir, selected, NULL,
                                               NULL, 0))
        return 1;
    rewind(output);
    if (fgets(line, sizeof(line), output) == NULL ||
        strstr(line, "external plugin emitted") == NULL) {
        fclose(output);
        return 1;
    }
    fclose(output);
    if (!flow_plugin_run_oracle(&PLUGIN, "fixture", message, sizeof(message)) ||
        strstr(message, "fixture") == NULL)
        return 1;

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "plugin-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; }

    /* --------------------------------------------------------------------- */
    /* Part 2: Custom domain plugin with tile/batch/layout & 2 candidates    */
    /* --------------------------------------------------------------------- */
    before = component_count();
    CHECK(flow_registry_register(&CUSTOM_TILE_PLUGIN) && component_count() == before + 2u);
    lookup = flow_registry_lookup("custom_tile");
    CHECK(lookup == &CUSTOM_TILE_PLUGIN);

    spec_input = tmpfile();
    CHECK(spec_input != NULL);
    fputs("input matrix_items {\n"
          " max_count 64\n"
          "}\n"
          "flow tile_flow {\n"
          " matrix_items -> transform -> collect\n"
          "}\n"
          "resource gpu\n"
          "capability simd\n"
          "import custom_tile\n", spec_input);
    rewind(spec_input);
    CHECK(parse_spec(spec_input, &spec));
    fclose(spec_input);

    lower_to_ir(&spec, &ir);
    flow_plugin_lower_semantics(&spec, &ir, &CUSTOM_TILE_PLUGIN);
    CHECK(ir.domain_ctx != NULL);

    CHECK(flow_component_validate_contract(&ir, &CUSTOM_TILE_PLUGIN, message, sizeof(message)));

    /* Verify candidate compatibility */
    CHECK(compatible_component_count(&ir) == 2u);

    /* Test Plan Dimensions Enumeration */
    const Component *comp_a = compatible_component_at(&ir, 0);
    const Component *comp_b = compatible_component_at(&ir, 1);
    CHECK(comp_a != NULL && comp_b != NULL);

    CHECK(flow_component_dimensions(&ir, comp_a, &dims) && dims.count == 3);
    CHECK(strcmp(dims.dimensions[0].name, "tile") == 0);
    CHECK(strcmp(dims.dimensions[1].name, "batch") == 0);
    CHECK(strcmp(dims.dimensions[2].name, "layout") == 0);

    /* Test Plan Search across tile, batch, layout */
    SearchResult search_res = search_best(&ir, 100, 42, 0, NULL);
    CHECK(search_res.component != NULL);
    CHECK(search_res.dimension_set.count == 3);
    CHECK(search_res.assignment.count == 3);
    CHECK(search_res.pareto.count > 0);

    /* Test Measured Search (Benchmark mode) */
    SearchResult search_bench = search_best(&ir, 50, 42, 1, NULL);
    CHECK(search_bench.component != NULL && search_bench.benchmark_ns > 0);

    /* Test Verification */
    int v_ok = verify_candidate(&ir, search_res.component, &search_res, &v_report);
    CHECK(v_ok && v_report.status == VERIFIER_PROVEN);

    /* Test Code Generation */
    output = tmpfile();
    CHECK(output != NULL);
    CHECK(emit_c(output, &ir, search_res.component, &search_res, &v_report, 0));
    rewind(output);
    int found_kernel = 0;
    while (fgets(line, sizeof(line), output) != NULL) {
        if (strstr(line, "custom_tile: kernel emitted") != NULL) {
            found_kernel = 1;
            break;
        }
    }
    fclose(output);
    CHECK(found_kernel);

    /* Test FlowPlanArtifact Persistence for Custom Tile Plugin */
    FlowPlanArtifact custom_art;
    CHECK(flow_search_result_to_artifact(&ir, &search_res, &custom_art));
    CHECK(custom_art.dimensions.count == 3);
    CHECK(custom_art.plan_schema_hash != 0);

    FILE *art_file = tmpfile();
    CHECK(art_file != NULL);
    CHECK(flow_plan_artifact_save(art_file, &custom_art));
    rewind(art_file);

    FlowPlanArtifact loaded_custom_art;
    CHECK(flow_plan_artifact_load(art_file, &loaded_custom_art));
    fclose(art_file);

    CHECK(loaded_custom_art.dimensions.count == 3);
    CHECK(strcmp(loaded_custom_art.dimensions.dimensions[0].name, "tile") == 0);
    CHECK(strcmp(loaded_custom_art.dimensions.dimensions[1].name, "batch") == 0);
    CHECK(strcmp(loaded_custom_art.dimensions.dimensions[2].name, "layout") == 0);
    CHECK(loaded_custom_art.plan.values[0] == search_res.assignment.values[0]);
    CHECK(loaded_custom_art.plan.values[1] == search_res.assignment.values[1]);
    CHECK(loaded_custom_art.plan.values[2] == search_res.assignment.values[2]);

    /* ===================================================================== */
    /* 4. Test Dynamic DSO Plugin Loading & Descriptor Verification          */
    /* ===================================================================== */
    {
        /* Create and compile sample DSO plugin */
        FILE *dso_c = fopen("/tmp/flow_sample_dso.c", "w");
        CHECK(dso_c != NULL);
        fputs("#include \"plugin.h\"\n"
              "#include \"reload.h\"\n"
              "#include <string.h>\n"
              "static int dso_comp_compat(const SemanticIR *ir, const Component *comp) {\n"
              "    (void)comp; return ir != NULL && strcmp(ir->domain_name, \"dso_domain\") == 0;\n"
              "}\n"
              "static int dso_comp_emit(FILE *out, const SemanticIR *ir, const Component *comp, const struct FlowSearchResult *sr, const struct FlowVerificationReport *vr, int reload) {\n"
              "    (void)ir; (void)comp; (void)sr; (void)vr; (void)reload; fputs(\"/* dso kernel */\\n\", out); return 1;\n"
              "}\n"
              "static const Component DSO_COMPONENTS[] = {\n"
              "    {\"dso_kernel\", \"compute\", \"cpu\", \"stdlib\", 0, 0, 1, 0, 9, 9, \"dso_contract\", \"\", 0, 4, 1}\n"
              "};\n"
              "static const FlowPlugin DSO_PLUGIN = {\n"
              "    \"dso_module\", \"1.0\", DSO_COMPONENTS, 1, dso_comp_compat, NULL, NULL, dso_comp_emit, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL\n"
              "};\n"
              "static const FlowPluginDescriptor DSO_DESCRIPTOR = {\n"
              "    FLOW_PLUGIN_ABI_MAJOR, FLOW_PLUGIN_ABI_MINOR, sizeof(FlowPluginDescriptor),\n"
              "    \"dso_module\", \"1.0\", 0x12345678, &DSO_PLUGIN, NULL, 0\n"
              "};\n"
              "const FlowPluginDescriptor *flow_plugin_entry_v1(void) {\n"
              "    return &DSO_DESCRIPTOR;\n"
              "}\n", dso_c);
        fclose(dso_c);

        int compile_res = system("cc -shared -fPIC -Isrc /tmp/flow_sample_dso.c -o /tmp/flow_sample_dso.so");
        CHECK(compile_res == 0);

        char dso_err[256];
        /* Positive DSO load */
        CHECK(flow_registry_load_dso("/tmp/flow_sample_dso.so", dso_err, sizeof(dso_err)));
        const FlowPlugin *loaded = flow_registry_lookup("dso_module");
        CHECK(loaded != NULL);
        CHECK(strcmp(loaded->name, "dso_module") == 0);
        CHECK(loaded->component_count == 1);
        CHECK(strcmp(loaded->components[0].id, "dso_kernel") == 0);

        /* Negative: non-existent file */
        CHECK(!flow_registry_load_dso("/tmp/missing_file.so", dso_err, sizeof(dso_err)));
        CHECK(strstr(dso_err, "dlopen failed") != NULL);
    }

    printf("PLUGIN_TEST=passed registered=%zu selected=custom_tile dimensions=3 candidates=2 dso_loading=verified\n",
           component_count());
    return 0;
}
