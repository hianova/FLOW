#include "backend.h"
#include "flow.h"
#include "registry.h"
#include "bitspace.h"
#include "abi.h"
#include "smt.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
static void flowc_print_usage(FILE *out) { fprintf(out, "FLOW Modern Declarative Compiler (flowc) v2.5.0 | Lean-RT, Cubical HoTT & QSBR\n"
        "Usage: flowc <input.lean|input.flow> [-o <output.c>] [options]\n"
        "  -o <output.c>              Target C17 source path (default: <input>.c)\n"
        "  --target-c-header <file.h> Emit static inline double/fx64 C17 header & FlowStatusCode\n"
        "  --target-rust <file.rs>    Emit zero-copy #[repr(C)] Rust FFI adapter\n"
        "  --target-python <file.py>  Emit ctypes.Structure Python zero-copy adapter\n"
        "  --target-js <file.mjs>     Emit Bun/Node.js TypedArray zero-copy adapter\n"
        "  --target-cuda <file.cuh>   Emit CUDA host-launch & device extent header\n"
        "  --target-matlab <file.m>   Emit IR-driven MATLAB handle class adapter\n"
        "  --target-julia <file.jl>   Emit Julia ccall & @enum zero-copy adapter\n"
        "  --target-ui <file.html>    Emit Algorithmic Control Plane & SoD Ledger HTML5 UI\n"
        "  --target-lean <file.lean>  Emit & verify Lean 4 formal soundness certificate\n"
        "  --target-llvm-ir / --target-mlir <file> Emit LLVM IR or MLIR module\n"
        "  --apply-fvec <file.fvec>   Apply pre-baked SMT-certified architecture weights\n"
        "  --profile-out <file>       Export crystallized plan as .fvec, .flowplan, or CSV\n"
        "  --search / --benchmark     Enable BMF combinatorial search / benchmarking\n"
        "  --iterations <N> / --seed <N> / --component <id> / --reload-adapter\n"
        "  -v, --version / -h, --help Show version or usage information\n"); } static int validate_and_lower_module(const char *mod_name, const FlowSpec *spec, SemanticIR *ir) {
    flow_registry_resolve_plugin(mod_name); const FlowPlugin *module = flow_registry_lookup(mod_name); char err_msg[128] = {0};
    if (!module) { fprintf(stderr, "flowc: imported domain module '%s' not registered\n", mod_name); return 0; } flow_plugin_lower_semantics(spec, ir, module);
    if (!flow_component_validate_contract(ir, module, err_msg, sizeof(err_msg))) {
        fprintf(stderr, "flowc: module '%s' contract validation failed: %s\n", mod_name, err_msg[0] ? err_msg : "invalid domain contract"); return 0; } return 1; }
int flowc_main(int argc, char **argv);
int flowc_main(int argc, char **argv) { const char *input_path = NULL, *output_path = NULL, *profile_path = NULL, *profile_out = NULL;
    const char *component_override = NULL, *target_c_header = NULL, *target_rust = NULL;
    const char *target_python = NULL, *target_lean = NULL, *target_mlir = NULL, *target_llvm_ir = NULL, *apply_fvec_path = NULL;
    const char *target_js = NULL, *target_cuda = NULL, *target_matlab = NULL, *target_julia = NULL, *target_ui = NULL;
    char auto_output_buf[512] = {0}, auto_fvec_path[512] = {0}; FlowSpec spec; SemanticIR ir; const Component *component; VerificationReport verification;
    SearchResult search = {0}; ProfileSeed profile = {0}; int use_search = 0, use_benchmark = 0, use_reload_adapter = 0;
    size_t iterations = 250, workload_bytes = 0; uint32_t seed = 0xC0F0123u;
    if (!flow_registry_init()) { fprintf(stderr, "flowc: failed to initialize plugin registry\n"); return EXIT_FAILURE; }
    if (argc < 2) { flowc_print_usage(stderr); return EXIT_FAILURE; }
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) { flowc_print_usage(stdout); return EXIT_SUCCESS; }
    if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version") || !strcmp(argv[1], "version")) {
        printf("FLOW Modern Declarative Compiler (flowc) v2.5.0 | Lean-RT, Cubical HoTT & QSBR\n"); return EXIT_SUCCESS; } input_path = argv[1];
    for (int arg = 2; arg < argc; ++arg) { if (!strcmp(argv[arg], "-o") && arg + 1 < argc) output_path = argv[++arg]; else if (!strcmp(argv[arg], "--search")) use_search = 1;
        else if (!strcmp(argv[arg], "--benchmark")) { use_benchmark = 1; use_search = 1; } else if (!strcmp(argv[arg], "--reload-adapter")) use_reload_adapter = 1;
        else if (!strcmp(argv[arg], "--profile") && arg + 1 < argc) profile_path = argv[++arg];
        else if ((!strcmp(argv[arg], "--profile-out") || !strcmp(argv[arg], "--lock") || !strcmp(argv[arg], "--flowplan")) && arg + 1 < argc) { profile_out = argv[++arg]; use_search = 1; }
        else if (!strcmp(argv[arg], "--component") && arg + 1 < argc) component_override = argv[++arg];
        else if ((!strcmp(argv[arg], "--apply-fvec") || !strcmp(argv[arg], "--fvec")) && arg + 1 < argc) apply_fvec_path = argv[++arg];
        else if (!strcmp(argv[arg], "--target-c-header") && arg + 1 < argc) target_c_header = argv[++arg];
        else if (!strcmp(argv[arg], "--target-rust") && arg + 1 < argc) target_rust = argv[++arg];
        else if (!strcmp(argv[arg], "--target-python") && arg + 1 < argc) target_python = argv[++arg];
        else if (!strcmp(argv[arg], "--target-lean") && arg + 1 < argc) target_lean = argv[++arg];
        else if (!strcmp(argv[arg], "--target-js") && arg + 1 < argc) target_js = argv[++arg];
        else if (!strcmp(argv[arg], "--target-cuda") && arg + 1 < argc) target_cuda = argv[++arg];
        else if (!strcmp(argv[arg], "--target-matlab") && arg + 1 < argc) target_matlab = argv[++arg];
        else if (!strcmp(argv[arg], "--target-julia") && arg + 1 < argc) target_julia = argv[++arg];
        else if (!strcmp(argv[arg], "--target-ui") && arg + 1 < argc) target_ui = argv[++arg];
        else if (!strcmp(argv[arg], "--target-mlir") && arg + 1 < argc) target_mlir = argv[++arg];
        else if (!strcmp(argv[arg], "--target-llvm-ir") && arg + 1 < argc) target_llvm_ir = argv[++arg];
        else if (!strcmp(argv[arg], "--workload-bytes") && arg + 1 < argc) workload_bytes = (size_t)strtoull(argv[++arg], NULL, 10);
        else if (!strcmp(argv[arg], "--iterations") && arg + 1 < argc) iterations = (size_t)strtoul(argv[++arg], NULL, 10);
        else if (!strcmp(argv[arg], "--seed") && arg + 1 < argc) seed = (uint32_t)strtoul(argv[++arg], NULL, 10);
        else if (!strcmp(argv[arg], "-h") || !strcmp(argv[arg], "--help")) { flowc_print_usage(stdout); return EXIT_SUCCESS; }
        else { fprintf(stderr, "flowc: unknown option: %s\n", argv[arg]); return EXIT_FAILURE; } } const char *dot = strrchr(input_path, '.');
    size_t base_len = (dot && (!strcmp(dot, ".flow") || !strcmp(dot, ".lean"))) ? (size_t)(dot - input_path) : 0; if (!output_path) {
        if (base_len > 0 && base_len + 3 < sizeof(auto_output_buf)) { memcpy(auto_output_buf, input_path, base_len); strcpy(auto_output_buf + base_len, ".c"); }
        else snprintf(auto_output_buf, sizeof(auto_output_buf), "%s.c", input_path); output_path = auto_output_buf; }
    if (!apply_fvec_path && base_len > 0 && base_len + 6 < sizeof(auto_fvec_path)) { memcpy(auto_fvec_path, input_path, base_len); strcpy(auto_fvec_path + base_len, ".fvec");
        FILE *ft = fopen(auto_fvec_path, "rb"); if (ft) { fclose(ft); apply_fvec_path = auto_fvec_path; } } FILE *input = fopen(input_path, "r");
    if (!input) { fprintf(stderr, "flowc: cannot open %s: %s\n", input_path, strerror(errno)); return EXIT_FAILURE; }
    if (!parse_spec(input, &spec)) { fclose(input); return EXIT_FAILURE; } fclose(input); lower_to_ir(&spec, &ir); if (target_lean) ir.fact_lean_certified = 1;
    ir.workload_bytes = workload_bytes; if (ir.imported_module_count > 0) { for (size_t m = 0; m < ir.imported_module_count; ++m)
            if (!validate_and_lower_module(ir.imported_modules[m], &spec, &ir)) { flow_ir_cleanup(&ir); return EXIT_FAILURE; }
    } else if (!validate_and_lower_module(spec.plugin_name[0] ? spec.plugin_name : "builtin", &spec, &ir)) { flow_ir_cleanup(&ir); return EXIT_FAILURE; } if (profile_path) {
        FILE *pf = fopen(profile_path, "r"); if (pf) { FlowPlanArtifact artifact;
            if (flow_plan_artifact_load(pf, &artifact) && (!artifact.flow_name[0] || !strcmp(artifact.flow_name, ir.flow_name)) && artifact.component_id[0]) {
                char val_err[256] = {0}; if (!flow_artifact_validate(&artifact, &ir, NULL, val_err, sizeof(val_err))) {
                    fprintf(stderr, "flowc: lockfile rejected: %s\n", val_err); fclose(pf); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
                for (size_t i = 0; i < component_count(); ++i) { const Component *cand = component_at(i);
                    if (cand && !strcmp(cand->id, artifact.component_id)) { flow_artifact_to_profile_seed(&artifact, &profile); profile.component = i; break; } } }
            if (!profile.available) { rewind(pf); char line[256]; while (fgets(line, sizeof(line), pf)) {
                    char fn[FLOW_NAME], cn[FLOW_NAME]; unsigned long cap_v, thr_v, shd_v, buf_v, init_v, grw_v, bat_v, arn_v; unsigned long long bm_v, wl_v;
                    int f = sscanf(line, "%63[^,],%63[^,],%lu,%lu,%lu,%llu,%llu,%lu,%lu,%lu,%lu,%lu", fn, cn, &cap_v, &thr_v, &shd_v, &bm_v, &wl_v, &buf_v, &init_v, &grw_v, &bat_v, &arn_v);
                    if ((f == 6 || f == 12) && !strcmp(fn, ir.flow_name)) { for (size_t i = 0; i < component_count(); ++i) { const Component *cand = component_at(i);
                            if (cand && !strcmp(cand->id, cn)) {
                                profile = (ProfileSeed){.available = 1, .component = i, .capacity = cap_v, .threads = thr_v, .shards = shd_v, .benchmark_ns = bm_v};
                                if (f == 12) { profile.workload_bytes = (size_t)wl_v; profile.tuning = (FlowTuning){buf_v, init_v, (unsigned)grw_v, bat_v, arn_v}; } break; } }
                    } } } fclose(pf); } if (profile.available)
            printf("  profile: loaded component=%s capacity=%zu threads=%zu shards=%zu benchmark_ns=%llu workload_bytes=%zu\n",
                   component_at(profile.component)->id, profile.capacity, profile.threads, profile.shards, (unsigned long long)profile.benchmark_ns, profile.workload_bytes);
        else fprintf(stderr, "flowc: profile not found for flow %s: %s\n", ir.flow_name, profile_path); } component = select_component(&ir);
    if (!component && !component_override) { fprintf(stderr, "flowc: no registered plugin candidate satisfies the spec\n"); return EXIT_FAILURE; } if (component_override) {
        component = NULL;
        for (size_t i = 0; i < component_count(); ++i) { const Component *cand = component_at(i); if (cand && !strcmp(cand->id, component_override)) { component = cand; break; } }
        if (!component) { fprintf(stderr, "flowc: unknown component: %s\n", component_override); return EXIT_FAILURE; }
        if (use_search) { fprintf(stderr, "flowc: --component cannot be combined with --search/--benchmark\n"); return EXIT_FAILURE; } } if (apply_fvec_path) {
        FlowVecHeader fvec_hdr; FlowVecPayload fvec_payload;
        if (!flow_fvec_read_file(apply_fvec_path, &fvec_hdr, &fvec_payload)) { fprintf(stderr, "flowc: failed to load .fvec file '%s'\n", apply_fvec_path); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
        char affinity_diag[512] = {0}; FlowEnvironmentState host_env = {.hardware_arch = FLOW_ARCH_INTEL_AVX2};
        if (!flow_fvec_verify_hardware_affinity(&fvec_hdr, &host_env, affinity_diag, sizeof(affinity_diag))) { fprintf(stderr, "flowc: .fvec hardware affinity rejected: %s\n", affinity_diag); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
        FlowBitSpace space;
        if (!flow_bitspace_init_for_ir(&ir, &space)) { fprintf(stderr, "flowc: failed to initialize BitSpace for spec '%s'\n", input_path); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
        FlowPlan plan; space.decode(&space, fvec_payload.pure_genome, &plan); space.evaluate(&space, &plan, &plan.eval);
        component = plan.component; flow_plan_to_search_result(&plan, &ir, seed, &search); use_search = 1;
        printf("  fvec: applied '%s' [%s]\n        -> Genome: 0x%016llx | Component: %s | Energy: %.2f | SMT: %s\n",
               fvec_hdr.name, apply_fvec_path, (unsigned long long)plan.genome, component ? component->id : "(auto)", plan.eval.energy, fvec_hdr.smt_signature);
    } else if (use_search) { search = search_best(&ir, iterations, seed, use_benchmark, profile.available ? &profile : NULL); component = search.component;
        if (!component) { fprintf(stderr, "flowc: search could not find a viable implementation plan satisfying all hard gates\n"); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
    }
    if (use_reload_adapter && !flow_component_supports_reload(component)) { fprintf(stderr, "flowc: --reload-adapter is not supported by component '%s'\n", component->id); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
    if (!verify_candidate(&ir, component, use_search ? &search : NULL, &verification)) { fprintf(stderr, "flowc: verifier: %s\n", verification.message); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
    FlowSMTProofAttestation proof = {0}; FlowPlanMetrics smt_m = search.metrics;
    if (!smt_m.capacity) smt_m.capacity = verification.capacity > 0 ? verification.capacity : (size_t)(ir.input_max_count > 0 ? ir.input_max_count : 4096);
    if (!smt_m.shards) smt_m.shards = (use_search && search.shards > 0) ? (size_t)search.shards : 1;
    if (!smt_m.threads) smt_m.threads = (use_search && search.threads > 0) ? (size_t)search.threads : 1;
    if (!smt_m.memory_bytes && verification.estimated_bytes > 0) smt_m.memory_bytes = verification.estimated_bytes;
    int smt_ok = flow_smt_verify(&ir, component, use_search ? &search.assignment : NULL, &smt_m, &proof);
    FlowSMTResult status = (!smt_ok || proof.buffer_bounds_safety == FLOW_SMT_VIOLATION_SAT || proof.memory_quota_bound == FLOW_SMT_VIOLATION_SAT || proof.shard_non_aliasing == FLOW_SMT_VIOLATION_SAT || proof.determinism_invariant == FLOW_SMT_VIOLATION_SAT) ? FLOW_SMT_VIOLATION_SAT : ((proof.buffer_bounds_safety != FLOW_SMT_PROVEN_UNSAT || proof.memory_quota_bound != FLOW_SMT_PROVEN_UNSAT || proof.shard_non_aliasing != FLOW_SMT_PROVEN_UNSAT || proof.determinism_invariant != FLOW_SMT_PROVEN_UNSAT) ? FLOW_SMT_UNKNOWN : FLOW_SMT_PROVEN_UNSAT);
    if (status != FLOW_SMT_PROVEN_UNSAT) {
        fprintf(stderr, "flowc: SMT proof failed: status=%s (%s)\n", flow_smt_result_name(status), proof.proof_summary);
        flow_ir_cleanup(&ir); return EXIT_FAILURE;
    }
    FILE *output = fopen(output_path, "w");
    if (!output) { fprintf(stderr, "flowc: cannot write %s: %s\n", output_path, strerror(errno)); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
    int generated_ok = emit_c(output, &ir, component, use_search ? &search : NULL, &verification, use_reload_adapter);
    if (fclose(output) != 0 || !generated_ok) { fprintf(stderr, "flowc: failed to generate %s\n", output_path); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
    if (profile_out && use_search) { FILE *pf = fopen(profile_out, "w");
        if (!pf) { fprintf(stderr, "flowc: cannot write profile %s: %s\n", profile_out, strerror(errno)); flow_ir_cleanup(&ir); return EXIT_FAILURE; }
        if (strstr(profile_out, ".fvec")) { fclose(pf);
            FlowBitSpace fvec_space; uint64_t hard_mask = UINT64_MAX, soft_bias = 0;
            if (flow_bitspace_init_for_ir(&ir, &fvec_space) && fvec_space.global_canvas.hard_composite_mask) {
                hard_mask = fvec_space.global_canvas.hard_composite_mask;
                soft_bias = fvec_space.global_canvas.soft_composite_bias;
            }
            FlowVecHeader hdr = {
                .magic = "FVEC_V1", .origin_hardware = "x86_avx2, L1=64K, Cores=64", .trigger_intent = "UNIVERSAL_LOCK", .category = "UNIVERSAL_LOCK",
                .energy_score = search.energy, .created_at_unix = (uint64_t)time(NULL),
                .vector_dim = FLOW_VAULT_DIM, .payload_size = sizeof(FlowVecPayload) };
            snprintf(hdr.smt_signature, sizeof(hdr.smt_signature), "%s:%s:%s:%s",
                     proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT ? "BUFFER_UNSAT" : "BUFFER_SAT",
                     proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT ? "MEM_UNSAT" : "MEM_SAT",
                     proof.shard_non_aliasing == FLOW_SMT_PROVEN_UNSAT ? "SHARD_UNSAT" : "SHARD_SAT",
                     proof.determinism_invariant == FLOW_SMT_PROVEN_UNSAT ? "DET_UNSAT" : "DET_SAT");
            snprintf(hdr.id, sizeof(hdr.id), "lock_%s", ir.flow_name);
            snprintf(hdr.name, sizeof(hdr.name), "Universal Lockfile [%s]", ir.flow_name); snprintf(hdr.component_id, sizeof(hdr.component_id), "%s", component->id);
            FlowVecPayload payload = { .pure_genome = search.genome & hard_mask, .hard_composite_mask = hard_mask, .soft_composite_bias = soft_bias, .proof = proof };
            flow_fvec_embed_prompt(ir.flow_name, payload.features);
            payload.crc32 = flow_fvec_crc32(&payload, sizeof(payload) - sizeof(uint32_t)); flow_fvec_write_file(profile_out, &hdr, &payload);
        } else if (strstr(profile_out, ".flowplan") || strstr(profile_out, ".lock")) { FlowBitSpace space; flow_bitspace_init_for_ir(&ir, &space);
            FlowPlan plan; space.decode(&space, search.genome, &plan); FlowPlanArtifact artifact; flow_plan_to_artifact(&plan, &ir, seed, &artifact);
            flow_plan_artifact_save(pf, &artifact); fclose(pf); } else {
            fprintf(pf, "flow,component,capacity,threads,shards,benchmark_ns,workload_bytes,buffer_bytes,initial_capacity,growth_percent,batch_size,arena_bytes\n%s,%s,%zu,%zu,%zu,%llu,%zu,%zu,%zu,%u,%zu,%zu\n",
                    ir.flow_name, component->id, (size_t)search.capacity, (size_t)search.threads, (size_t)search.shards, (unsigned long long)search.benchmark_ns, ir.workload_bytes, search.tuning.buffer_bytes, search.tuning.initial_capacity, search.tuning.growth_percent, search.tuning.batch_size, search.tuning.arena_bytes);
            fclose(pf); } printf("  profile: wrote %s\n", profile_out); } printf("flowc: %s -> %s\n", input_path, output_path);
    if (ir.project_name[0]) printf("  project: %s\n", ir.project_name);
    printf("  IR: output=%s shared=%d read_heavy=%d bounded=%d parallelizable=%d ordered=%d unordered=%d deterministic=%d range=%d size_preserved=%d mutability_read_only=%d ensures=%d declared_constraints=%zu resource=%s capability=%s domain=%s contract=%s fallback=%s graph_nodes=%zu facts=%zu constraints=%zu holes=%zu\n  selected: %s (%s)\n  candidates:",
           ir.output_name, ir.state_shared, ir.state_read_heavy, ir.state_bounded, ir.flow_parallelizable, ir.fact_ordered, ir.fact_unordered, ir.fact_deterministic, ir.fact_range_proven, ir.fact_size_preserved, ir.fact_mutability_read_only, ir.ensure_count, ir.declared_constraint_count, ir.resource_name, ir.capability_name, ir.domain_name, ir.contract_name, ir.fallback_policy, ir.flow_node_count, ir.fact_count, ir.constraint_count, ir.hole_count, component->id, component->kind);
    for (size_t ci = 0; ci < compatible_component_count(&ir); ++ci) { const Component *cand = compatible_component_at(&ir, ci); if (cand) printf(" %s", cand->id); }
    printf("\n  verifier: status=%s capacity=%zu estimated_bytes=%zu (%s)\n", verification_status_name(verification.status), verification.capacity, verification.estimated_bytes, verification.message);
    size_t cap = ir.input_max_count > 0 ? ir.input_max_count : (verification.capacity > 0 ? verification.capacity : 64);
    printf("  symplectic: omega=%.3f mu=%.6f q_sat=%.1f (polyhedral shaped)\n", fmax(0.1, sqrt((double)cap) / 16.0), 1.0 / (double)cap, 1.25 * (double)cap);
    if (ir.jet.enabled) printf("  jet phase-space: freq=%.1f Hz symplectic=%s max_drift=%.4f%% zmp_margin=%.3f dt=%.6fs dim=%zu\n", ir.jet.frequency_hz, ir.jet.symplectic ? "YES" : "NO", ir.jet.max_drift * 100.0, ir.jet.zmp_margin, ir.jet.dt, ir.jet.dim);
    if (ir.thermal.enabled) printf("  thermal envelope: max_temp=%.1f C target_temp=%.1f C power_limit=%.1f W (R=%.2f C/W, C=%.2f J/C)\n", ir.thermal.max_temp_c, ir.thermal.target_temp_c, ir.thermal.power_limit_w, ir.thermal.r_thermal, ir.thermal.c_thermal);
    if (use_search) { uint64_t schema_hash = flow_bitspace_compute_schema_hash(&ir, component, &search.dimension_set);
        printf("  C search: mode=%s iterations=%zu seed=%u schema_hash=%llu genome=%llu energy=%.6f benchmark_ns=%llu capacity=%.0f threads=%.0f shards=%.0f tuning_buffer=%zu tuning_initial=%zu tuning_growth=%u tuning_batch=%zu tuning_arena=%zu\n",
               search.measured ? "benchmark" : "model", search.iterations, search.seed, (unsigned long long)schema_hash, (unsigned long long)search.genome, search.energy, (unsigned long long)search.benchmark_ns, search.capacity, search.threads, search.shards, search.tuning.buffer_bytes, search.tuning.initial_capacity, search.tuning.growth_percent, search.tuning.batch_size, search.tuning.arena_bytes);
    } if (target_c_header || target_rust || target_python || target_lean || target_js || target_cuda || target_matlab || target_julia || target_ui || target_mlir || target_llvm_ir) {
        FlowComponentABI abi; flow_abi_build_for_component(&ir, component, &abi);
        #define EMIT_TARGET(path, tag, call) do { if (path) { FILE *fp = fopen(path, "w"); if (fp) { call; fclose(fp); printf("  " tag ": wrote %s\n", path); } } } while (0)
        EMIT_TARGET(target_c_header, "target-c-header", flow_abi_emit_c_header(fp, &abi)); EMIT_TARGET(target_rust, "target-rust", flow_abi_emit_rust_adapter(fp, &abi));
        EMIT_TARGET(target_python, "target-python", flow_abi_emit_python_adapter(fp, &abi));
        EMIT_TARGET(target_js, "target-js", flow_abi_emit_polyglot_adapter(fp, &abi, "js"));
        EMIT_TARGET(target_cuda, "target-cuda", flow_abi_emit_polyglot_adapter(fp, &abi, "cuda"));
        EMIT_TARGET(target_matlab, "target-matlab", flow_abi_emit_polyglot_adapter(fp, &abi, "matlab"));
        EMIT_TARGET(target_julia, "target-julia", flow_abi_emit_polyglot_adapter(fp, &abi, "julia"));
        EMIT_TARGET(target_ui, "target-ui", flow_abi_emit_ui_adapter(fp, &ir, &abi));
        EMIT_TARGET(target_lean, "target-lean", flow_abi_emit_lean_adapter(fp, &ir, &abi, search.genome, use_search && search.capacity > 0 ? (size_t)search.capacity : verification.capacity, use_search && search.shards > 0 ? (size_t)search.shards : 1));
        if (target_lean) {
            char lean_err[256] = {0};
            if (!flow_abi_verify_lean_certificate(target_lean, lean_err, sizeof(lean_err))) {
                fprintf(stderr, "flowc: lean certificate verification failed: %s\n", lean_err);
                flow_ir_cleanup(&ir); return EXIT_FAILURE;
            }
        }
        EMIT_TARGET(target_mlir, "target-mlir", flow_emit_mlir(fp, &ir, component, use_search ? &search : NULL, &verification));
        EMIT_TARGET(target_llvm_ir, "target-llvm-ir", flow_emit_llvm_ir(fp, &ir, component, use_search ? &search : NULL, &verification));
        #undef EMIT_TARGET
    }
    printf("  SMT proof: status=%s (%s)\n", flow_smt_result_name(status), proof.proof_summary); flow_ir_cleanup(&ir); return EXIT_SUCCESS; }
#ifndef FLOWC_NO_MAIN
int main(int argc, char **argv) { return flowc_main(argc, argv); }
#endif
