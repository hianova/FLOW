#include "flow.h"

#if defined(FLOW_RING0_GATE)

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ != 0)
#error "FLOW_RING0_GATE requires -ffreestanding (__STDC_HOSTED__ == 0)"
#endif

flow_cell flow_ring0_step_entry(flow_cell state, flow_cell bound);
void flow_ring0_arena_entry(flow_cell *arena, uint64_t idx, uint64_t mask, const uint8_t raw[64],
                            uint8_t out[64]);

flow_cell flow_ring0_step_entry(flow_cell state, flow_cell bound) {
    return flow_step(state, bound);
}

void flow_ring0_arena_entry(flow_cell *arena, uint64_t idx, uint64_t mask, const uint8_t raw[64],
                            uint8_t out[64]) {
    flow_cell c = flow_cell_from_bytes(raw);
    flow_arena_store_rel(arena, idx, mask, c);
    flow_cell loaded = flow_arena_load_acq(arena, idx, mask);
    flow_cell_to_bytes(loaded, out);
}

#else

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
            exit(1);                                                            \
        }                                                                       \
        ++g_assertions;                                                         \
    } while (0)

static uint64_t g_assertions = 0ULL;

static flow_cell load_capsule_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL, "open .flow capsule file");
    uint8_t raw[64];
    size_t n = fread(raw, 1U, 64U, fp);
    int extra = fgetc(fp);
    fclose(fp);
    CHECK(n == 64U && extra == EOF, ".flow capsule must be exactly 64 bytes");
    return flow_cell_from_bytes(raw);
}

static void load_arena_file(const char *path, flow_cell *out_cells, size_t count) {
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL, "open .flow arena file");
    for (size_t i = 0U; i < count; ++i) {
        uint8_t raw[64];
        size_t n = fread(raw, 1U, 64U, fp);
        CHECK(n == 64U, "read 64B arena slot");
        out_cells[i] = flow_cell_from_bytes(raw);
    }
    int extra = fgetc(fp);
    fclose(fp);
    CHECK(extra == EOF, ".flow arena file exact size check");
}

static flow_cell make_state(int64_t x_q16, uint32_t domain_tag) {
    flow_cell s = {
        0ULL, 0ULL,
        0ULL, (uint64_t)x_q16,
        0ULL, ((uint64_t)domain_tag << 32U) | (uint64_t)(uint32_t)FLOW_Q16_ONE,
        0ULL, 0ULL,
    };
    s[7] = flow_seal_cell(s);
    return s;
}

/* --- Multi-Threaded Lock-Free QSBR / SPSC Shared State --- */

static flow_cell g_shared_arena[4];
static uint64_t g_published_cursor = 0ULL;
static uint32_t g_writer_done = 0U;
static flow_cell g_capsule_bank[4];

static void *qsbr_writer_thread(void *arg) {
    (void)arg;
    for (uint64_t i = 1ULL; i <= 50000ULL; ++i) {
        flow_cell c = g_capsule_bank[i & 3ULL];
        uint32_t req = (uint32_t)(c[6] & UINT64_C(0xFFFFFFFF));
        c[6] = (i << 32U) | (uint64_t)req;
        c[7] = flow_seal_cell(c);
        flow_arena_store_rel(g_shared_arena, i, 3ULL, c);
        __atomic_store_n(&g_published_cursor, i, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&g_writer_done, 1U, __ATOMIC_RELEASE);
    return NULL;
}

int main(int argc, char **argv) {
    const char *cap_dir = (argc > 1) ? argv[1] : "build/capsules";
    char path_quad[256];
    char path_inst[256];
    char path_kan[256];
    char path_arena[256];
    snprintf(path_quad, sizeof(path_quad), "%s/quadratic_trap.flow", cap_dir);
    snprintf(path_inst, sizeof(path_inst), "%s/instanton_well.flow", cap_dir);
    snprintf(path_kan, sizeof(path_kan), "%s/kan_guarded.flow", cap_dir);
    snprintf(path_arena, sizeof(path_arena), "%s/arena4.flow", cap_dir);

    const uint32_t dom_flow = 0x464C4F57U;

    /* 1. Univalence, 0-ULP Lean <-> C Seal Equivalence & Firewall */
    flow_cell b_quad = load_capsule_file(path_quad);
    flow_cell b_inst = load_capsule_file(path_inst);
    flow_cell b_kan = load_capsule_file(path_kan);
    load_arena_file(path_arena, g_capsule_bank, 4U);

    CHECK(b_quad[7] != 0ULL && b_quad[7] == flow_seal_cell(b_quad),
          "quadratic_trap.flow 0-ULP Goldilocks F_p seal match");
    CHECK(b_inst[7] != 0ULL && b_inst[7] == flow_seal_cell(b_inst),
          "instanton_well.flow 0-ULP Goldilocks F_p seal match");
    CHECK(b_kan[7] != 0ULL && b_kan[7] == flow_seal_cell(b_kan),
          "kan_guarded.flow 0-ULP Goldilocks F_p seal match");
    for (size_t i = 0U; i < 4U; ++i) {
        CHECK(g_capsule_bank[i][7] == flow_seal_cell(g_capsule_bank[i]),
              "arena4.flow slot seal match");
    }

    flow_cell s_init = make_state(12345, dom_flow);
    flow_cell b_tampered = b_quad;
    b_tampered[0] ^= 1ULL;
    flow_cell s_after_tamper = flow_step(s_init, b_tampered);
    CHECK((s_after_tamper[6] & FLOW_SIEVE_SEAL_VALID) == 0ULL,
          "1-bit tamper drops FLOW_SIEVE_SEAL_VALID");
    CHECK(s_after_tamper[3] == s_init[3], "tampered boundary preserves state coordinate x");

    flow_cell s_foreign = make_state(12345, 0xDEADBEEFU);
    flow_cell s_after_foreign = flow_step(s_foreign, b_quad);
    CHECK((s_after_foreign[6] & FLOW_SIEVE_DOMAIN_MATCH) == 0ULL,
          "domain mismatch drops FLOW_SIEVE_DOMAIN_MATCH");
    CHECK(s_after_foreign[3] == s_foreign[3], "foreign domain preserves state coordinate x");

    /* 2. Exact Branchless isqrt_u64 & Hardware Overflow Sieve */
    CHECK(flow_isqrt_u64(0ULL) == 0ULL, "isqrt(0)");
    CHECK(flow_isqrt_u64(1ULL) == 1ULL, "isqrt(1)");
    CHECK(flow_isqrt_u64(8ULL) == 2ULL, "isqrt(8)");
    CHECK(flow_isqrt_u64(9ULL) == 3ULL, "isqrt(9)");
    CHECK(flow_isqrt_u64(UINT64_MAX) == UINT64_C(0xFFFFFFFF), "isqrt(UINT64_MAX)");

    uint64_t rng = FLOW_GOLDEN_64;
    for (uint32_t i = 0U; i < 4096U; ++i) {
        rng ^= rng << 13U;
        rng ^= rng >> 7U;
        rng ^= rng << 17U;
        uint64_t r = flow_isqrt_u64(rng);
        CHECK(r <= UINT64_C(0xFFFFFFFF), "isqrt range");
        CHECK(r * r <= rng, "isqrt lower bound r^2 <= x");
        uint64_t r1_hi = 0ULL;
        uint64_t r1_lo = flow_umul128(r + 1ULL, r + 1ULL, &r1_hi);
        CHECK(r1_hi > 0ULL || r1_lo > rng, "isqrt upper bound x < (r+1)^2");
    }

    flow_cell s_huge = make_state(INT64_MAX / 2, dom_flow);
    flow_cell s_ovf = flow_step(s_huge, b_quad);
    CHECK((s_ovf[6] & FLOW_SIEVE_NO_OVERFLOW) == 0ULL, "overflow captured into Sieve");
    CHECK(s_ovf[3] == s_huge[3], "overflowing step preserves prior valid coordinate");

    /* 3. Critical-Point Saddle Trap Root Jump: R(x) = 2 - x^2 at x = 0 (R'(0) = 0) */
    flow_cell s_trap = make_state(0, dom_flow);
    flow_cell s_jump1 = flow_step(s_trap, b_quad);
    int64_t x_jump1 = (int64_t)s_jump1[3];
    CHECK(x_jump1 >= 92680 && x_jump1 <= 92683,
          "single-step Citardauq escape from R'(0)=0 critical point to sqrt(2)");
    flow_cell s_jump2 = flow_step(s_jump1, b_quad);
    CHECK((s_jump2[6] & FLOW_SIEVE_CONVERGED) != 0ULL,
          "converged to 0-locus within Q48.16 epsilon on step 2");
    CHECK(flow_abs_i64((int64_t)s_jump2[0]) <= (uint64_t)FLOW_CONVERGE_EPS_Q16,
          "residual |R(x*)| <= 4 LSB");

    /* 4. Non-Convex Local Minimum Instanton Tunneling -> Global Basin Root */
    flow_cell s_well = make_state(0, dom_flow);
    flow_cell s_tun = flow_step(s_well, b_inst);
    CHECK((int64_t)s_tun[4] >= 92680,
          "Wick-rotated curvature tunneling escapes positive local minimum (Delta < 0, R'(0) = 0)");
    CHECK((int64_t)s_tun[3] >= 92680, "coordinate tunneled out of x = 0 dead-zone");

    /* Transition tunneled state into global basin slot 1: R(x) = -4.5 + 0.5*x^2 (root x = 3.0) */
    flow_cell b_basin = g_capsule_bank[1];
    flow_cell s_cur = s_tun;
    for (uint32_t step = 0U; step < 5U; ++step) {
        s_cur = flow_step(s_cur, b_basin);
    }
    int64_t x_root = (int64_t)s_cur[3];
    CHECK(flow_abs_i64(x_root - (3 * FLOW_Q16_ONE)) <= 2ULL,
          "converged to exact global basin root x = 3.0 after tunneling");
    CHECK((s_cur[6] & FLOW_SIEVE_CONVERGED) != 0ULL, "global basin FLOW_SIEVE_CONVERGED set");

    /* 5. HoTT Cubical Kan Homotopy Obstruction Sieve */
    flow_cell s_pos = make_state(1 * FLOW_Q16_ONE, dom_flow);
    flow_cell s_blocked = flow_step(s_pos, b_kan);
    CHECK((s_blocked[6] & FLOW_SIEVE_KAN_HOMOTOPIC) == 0ULL,
          "Kan filter blocks crossing into negative half-space");
    CHECK((int64_t)s_blocked[3] == 1 * FLOW_Q16_ONE,
          "obstructed Kan transition retains homotopic coordinate x = +1.0");

    /* 6. Autonomous 2-Jet Fiber Pushforward Mode (b1 == 0, b2 == 0) */
    flow_cell b_zero_target = {
        0ULL,
        0ULL,
        0ULL,
        0ULL,
        (uint64_t)(4 * FLOW_Q16_ONE),
        (uint64_t)dom_flow << 32U,
        (1ULL << 32U) | (uint64_t)FLOW_SIEVE_SAFETY_GATE,
        0ULL,
    };
    b_zero_target[7] = flow_seal_cell(b_zero_target);
    flow_cell s_fiber = {
        (uint64_t)(2 * FLOW_Q16_ONE),
        (uint64_t)(-3 * FLOW_Q16_ONE),
        (uint64_t)(2 * FLOW_Q16_ONE),
        0ULL,
        0ULL,
        ((uint64_t)dom_flow << 32U) | (uint64_t)(uint32_t)FLOW_Q16_ONE,
        0ULL,
        0ULL,
    };
    s_fiber[7] = flow_seal_cell(s_fiber);
    flow_cell s_fib_next = flow_step(s_fiber, b_zero_target);
    CHECK((int64_t)s_fib_next[3] == 1 * FLOW_Q16_ONE,
          "autonomous 2-Jet fiber jumps directly to nearer root delta = 1.0 of 2 - 3t + t^2");
    CHECK((s_fib_next[6] & FLOW_SIEVE_CONVERGED) != 0ULL,
          "autonomous 2-Jet Taylor pushforward reaches exact 0 residual");

    /* 7. Multi-Threaded Lock-Free SPSC & QSBR Arena Epoch Hot-Swap */
    for (uint64_t i = 0ULL; i < 4ULL; ++i) {
        flow_arena_store_rel(g_shared_arena, i, 3ULL, g_capsule_bank[i]);
    }
    pthread_t writer;
    CHECK(pthread_create(&writer, NULL, qsbr_writer_thread, NULL) == 0, "spawn QSBR writer");

    flow_cell s_rt = make_state(1 * FLOW_Q16_ONE, dom_flow);
    uint64_t reads = 0ULL;
    while (__atomic_load_n(&g_writer_done, __ATOMIC_ACQUIRE) == 0U || reads < 10000ULL) {
        uint64_t cur = __atomic_load_n(&g_published_cursor, __ATOMIC_ACQUIRE);
        flow_cell active_b = flow_arena_load_acq(g_shared_arena, cur, 3ULL);
        /* Masked Z/2^2Z index guarantees valid slot; verify seal integrity under hot-swap */
        uint64_t calc = flow_seal_cell(active_b);
        if (active_b[7] == calc) {
            s_rt = flow_step(s_rt, active_b);
            CHECK(s_rt[7] == flow_seal_cell(s_rt), "live state seal invariant under QSBR");
        }
        ++reads;
        if (reads > 200000ULL && __atomic_load_n(&g_writer_done, __ATOMIC_ACQUIRE) != 0U) {
            break;
        }
    }
    pthread_join(writer, NULL);
    CHECK(reads >= 10000ULL, "completed concurrent lock-free arena hot-swap evaluations");

    printf(
        "  [PASS] FLOW HoTT x 2-Jet Suite: %llu assertions verified (0-ULP, 0 structs, 0 heap)\n",
        (unsigned long long)g_assertions);
    return 0;
}

#endif /* FLOW_RING0_GATE */
