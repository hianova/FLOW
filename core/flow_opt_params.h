#ifndef FLOW_OPT_PARAMS_H
#define FLOW_OPT_PARAMS_H

/* ============================================================================
 * AUTO-SYNTHESIZED BY `flow self-opt` (SEARU x FLOW 2-Jet Meta-Pipeline)
 * Zero hand-tuned magic constants: regenerated via `make self-opt`.
 * ============================================================================ */

/* Stage 1: Synthesized 0-ULP Degree-3 Horner Seeds for flow.h */
#define FLOW_OPT_ISQRT_C0 UINT64_C(3036802773)
#define FLOW_OPT_ISQRT_C1 UINT64_C(1473045638)
#define FLOW_OPT_ISQRT_C2 UINT64_C(837956711)
#define FLOW_OPT_ISQRT_C3 UINT64_C(254306611)

#define FLOW_OPT_RECIP_C0 UINT64_C(4279784192)
#define FLOW_OPT_RECIP_C1 UINT64_C(4077431235)
#define FLOW_OPT_RECIP_C2 UINT64_C(2892846106)
#define FLOW_OPT_RECIP_C3 UINT64_C(950821238)

/* Stage 2: Self-Evolved Hyperparameters for searu.h */
#define SEARU_OPT_CHAOS_MIN_Q8      128U
#define SEARU_OPT_CHAOS_MAX_Q8      320U
#define SEARU_OPT_STAG_SHIFT        0U
#define SEARU_OPT_BARRIER_BOOST_Q8  16U
#define SEARU_OPT_RAND_WALK_MASK    31U
#define SEARU_OPT_COL_WALK_MASK     3U
#define SEARU_OPT_BIL_WALK_MASK     1U
#define SEARU_OPT_INL_WALK_MASK     1U

#define SEARU_OPT_SAT_INIT_TEMP_Q12 UINT64_C(12288)
#define SEARU_OPT_SAT_COOL_Q16      UINT64_C(65212)
#define SEARU_OPT_SAT_REHEAT_STAG   112U
#define SEARU_OPT_SAT_REHEAT_Q12    UINT64_C(3328)

#define SEARU_OPT_COL_INIT_TEMP_Q12 UINT64_C(192)
#define SEARU_OPT_COL_COOL_Q16      UINT64_C(65088)
#define SEARU_OPT_COL_REHEAT_STAG   16U
#define SEARU_OPT_COL_REHEAT_Q12    UINT64_C(352)

#define SEARU_OPT_BIL_INIT_TEMP_Q12 UINT64_C(4194304)
#define SEARU_OPT_BIL_COOL_Q16      UINT64_C(65460)
#define SEARU_OPT_BIL_REHEAT_STAG   48U
#define SEARU_OPT_BIL_REHEAT_Q12    UINT64_C(1049600)

#define SEARU_OPT_INL_INIT_TEMP_Q12 UINT64_C(2097152)
#define SEARU_OPT_INL_COOL_Q16      UINT64_C(63724)
#define SEARU_OPT_INL_REHEAT_STAG   64U
#define SEARU_OPT_INL_REHEAT_Q12    UINT64_C(131584)

#endif /* FLOW_OPT_PARAMS_H */
