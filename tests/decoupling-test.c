#include "flow.h"
#include "registry.h"
#include "plugin.h"
#include "flowy.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "decoupling-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("Starting FLOW Occam's Razor Decoupling & Modular DSO Verification Suite...\n");

    /* ========================================================================= */
    /* 1. Verify flowc Minimal Core CLI rejects domain CLI parameters            */
    /* ========================================================================= */
    {
        printf("  [1/4] Verifying flowc rejects bloated non-compiler CLI options...\n");
        int res_swarm = system("build/flowc examples/rank.flow -o /tmp/d_out.c --swarm 2>/dev/null");
        CHECK(res_swarm != 0);

        int res_mtd = system("build/flowc examples/rank.flow -o /tmp/d_out.c --mtd 2>/dev/null");
        CHECK(res_mtd != 0);

        int res_ask = system("build/flowc ask \"hello\" 2>/dev/null");
        CHECK(res_ask != 0);

        int res_audit = system("build/flowc audit 2>/dev/null");
        CHECK(res_audit != 0);

        int res_doc = system("build/flowc doc 2>/dev/null");
        CHECK(res_doc != 0);

        /* Positive core compilation */
        int res_core = system("build/flowc examples/rank.flow -o /tmp/d_core.c --search --iterations 50 --seed 42");
        CHECK(res_core == 0);
        printf("        -> flowc Core verified: Clean, fast compilation without domain CLI bloat.\n");
    }

    /* ========================================================================= */
    /* 2. Verify Dynamic DSO Plugin Loading (libflow_embodied.so, libflow_smt.so)*/
    /* ========================================================================= */
    {
        printf("  [2/4] Verifying dynamic DSO plugin registration & ABI entry...\n");
        flow_registry_init();

        char err[256] = {0};
        int dso_res = flow_registry_load_dso("build/libflow_embodied.so", err, sizeof(err));
        CHECK(dso_res == 1);

        const FlowPlugin *embodied_p = flow_registry_lookup("flow.embodied");
        CHECK(embodied_p != NULL);
        CHECK(strcmp(embodied_p->name, "flow.embodied") == 0);
        CHECK(embodied_p->environment_mask != NULL);

        int smt_res = flow_registry_load_dso("build/libflow_smt.so", err, sizeof(err));
        if (!smt_res) {
            fprintf(stderr, "flow_registry_load_dso failed for smt: %s\n", err);
        }
        CHECK(smt_res == 1);
        const FlowPlugin *smt_p = flow_registry_lookup("flow.smt");
        CHECK(smt_p != NULL);
        CHECK(strcmp(smt_p->name, "flow.smt") == 0);
        CHECK(smt_p->contract_mask != NULL);

        printf("        -> Dynamic DSOs verified: libflow_embodied.so & libflow_smt.so dlopened with ABI v1.\n");
    }

    /* ========================================================================= */
    /* 3. Verify .flow 'import flow.embodied' Auto-Resolves DSO in flowc          */
    /* ========================================================================= */
    {
        printf("  [3/4] Verifying .flow 'import flow.embodied' triggers on-demand DSO resolution...\n");
        FILE *fp = fopen("/tmp/test_import_embodied.flow", "w");
        CHECK(fp != NULL);
        fprintf(fp, "import flow.embodied\n\n");
        fprintf(fp, "input items {\n    max_count 1024\n}\n\n");
        fprintf(fp, "flow test_robotics {\n    items -> top(10)\n}\n\n");
        fprintf(fp, "require {\n    deterministic\n}\n");
        fclose(fp);

        int comp_res = system("build/flowc /tmp/test_import_embodied.flow -o /tmp/test_embodied_out.c --search --iterations 30 --seed 42");
        CHECK(comp_res == 0);
        printf("        -> On-Demand DSO Resolution verified: flowc automatically dlopened plugin on import.\n");
    }

    /* ========================================================================= */
    /* 4. Verify Standalone flowy Introspection CLI                             */
    /* ========================================================================= */
    {
        printf("  [4/4] Verifying standalone flowy auxiliary tool...\n");
        int res_ask = system("build/flowy ask \"what is the memory model of bitspace?\" > /tmp/flowy_ask.log");
        CHECK(res_ask == 0);

        int res_audit = system("build/flowy audit > /tmp/flowy_audit.log");
        CHECK(res_audit == 0);

        int res_why = system("build/flowy why > /tmp/flowy_why.log");
        CHECK(res_why == 0);

        int res_bottleneck = system("build/flowy bottleneck > /tmp/flowy_bottleneck.log");
        CHECK(res_bottleneck == 0);

        int res_doc = system("build/flowy doc bitspace > /tmp/flowy_doc.log");
        CHECK(res_doc == 0);

        int res_absorb = system("build/flowy absorb examples/compiler.flow > /tmp/flowy_absorb.log");
        CHECK(res_absorb == 0);

        int res_anneal = system("build/flowy anneal examples/compiler.flow examples/project.flow > /tmp/flowy_anneal.log");
        CHECK(res_anneal == 0);

        printf("        -> Standalone flowy verified: Complete introspection decoupled from compiler.\n");
    }

    printf("\nDECOUPLING_TEST=passed flowc_minimal=verified dso_plugins=verified import_resolution=verified standalone_flowy=verified\n");
    return 0;
}
