#include "bitspace.h"
#include "registry.h"
#include "search.h"
#include "backend.h"
#include "verifier.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROJ_CHECK(cond) if (!(cond)) { fprintf(stderr, "project-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; }

int main(void) {
    if (!flow_registry_init()) return 1;

    /* 1. Parse project.flow with project declaration and imports */
    const char *project_source =
        "project browser_runtime\n"
        "input task_stream {\n"
        "    max_count 10000\n"
        "}\n"
        "flow browser_pipeline {\n"
        "    task_stream -> transform -> collect\n"
        "}\n"
        "import builtin\n"
        "require {\n"
        "    deterministic\n"
        "    memory < 64mb\n"
        "}\n"
        "prefer {\n"
        "    latency\n"
        "}\n";

    FILE *spec_fp = tmpfile();
    PROJ_CHECK(spec_fp != NULL);
    fputs(project_source, spec_fp);
    rewind(spec_fp);

    FlowSpec spec;
    PROJ_CHECK(parse_spec(spec_fp, &spec));
    fclose(spec_fp);

    PROJ_CHECK(strcmp(spec.project_name, "browser_runtime") == 0);
    PROJ_CHECK(strcmp(spec.flow_name, "browser_pipeline") == 0);
    PROJ_CHECK(spec.imported_module_count == 1);
    PROJ_CHECK(strcmp(spec.imported_modules[0], "builtin") == 0);

    /* 2. Lower to SemanticIR */
    SemanticIR ir;
    lower_to_ir(&spec, &ir);
    PROJ_CHECK(strcmp(ir.project_name, "browser_runtime") == 0);
    PROJ_CHECK(ir.imported_module_count == 1);
    PROJ_CHECK(ir.memory_limit_mb == 64);
    PROJ_CHECK(ir.fact_deterministic == 1);
    PROJ_CHECK(ir.prefer_latency == 1);

    /* 3. Compute deterministic contract hash */
    uint64_t contract_h = flow_compute_contract_hash(&ir);
    PROJ_CHECK(contract_h != 0);

    /* 4. Initialize Hierarchical FlowBitSpace and 1-Bit Search */
    FlowBitSpace space;
    PROJ_CHECK(flow_bitspace_init_for_ir(&ir, &space));
    PROJ_CHECK(space.candidate_count > 0);

    FlowBitSearchResult search_res;
    PROJ_CHECK(flow_bitspace_search(&space, 50, 42, 0, NULL, &search_res));
    PROJ_CHECK(search_res.best_plan.component != NULL);
    PROJ_CHECK(search_res.best_plan.eval.hard_gate_passed == 1);

    /* 5. Save flowplan.lock (Lockfile / Evidence Spine) */
    FlowPlanArtifact lock_saved;
    PROJ_CHECK(flow_plan_to_artifact(&search_res.best_plan, &ir, 42, &lock_saved));
    PROJ_CHECK(lock_saved.contract_hash == contract_h);
    PROJ_CHECK(lock_saved.plan_schema_hash != 0);

    FILE *lock_fp = tmpfile();
    PROJ_CHECK(lock_fp != NULL);
    PROJ_CHECK(flow_plan_artifact_save(lock_fp, &lock_saved));
    rewind(lock_fp);

    /* 6. Restore from flowplan.lock */
    FlowPlanArtifact lock_loaded;
    PROJ_CHECK(flow_plan_artifact_load(lock_fp, &lock_loaded));
    fclose(lock_fp);

    PROJ_CHECK(strcmp(lock_loaded.flow_name, "browser_pipeline") == 0);
    PROJ_CHECK(strcmp(lock_loaded.component_id, search_res.best_plan.component->id) == 0);
    PROJ_CHECK(lock_loaded.contract_hash == contract_h);
    PROJ_CHECK(lock_loaded.plan_schema_hash == search_res.best_plan.schema_hash);
    PROJ_CHECK(lock_loaded.genome == search_res.best_plan.genome);

    /* 7. Verify Candidate and Emit C */
    SearchResult legacy_search = search_best(&ir, 50, 42, 0, NULL);
    VerificationReport v_rep;
    PROJ_CHECK(verify_candidate(&ir, legacy_search.component, &legacy_search, &v_rep));
    PROJ_CHECK(v_rep.status == VERIFIER_PROVEN || v_rep.status == VERIFIER_RUNTIME_CHECK);

    FILE *c_out = tmpfile();
    PROJ_CHECK(c_out != NULL);
    PROJ_CHECK(emit_c(c_out, &ir, legacy_search.component, &legacy_search, &v_rep, 0));
    rewind(c_out);

    char line[256];
    int found_proj = 0;
    while (fgets(line, sizeof(line), c_out) != NULL) {
        if (strstr(line, "Project: browser_runtime") != NULL) {
            found_proj = 1;
            break;
        }
    }
    fclose(c_out);
    PROJ_CHECK(found_proj);

    flow_ir_cleanup(&ir);

    printf("PROJECT_TEST=passed project=browser_runtime lockfile=flowplan.lock schema_hash=%llu genome=0x%016llx\n",
           (unsigned long long)lock_saved.plan_schema_hash, (unsigned long long)lock_saved.genome);
    return 0;
}
