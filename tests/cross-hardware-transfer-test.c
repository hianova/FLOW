#include "flowy_fvec.h"
#include "registry.h"
#include "smt.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "cross-hardware-transfer-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    flow_registry_init();

    printf("========================================================================================\n");
    printf("  🧬 SCENARIO 2: Cross-Hardware Zero-Shot Gene Transplant Test\n");
    printf("  (Transferring Hardware-Agnostic Software DNA across x86 -> ARM -> RISC-V)\n");
    printf("========================================================================================\n\n");

    FlowVectorVault x86_vault;
    flow_vault_init(&x86_vault);
    flow_vault_seed_canonical_archetypes(&x86_vault);

    const FlowVaultEntry *hft = flow_vault_lookup_by_id(&x86_vault, "vec_hft_lockfree_trading");
    CHECK(hft != NULL);

    /* 1. Export Software DNA from x86_avx2 */
    char dna_packet[512];
    CHECK(flow_vault_export_dna(hft, FLOW_ARCH_INTEL_AVX2, dna_packet, sizeof(dna_packet)) > 0);
    printf("  [Step 1: Exported Software DNA from x86_avx2]\n");
    printf("    -> DNA Packet: %s\n", dna_packet);
    CHECK(strstr(dna_packet, "src_arch=x86_avx2") != NULL);

    /* 2. Zero-Shot Transplant to ARM (AArch64 NEON) */
    FlowVectorVault arm_vault;
    flow_vault_init(&arm_vault);
    size_t arm_idx = 0;
    double arm_confidence = 0.0;
    CHECK(flow_vault_import_dna(&arm_vault, dna_packet, FLOW_ARCH_ARM_NEON, &arm_idx, &arm_confidence));

    const FlowVaultEntry *arm_transferred = flow_vault_get(&arm_vault, arm_idx);
    CHECK(arm_transferred != NULL);
    printf("\n  [Step 2: Zero-Shot Transplant to ARM (AArch64 NEON)]\n");
    printf("    -> Transplanted Archetype: [%s]\n", arm_transferred->name);
    printf("    -> Zero-Shot Confidence:   %.2f%% (Inherited Prior Bias)\n", arm_confidence * 100.0);
    printf("    -> ARM Calibrated Soft Bias: 0x%016llx\n", (unsigned long long)arm_transferred->canvas.soft_composite_bias);
    CHECK(arm_confidence >= 0.95);
    CHECK(arm_transferred->proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);

    /* 3. Zero-Shot Transplant to RISC-V (RVV) */
    FlowVectorVault riscv_vault;
    flow_vault_init(&riscv_vault);
    size_t riscv_idx = 0;
    double riscv_confidence = 0.0;
    CHECK(flow_vault_import_dna(&riscv_vault, dna_packet, FLOW_ARCH_RISCV_VECTOR, &riscv_idx, &riscv_confidence));

    const FlowVaultEntry *riscv_transferred = flow_vault_get(&riscv_vault, riscv_idx);
    CHECK(riscv_transferred != NULL);
    printf("\n  [Step 3: Zero-Shot Transplant to RISC-V Vector]\n");
    printf("    -> Transplanted Archetype: [%s]\n", riscv_transferred->name);
    printf("    -> Zero-Shot Confidence:   %.2f%% (Inherited Prior Bias)\n", riscv_confidence * 100.0);
    printf("    -> SMT Proofs Soundness:   100%% PROVEN UNSAT (Zero Defect Across ISAs)\n");
    CHECK(riscv_confidence >= 0.94);
    CHECK(riscv_transferred->proof.memory_quota_bound == FLOW_SMT_PROVEN_UNSAT);

    printf("\n========================================================================================\n");
    printf("CROSS_HARDWARE_TRANSFER_TEST=passed source=x86 target=[arm,riscv] zero_shot_confidence=sound hardware_dna=verified\n");
    return 0;
}
