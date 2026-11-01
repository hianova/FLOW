#ifndef FLOW_H
#define FLOW_H
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FLOW_VERSION_MAJOR 3
#define FLOW_VERSION_MINOR 0
#define FLOW_VERSION_PATCH 0
#ifndef FLOW_VERSION_STRING
#define FLOW_VERSION_STRING "3.0.0"
#endif
#ifndef FLOW_GIT_BRANCH
#define FLOW_GIT_BRANCH "main"
#endif
#ifndef FLOW_GIT_COMMIT
#define FLOW_GIT_COMMIT "unknown"
#endif
#ifndef FLOW_GIT_DIRTY
#define FLOW_GIT_DIRTY "clean"
#endif
#define FLOW_GIT_PROVENANCE        FLOW_GIT_BRANCH "@" FLOW_GIT_COMMIT "-" FLOW_GIT_DIRTY

#define FLOW_LINE                  512
#define FLOW_NAME                  64
#define FLOW_NODE_MAX              64
#define FLOW_CONSTRAINT_MAX        64
#define FLOW_FIELD_MAX             64
#define FLOW_LEAN_FN_MAX           128
#define FLOW_LEAN_PARAM_MAX        64
#define FLOW_LEAN_THEOREM_MAX      128
#define FLOW_LEAN_BODY_MAX         4096
#define FLOW_TUPLE_MAX             4
#define FLOW_LEAN_STRUCT_MAX       64
#define FLOW_LEAN_ENUM_MAX         64
#define FLOW_LEAN_ENUM_VARIANT_MAX 64
#define FLOW_LEAN_STRUCT_INV_MAX   64
#define FLOW_LEAN_THM_GUARD_MAX    64
#define FLOW_IMPORTED_MODULE_MAX   64

typedef struct {
    char name[FLOW_NAME];
    size_t size_bytes;
    uint32_t flags;
} FlowFieldSpec;
typedef struct {
    char name[FLOW_NAME];
    char c_type[16];
    char lean_type[16];
    size_t size_bytes;
} FlowLeanStructField;
typedef struct {
    char name[FLOW_NAME];
    FlowLeanStructField fields[FLOW_FIELD_MAX];
    size_t field_count;
    size_t byte_size;
    size_t align_bytes;
    size_t invariant_count;
    char invariants[FLOW_LEAN_STRUCT_INV_MAX][128];
} FlowLeanStruct;
typedef struct {
    char name[FLOW_NAME];
    char variants[FLOW_LEAN_ENUM_VARIANT_MAX][FLOW_NAME];
    size_t variant_count;
} FlowLeanEnum;

typedef enum {
    FLOW_EXPR_NAT = 0,
    FLOW_EXPR_UINT,
    FLOW_EXPR_INT,
    FLOW_EXPR_JET2
} FlowLeanExprMode;

/* ============================================================================
 * LLVM TableGen-Style X-Macro Single Source of Truth (SSOT) Tables
 * ============================================================================ */

/* X(lean_name, norm_lean_name, c_type, size_bytes, expr_mode) */
#define FLOW_LEAN_SCALAR_TYPES_X(X)                       \
    X("Nat", "Nat", "uint64_t", 8U, FLOW_EXPR_NAT)        \
    X("UInt64", "UInt64", "uint64_t", 8U, FLOW_EXPR_UINT) \
    X("USize", "USize", "uint64_t", 8U, FLOW_EXPR_UINT)   \
    X("Int", "Int", "int64_t", 8U, FLOW_EXPR_INT)         \
    X("Int64", "Int64", "int64_t", 8U, FLOW_EXPR_INT)     \
    X("UInt32", "UInt32", "uint32_t", 4U, FLOW_EXPR_UINT) \
    X("Int32", "Int32", "int32_t", 4U, FLOW_EXPR_INT)     \
    X("UInt16", "UInt16", "uint16_t", 2U, FLOW_EXPR_UINT) \
    X("Int16", "Int16", "int16_t", 2U, FLOW_EXPR_INT)     \
    X("UInt8", "UInt8", "uint8_t", 1U, FLOW_EXPR_UINT)    \
    X("Int8", "Int8", "int8_t", 1U, FLOW_EXPR_INT)        \
    X("Bool", "Bool", "bool", 1U, FLOW_EXPR_UINT)

/* X(lean_ident, c_ident) */
#define FLOW_LEAN_BUILTIN_OPS_X(X)       \
    X("Nat.min", "flow_nat_min")         \
    X("Nat.max", "flow_nat_max")         \
    X("Int.natAbs", "flow_int_abs")      \
    X("Int.min", "flow_int_min")         \
    X("Int.max", "flow_int_max")         \
    X("i_abs", "flow_int_abs")           \
    X("i_max", "flow_int_max")           \
    X("i_min", "flow_int_min")           \
    X("Int.ofNat", "flow_int_of_nat")    \
    X("Option.some", "some")             \
    X("Option.none", "none")             \
    X("FlowResult.interior", "interior") \
    X("FlowResult.boundary", "boundary")

/* X(enum_suffix, bit_index, internal_prefix, abi_prefix) */
#define FLOW_OPERATOR_VARIANTS_X(X)                        \
    X(PRIMARY, 0, "", "flowc_")                            \
    X(JET2, 1, "jet2_", "flowc_jet2_")                     \
    X(LEAN, 2, "lean_", "flowc_lean_")                     \
    X(CERTIFIED, 3, "lean_certified_", "flowc_certified_") \
    X(TRY, 4, "lean_try_", "flowc_try_")                   \
    X(SOA64, 5, "soa_", "flowc_soa_")

typedef enum {
#define FLOW_OP_VAR_ENUM(name, bit, c_pfx, abi_pfx) FLOW_FN_VAR_##name = (1u << (bit)),
    FLOW_OPERATOR_VARIANTS_X(FLOW_OP_VAR_ENUM)
#undef FLOW_OP_VAR_ENUM
} FlowOperatorVariantBit;

typedef struct {
    char name[FLOW_NAME];
    char return_type[FLOW_NAME];
    size_t param_count;
    char param_names[FLOW_LEAN_PARAM_MAX][32];
    char param_types[FLOW_LEAN_PARAM_MAX][32];
    int is_struct_transform;
    size_t field_assign_count;
    int is_tuple_return;
    size_t tuple_arity;
    char tuple_types[FLOW_TUPLE_MAX][32];
    char field_assign_names[FLOW_FIELD_MAX][24];
    char field_assign_exprs[FLOW_FIELD_MAX][64];
    char body[FLOW_LEAN_BODY_MAX];
    char raw_body[FLOW_LINE];
    char precond_expr[256];
    char postcond_expr[512];
    size_t thm_guard_count;
    char thm_names[FLOW_LEAN_THM_GUARD_MAX][FLOW_NAME];
    char thm_hyp_exprs[FLOW_LEAN_THM_GUARD_MAX][256];
    char thm_conc_exprs[FLOW_LEAN_THM_GUARD_MAX][256];
    int is_result_return;
    uint8_t param_is_result[FLOW_LEAN_PARAM_MAX];
    int is_private;
    uint8_t expr_mode;
    uint32_t variant_mask;
    uint64_t estimated_wcet_ns;
} FlowLeanFunction;
typedef struct {
    char name[FLOW_NAME];
    char anchored_fn[FLOW_NAME];
    char full_text[FLOW_LEAN_BODY_MAX];
    char raw_body[FLOW_LINE];
} FlowLeanTheorem;

#ifndef FLOW_RELATIONAL_DEFINED
#define FLOW_RELATIONAL_DEFINED

typedef enum {
    FLOW_REL_INVOLUTION = 1,
    FLOW_REL_IDEMPOTENT = 2,
    FLOW_REL_MONOTONIC = 3,
    FLOW_REL_EQUIVALENCE = 4
} FlowRelationalKind;

typedef struct {
    char name[FLOW_NAME];
    FlowRelationalKind kind;
    char fn_primary[FLOW_NAME];
    char fn_secondary[FLOW_NAME];
    char param_names[FLOW_LEAN_PARAM_MAX][FLOW_NAME];
    char param_types[FLOW_LEAN_PARAM_MAX][FLOW_NAME];
    size_t param_count;
    char hyp_expr[256];
    char conc_expr[256];
} FlowRelationalContract;

#endif /* FLOW_RELATIONAL_DEFINED */
typedef enum {
    FLOW_FACT_SHARED_STATE,
    FLOW_FACT_COLLECTION,
    FLOW_FACT_TRANSFORM,
    FLOW_FACT_PARALLELIZABLE,
    FLOW_FACT_ORDERED,
    FLOW_FACT_BOUNDED,
    FLOW_FACT_CAPABILITY,
    FLOW_FACT_CONSTRAINT,
    FLOW_FACT_HOLE,
    FLOW_FACT_RANGE,
    FLOW_FACT_SIZE,
    FLOW_FACT_MUTABILITY,
    FLOW_FACT_DETERMINISM,
    FLOW_FACT_LEAN_CERTIFIED
} FlowFactKind;
typedef struct {
    FlowFactKind kind;
    char detail[FLOW_NAME];
} FlowFact;
typedef struct {
    char name[FLOW_NAME];
    char operator[FLOW_NAME];
    char value[FLOW_NAME];
    char expression[FLOW_LINE];
} FlowConstraint;
#define FLOW_SPEC_CORE_FIELDS                                   \
    char input_name[FLOW_NAME];                                 \
    int input_max_count;                                        \
    char output_name[FLOW_NAME];                                \
    char output_type[FLOW_NAME];                                \
    char state_name[FLOW_NAME];                                 \
    int state_shared;                                           \
    int state_read_heavy;                                       \
    int state_bounded;                                          \
    char flow_name[FLOW_NAME];                                  \
    char flow_expression[FLOW_LINE];                            \
    int top_n;                                                  \
    int fact_deterministic;                                     \
    int fact_lean_certified;                                    \
    int memory_limit_mb;                                        \
    int prefer_latency;                                         \
    int ensure_count;                                           \
    FlowConstraint constraints[FLOW_CONSTRAINT_MAX];            \
    size_t declared_constraint_count;                           \
    char resource_name[FLOW_NAME];                              \
    char capability_name[FLOW_NAME];                            \
    char domain_name[FLOW_NAME];                                \
    char contract_name[FLOW_NAME];                              \
    char fallback_policy[FLOW_NAME];                            \
    char plugin_name[FLOW_NAME];                                \
    char project_name[FLOW_NAME];                               \
    char imported_modules[FLOW_IMPORTED_MODULE_MAX][FLOW_NAME]; \
    size_t imported_module_count;                               \
    size_t input_stride_bytes;                                  \
    size_t input_align_bytes;                                   \
    uint32_t state_schema_version;                              \
    FlowFieldSpec state_fields[FLOW_FIELD_MAX];                 \
    size_t state_field_count;                                   \
    uint64_t sla_latency_ns;                                    \
    FlowLeanFunction lean_functions[FLOW_LEAN_FN_MAX];          \
    size_t lean_function_count;                                 \
    FlowLeanTheorem lean_theorems[FLOW_LEAN_THEOREM_MAX];       \
    size_t lean_theorem_count;                                  \
    FlowLeanStruct lean_structs[FLOW_LEAN_STRUCT_MAX];          \
    size_t lean_struct_count;                                   \
    FlowLeanEnum lean_enums[FLOW_LEAN_ENUM_MAX];                \
    size_t lean_enum_count;                                     \
    int float_abi_ieee64;                                       \
    int omit_dummy_main;                                        \
    uint64_t qsbr_timeout_ns;                                   \
    size_t arena_limit_bytes;                                   \
    FlowRelationalContract relational_contracts[16];            \
    size_t relational_contract_count;
typedef struct {
    FLOW_SPEC_CORE_FIELDS
    size_t constraint_count;
} FlowSpec;
typedef struct SemanticIR {
    FLOW_SPEC_CORE_FIELDS
    int flow_parallelizable;
    int fact_ordered;
    int fact_range_proven;
    int fact_size_preserved;
    int fact_mutability_read_only;
    int fact_unordered;
    size_t workload_bytes;
    FlowFact facts[FLOW_NODE_MAX];
    size_t fact_count;
    size_t constraint_count;
    size_t hole_count;
    void *domain_ctx;
    void (*domain_ctx_free)(void *ctx);
} SemanticIR;

static inline void flow_ir_cleanup(SemanticIR *ir) {
    if (ir && ir->domain_ctx && ir->domain_ctx_free) {
        ir->domain_ctx_free(ir->domain_ctx);
        ir->domain_ctx = NULL;
    }
}
#define FLOW_WCET_BASE_CALL_NS    20ULL
#define FLOW_WCET_STRUCT_FIELD_NS 8ULL
#define FLOW_WCET_ALU_OP_NS       5ULL
#define FLOW_WCET_BRANCH_OP_NS    10ULL
#define FLOW_WCET_COMPLEX_OP_NS   25ULL

#define FLOW_FNV1A_64_OFFSET      0xCBF29CE484222325ULL
#define FLOW_FNV1A_64_PRIME       0x100000001B3ULL
#define FLOW_GOLDEN_RATIO_64      0x9e3779b97f4a7c15ULL
#define FLOW_DEFAULT_SEED         42U

static inline uint64_t flow_prng_next(uint64_t *state) {
    uint64_t x = (state && *state) ? *state : FLOW_GOLDEN_RATIO_64;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    if (state)
        *state = x;
    return x;
}

static inline uint32_t flow_crc32_ieee(const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)(-(int32_t)(crc & 1U)));
    }
    return ~crc;
}

int parse_spec(FILE *input, FlowSpec *spec);
void flow_parser_set_quiet(int quiet);
int flow_parser_is_quiet(void);
const char *flow_parser_last_error(void);
void lower_to_ir(const FlowSpec *spec, SemanticIR *ir);
int flow_verify_lean_theorems(const char *lean_source, char *err_msg, size_t err_size);
const char *flow_lean_scalar_to_c_type(const char *lean_type);
FlowLeanExprMode flow_c_type_expr_mode(const char *c_type, FlowLeanExprMode fallback);
void flow_lean_normalize_expr(const char *src, char *dst, size_t dst_size);
const FlowLeanStruct *flow_ir_find_struct(const SemanticIR *ir, const char *name);
const FlowLeanEnum *flow_ir_find_enum(const SemanticIR *ir, const char *name);
const char *flow_ir_type_to_c(const SemanticIR *ir, const char *lean_type);
void flow_ir_compute_fn_variants(SemanticIR *ir);
void flow_emit_ir_enums_and_structs(FILE *output, const SemanticIR *ir);

#ifndef FLOW_SMT_RESULT_DEFINED
#define FLOW_SMT_RESULT_DEFINED
typedef enum {
    FLOW_SMT_UNINITIALIZED = 0, /* Zero-initialized attestation has not been verified */
    FLOW_SMT_PROVEN_UNSAT = 1,  /* Negation is UNSAT -> Theorem holds universally */
    FLOW_SMT_VIOLATION_SAT = 2, /* Counterexample found -> Invariant violated */
    FLOW_SMT_UNKNOWN = 3        /* Unconstrained or unbounded */
} FlowSMTResult;
#endif

#ifndef FLOW_SMT_PROOF_ATTESTATION_DEFINED
#define FLOW_SMT_PROOF_ATTESTATION_DEFINED
typedef struct FlowSMTProofAttestation {
    FlowSMTResult buffer_bounds_safety;
    FlowSMTResult memory_quota_bound;
    FlowSMTResult shard_non_aliasing;
    FlowSMTResult determinism_invariant;
    char proof_summary[256];
} FlowSMTProofAttestation;
#endif

static inline const char *flow_smt_result_name(FlowSMTResult res) {
    return res == FLOW_SMT_PROVEN_UNSAT
               ? "unsat_proven_sound"
               : (res == FLOW_SMT_VIOLATION_SAT
                      ? "sat_counterexample_found"
                      : (res == FLOW_SMT_UNKNOWN ? "unknown_unbounded" : "unknown"));
}

#ifndef FLOW_SMT_RULE
typedef struct {
    const char *name;
    int64_t lhs;
    int64_t lower;
    int64_t upper;
    int theorem_id;
    const char *violation_msg;
} FlowBoxConstraint;

#define FLOW_BOX_THEOREM_DETERMINISM     1
#define FLOW_BOX_THEOREM_BUFFER_BOUNDS   2
#define FLOW_BOX_THEOREM_MEMORY_QUOTA    3
#define FLOW_BOX_THEOREM_SHARD_ISOLATION 4

#define FLOW_SMT_RULE(rule_name, lhs_val, lo_val, hi_val, thm_id, msg) \
    {(rule_name), (int64_t)(lhs_val), (int64_t)(lo_val), (int64_t)(hi_val), (thm_id), (msg)}

static inline FlowSMTResult flow_smt_verify_rules_impl(const char *domain,
                                                       FlowSMTProofAttestation *proof_out,
                                                       const FlowBoxConstraint *rules, size_t count,
                                                       const char *fmt, ...) {
    if (proof_out) {
        proof_out->buffer_bounds_safety = FLOW_SMT_PROVEN_UNSAT;
        proof_out->memory_quota_bound = FLOW_SMT_PROVEN_UNSAT;
        proof_out->shard_non_aliasing = FLOW_SMT_PROVEN_UNSAT;
        proof_out->determinism_invariant = FLOW_SMT_PROVEN_UNSAT;
    }
    for (size_t i = 0; i < count; ++i) {
        if (rules[i].lhs < rules[i].lower || rules[i].lhs > rules[i].upper) {
            if (proof_out) {
                if (rules[i].theorem_id == FLOW_BOX_THEOREM_BUFFER_BOUNDS)
                    proof_out->buffer_bounds_safety = FLOW_SMT_VIOLATION_SAT;
                else if (rules[i].theorem_id == FLOW_BOX_THEOREM_MEMORY_QUOTA)
                    proof_out->memory_quota_bound = FLOW_SMT_VIOLATION_SAT;
                else if (rules[i].theorem_id == FLOW_BOX_THEOREM_SHARD_ISOLATION)
                    proof_out->shard_non_aliasing = FLOW_SMT_VIOLATION_SAT;
                else
                    proof_out->determinism_invariant = FLOW_SMT_VIOLATION_SAT;
                snprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary),
                         "SMT Counterexample in %s [%s]: %s", domain ? domain : "domain",
                         rules[i].name ? rules[i].name : "rule",
                         rules[i].violation_msg ? rules[i].violation_msg : "bound violated");
            }
            return FLOW_SMT_VIOLATION_SAT;
        }
    }
    if (proof_out && fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(proof_out->proof_summary, sizeof(proof_out->proof_summary), fmt, ap);
        va_end(ap);
    }
    return FLOW_SMT_PROVEN_UNSAT;
}

#define FLOW_SMT_VERIFY_RULES(domain, proof_out, rules_arr, ...)   \
    flow_smt_verify_rules_impl((domain), (proof_out), (rules_arr), \
                               sizeof(rules_arr) / sizeof((rules_arr)[0]), __VA_ARGS__)
#endif

#include "flow_hw_probe.h"
#endif
