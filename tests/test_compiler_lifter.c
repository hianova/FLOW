#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hott_test_kit.h"
#include "flow.h"
#include "bitspace.h"
#include "backend.h"
#include "abi.h"
#include "llvm_lifter.h"

static int compile_lean_in_memory(const char *lean_src, SemanticIR *ir_out, char *c_buf,
                                  size_t c_cap, char *h_buf, size_t h_cap) {
    memset(ir_out, 0, sizeof(*ir_out));
    FILE *in = fmemopen((void *)lean_src, strlen(lean_src), "r");
    if (!in)
        return 0;
    FlowSpec spec;
    memset(&spec, 0, sizeof(spec));
    int ok = parse_spec(in, &spec);
    fclose(in);
    if (!ok)
        return 0;
    lower_to_ir(&spec, ir_out);
    if (c_buf && c_cap > 0) {
        FILE *c_out = fmemopen(c_buf, c_cap - 1, "w");
        if (!c_out)
            return 0;
        ok = emit_c(c_out, ir_out);
        fclose(c_out);
        if (!ok)
            return 0;
    }
    if (h_buf && h_cap > 0) {
        FILE *h_out = fmemopen(h_buf, h_cap - 1, "w");
        if (!h_out)
            return 0;
        ok = flow_abi_emit_c_header(h_out, ir_out);
        fclose(h_out);
        if (!ok)
            return 0;
    }
    return 1;
}

int main(void) {
    flow_parser_set_quiet(1);
    setenv("FLOW_LEAN_BIN", "/nonexistent/lean", 1);
    FLOW_TEST_SUITE_BEGIN("Pure Lean 4 <-> C17 / LLVM SSA Bidirectional Compiler & Lifter");

    /* =====================================================================
     * Stage 1: Lean 4 Euclidean Integer Semantics & 256-Bit Exact Numerics
     * ===================================================================== */
    FLOW_STAGE_BEGIN(1, "Lean 4 Euclidean Division/Modulo & U256 Montgomery Arithmetic");
    {
        /* Euclidean identity: b * (a / b) + (a % b) == a, with 0 <= a % b < |b| */
        const int64_t dividends[] = {-17, -16, -15, -1, 0, 1, 15, 16, 17, INT64_MAX, INT64_MIN + 1};
        const int64_t divisors[] = {-5, -4, -3, -1, 1, 3, 4, 5, 7};
        for (size_t i = 0; i < sizeof(dividends) / sizeof(dividends[0]); ++i) {
            for (size_t j = 0; j < sizeof(divisors) / sizeof(divisors[0]); ++j) {
                int64_t a = dividends[i], b = divisors[j];
                int64_t q = flow_int_div(a, b);
                int64_t r = flow_int_mod(a, b);
                FLOW_ASSERT_EQ((int64_t)((flow_i128_t)b * q + r), a);
                FLOW_ASSERT_TRUE(r >= 0 && r < (b < 0 ? -b : b));
            }
        }
        /* Division by zero and INT64_MIN / -1 totalized without UB */
        FLOW_ASSERT_EQ(flow_int_div(42, 0), 0);
        FLOW_ASSERT_EQ(flow_int_mod(42, 0), 42);
        FLOW_ASSERT_EQ(flow_int_div(INT64_MIN, -1), INT64_MIN);
        FLOW_ASSERT_EQ(flow_int_mod(INT64_MIN, -1), 0);

        /* Saturating Nat subtraction */
        FLOW_ASSERT_EQ(flow_nat_sub(10ULL, 3ULL), 7ULL);
        FLOW_ASSERT_EQ(flow_nat_sub(3ULL, 10ULL), 0ULL);

        /* U256 Montgomery modular exponentiation & Miller-Rabin primality */
        FlowU256 base = flow_u256_from_u64(3ULL);
        FlowU256 exp = flow_u256_from_u64(20ULL);
        FlowU256 mod = flow_u256_from_u64(1000000007ULL);
        FlowU256 res = flow_u256_mod_pow(base, exp, mod);
        FLOW_ASSERT_EQ(res.w[0], 486784380ULL); /* 3^20 = 3486784401 ≡ 486784380 (mod 1000000007) */
        const uint64_t mr_bases[] = {2ULL, 3ULL, 5ULL, 7ULL};
        FLOW_ASSERT_EQ(flow_u256_is_probably_prime(&mod, mr_bases, 4), 1);
    }

    /* =====================================================================
     * Stage 2: Forward Compiler (Pure Lean 4 -> Zero-GC C17 & Header)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(2, "Forward Synthesis: Pure Lean 4 -> Zero-GC C17, Guards & 2-Jet AD");
    {
        /* 2a. Synthesize plugins/embodied.lean and plugins/cryptanalysis.lean */
        const char *spec_files[] = {"plugins/embodied.lean", "plugins/cryptanalysis.lean"};
        for (size_t fi = 0; fi < 2; ++fi) {
            FILE *fp = fopen(spec_files[fi], "r");
            FLOW_ASSERT_TRUE(fp != NULL);
            FlowSpec spec;
            memset(&spec, 0, sizeof(spec));
            FLOW_ASSERT_EQ(parse_spec(fp, &spec), 1);
            fclose(fp);

            SemanticIR ir;
            memset(&ir, 0, sizeof(ir));
            lower_to_ir(&spec, &ir);
            FLOW_ASSERT_TRUE(ir.lean_function_count > 0);
            FLOW_ASSERT_TRUE(ir.lean_theorem_count > 0);

            char *c_buf = (char *)calloc(262144, 1);
            char *h_buf = (char *)calloc(131072, 1);
            FLOW_ASSERT_TRUE(c_buf != NULL && h_buf != NULL);

            FILE *c_mem = fmemopen(c_buf, 262143, "w");
            FLOW_ASSERT_EQ(emit_c(c_mem, &ir), 1);
            FLOW_ASSERT_EQ(flow_abi_emit_mex_dispatcher(c_mem, &ir), 1);
            fclose(c_mem);

            FILE *h_mem = fmemopen(h_buf, 131071, "w");
            FLOW_ASSERT_EQ(flow_abi_emit_c_header(h_mem, &ir), 1);
            fclose(h_mem);

            FLOW_ASSERT_TRUE(strstr(c_buf, "lean_certified_") != NULL);
            FLOW_ASSERT_TRUE(strstr(c_buf, "mexFunction") != NULL);
            FLOW_ASSERT_TRUE(strstr(h_buf, "FlowJetScalar2") != NULL);

            free(c_buf);
            free(h_buf);
            flow_ir_cleanup(&ir);
        }

        /* 2b. Verify theorem implication guard & relational contract synthesis */
        const char *rel_lean =
            "namespace Flow.RelationalTest\n"
            "structure TorqueCmd where\n"
            "  val : Int\n"
            "  h_bound : val >= -100 && val <= 100\n\n"
            "def clampTorque (x : Int) (limit : Int) : Int :=\n"
            "  if x > limit then limit else if x < 0 - limit then 0 - limit else x\n\n"
            "def mirrorAxis (x : Int) : Int := 0 - x\n\n"
            "theorem clamp_bounded (x : Int) (limit : Int) (h : limit > 0) :\n"
            "    clampTorque x limit <= limit := by\n"
            "  unfold clampTorque\n"
            "  split <;> (try split) <;> omega\n\n"
            "theorem mirror_involutive (x : Int) :\n"
            "    mirrorAxis (mirrorAxis x) = x := by\n"
            "  unfold mirrorAxis\n"
            "  omega\n"
            "end Flow.RelationalTest\n";

        SemanticIR rel_ir;
        char *rel_c = (char *)calloc(262144, 1);
        char *rel_h = (char *)calloc(131072, 1);
        FLOW_ASSERT_EQ(compile_lean_in_memory(rel_lean, &rel_ir, rel_c, 262144, rel_h, 131072), 1);
        FLOW_ASSERT_EQ(rel_ir.lean_struct_count, 1U);
        FLOW_ASSERT_EQ(rel_ir.lean_function_count, 2U);
        FLOW_ASSERT_TRUE(rel_ir.relational_contract_count >= 1U);
        FLOW_ASSERT_TRUE(strstr(rel_c, "lean_certify_TorqueCmd") != NULL);
        FLOW_ASSERT_TRUE(strstr(rel_c, "flow_rel_contract_mirror_involutive") != NULL);
        FLOW_ASSERT_TRUE(strstr(rel_c, "jet2_clampTorque") != NULL);
        FLOW_ASSERT_TRUE(strstr(rel_h, "flowc_jet2_clampTorque") != NULL);
        free(rel_c);
        free(rel_h);
        flow_ir_cleanup(&rel_ir);

        /* 2c. Soundness gate: reject sorry, axiom, recursion, and retired @blocks */
        const char *invalid_specs[] = {
            "def f (x : UInt64) : UInt64 := x\ntheorem bad (x : UInt64) : f x = 0 := by sorry\n",
            "axiom magic_false : False\ndef f (x : UInt64) : UInt64 := x\n",
            "def loop (n : UInt64) : UInt64 := if n == 0 then 0 else loop (n - 1)\n",
            "@embodied { frequency: 1000hz }\ndef f (x : UInt64) : UInt64 := x\n"};
        for (size_t k = 0; k < sizeof(invalid_specs) / sizeof(invalid_specs[0]); ++k) {
            SemanticIR bad_ir;
            FLOW_ASSERT_EQ(compile_lean_in_memory(invalid_specs[k], &bad_ir, NULL, 0, NULL, 0), 0);
        }
    }

    /* =====================================================================
     * Stage 3: Reverse LLVM SSA Boundary Lifter (LLVM IR -> Lean 4)
     * ===================================================================== */
    FLOW_STAGE_BEGIN(3, "Reverse Lifter: LLVM IR SSA -> Abstract Intervals & Lean 4 Proof");
    {
        const char *llvm_ir_src = "define i64 @saturating_sensor_gain(i64 %raw, i64 %gain) {\n"
                                  "entry:\n"
                                  "  %masked_raw = and i64 %raw, 1023\n"
                                  "  %masked_gain = and i64 %gain, 15\n"
                                  "  %prod = mul nuw nsw i64 %masked_raw, %masked_gain\n"
                                  "  %biased = add nuw nsw i64 %prod, 32\n"
                                  "  %is_high = icmp sgt i64 %biased, 8000\n"
                                  "  %clamped = select i1 %is_high, i64 8000, i64 %biased\n"
                                  "  ret i64 %clamped\n"
                                  "}\n";

        FlowLLVMLiftedFunction fns[4];
        size_t fn_count = 0;
        memset(fns, 0, sizeof(fns));
        FLOW_ASSERT_EQ(flow_llvm_lift_ir_text(llvm_ir_src, fns, 4, &fn_count), 0);
        FLOW_ASSERT_EQ(fn_count, 1U);
        FLOW_ASSERT_STR_EQ(fns[0].fn_name, "saturating_sensor_gain");
        FLOW_ASSERT_EQ(fns[0].param_count, 2U);
        FLOW_ASSERT_EQ(flow_llvm_infer_bounds(&fns[0], NULL), 0);
        FLOW_ASSERT_EQ(fns[0].overflow_free_proven, 1);
        /* Abstract interval must deduce [32, 8000] on the clamped select instruction */
        FLOW_ASSERT_TRUE(fns[0].inst_count >= 2U);
        const FlowLLVMInst *clamped_inst = &fns[0].insts[fns[0].inst_count - 2];
        FLOW_ASSERT_EQ(clamped_inst->inferred_min, 32);
        FLOW_ASSERT_EQ(clamped_inst->inferred_max, 8000);

        char lean_proof[32768] = {0};
        FILE *l_mem = fmemopen(lean_proof, sizeof(lean_proof) - 1, "w");
        FLOW_ASSERT_TRUE(l_mem != NULL);
        FLOW_ASSERT_EQ(flow_llvm_emit_lean_boundary_proof(l_mem, NULL, fns, fn_count), 0);
        fclose(l_mem);

        FLOW_ASSERT_TRUE(strstr(lean_proof, "namespace Flow.Lifted") != NULL);
        FLOW_ASSERT_TRUE(strstr(lean_proof, "def saturating_sensor_gain_llvm_lifted") != NULL);
        FLOW_ASSERT_TRUE(strstr(lean_proof, "theorem saturating_sensor_gain_in_bounds") != NULL);
    }

    FLOW_TEST_SUITE_END();
}
