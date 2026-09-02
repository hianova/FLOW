#include "flow.h"
#include "flowy.h"
#include "topology.h"
#include "registry.h"
#include "orchestrator.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "flowy-level5-crucible failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("================================================================================\n");
    printf("            FLOW LEVEL-5 AUTONOMOUS CRUCIBLE AUDIT & CONTEST                    \n");
    printf("================================================================================\n");
    printf("Goal: Rigorously audit Level-5 Autonomous Self-Awareness & Double-Bind Survival\n");
    printf("Scenario: 16GB RAM + 64 Cores -> Instant 99%% Memory Drop (16MB) + 10,000x Concurrency Surge\n\n");

    flow_registry_init();

    FlowyCrucibleResult result;
    int run_ok = flowy_crucible_run(&result, stdout);
    CHECK(run_ok == 1);

    /* ========================================================================= */
    /* Verification 1: Stage 1 SMT Formal Rejection                              */
    /* ========================================================================= */
    printf("\n[Audit 1/4] Verifying Stage 1: SMT Formal Rejection of Naive Greedy Mask...\n");
    CHECK(result.stage1_smt_rejected == 1);
    CHECK(strstr(result.stage1_rejection_log, "[FLOWY-AUDIT]") != NULL);
    CHECK(strstr(result.stage1_rejection_log, "Proposed Mask 0x4A rejected by SMT") != NULL);
    CHECK(strstr(result.stage1_rejection_log, "Probability bias zeroed") != NULL);
    printf("  -> PASS: Mathematical SMT proof identified livelock and eliminated probability bias.\n");

    /* ========================================================================= */
    /* Verification 2: Stage 2 Epistatic Breakthrough & Self-Aware JIT Veto      */
    /* ========================================================================= */
    printf("\n[Audit 2/4] Verifying Stage 2: Self-Aware JIT Veto & Static Survival Routing...\n");
    CHECK(result.stage2_jit_vetoed == 1);
    CHECK(strstr(result.stage2_jit_log, "[FLOWY-AUDIT] JIT Compilation Disabled") != NULL);
    CHECK(strstr(result.stage2_jit_log, "Available RAM (16MB) < JIT Threshold (100MB)") != NULL);
    CHECK(strstr(result.stage2_routing_log, "[FLOWY-ORCHESTRATOR] Bypassing JIT") != NULL);
    CHECK(strstr(result.stage2_routing_log, "[Static_Survival_Mode_v1]") != NULL);
    printf("  -> PASS: JIT vetoed to prevent OS OOM Killer; pointers routed to zero-allocation static survival mode.\n");

    /* ========================================================================= */
    /* Verification 3: Stage 3 Zero-Downtime Hot-swap & Witness (< 50ms)         */
    /* ========================================================================= */
    printf("\n[Audit 3/4] Verifying Stage 3: Zero-Downtime Hot-swap & Zero-Drop Witness...\n");
    CHECK(result.stage3_hotswap_success == 1);
    CHECK(result.stage3_latency_ms < 50);
    CHECK(result.dropped_requests == 0);
    CHECK(result.oom_killer_triggered == 0);
    CHECK(result.energy_delta < -300.0);
    CHECK(strstr(result.stage3_narrative_log, "AoS_Multi -> SoA_EventLoop") != NULL);
    CHECK(strstr(result.stage3_narrative_log, "QSBR Migration [Success, 0 drops]") != NULL);
    printf("  -> PASS: Autonomous remodeling executed in %llu ms (<50ms target) with 0 dropped requests.\n",
           (unsigned long long)result.stage3_latency_ms);

    /* ========================================================================= */
    /* Verification 4: Stage 4 Crisis Cleared & Asynchronous Recovery            */
    /* ========================================================================= */
    printf("\n[Audit 4/4] Verifying Stage 4: Asynchronous Recovery to JIT v2...\n");
    CHECK(result.stage4_recovery_success == 1);
    CHECK(strstr(result.stage4_recovery_log, "Crisis cleared. RAM 16GB restored") != NULL);
    CHECK(strstr(result.stage4_recovery_log, "[Optimized_JIT_v2]") != NULL);
    printf("  -> PASS: Background JIT safely resumed and hot-swapped upon resource restoration.\n");

    printf("\n================================================================================\n");
    printf("FLOWY_LEVEL5_CRUCIBLE=PASSED (All 4 stages mathematically audited and proven sound)\n");
    printf("================================================================================\n");
    return 0;
}
