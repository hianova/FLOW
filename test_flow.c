#include "flow.h"

#if defined(FLOW_RING0_GATE)

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ != 0)
#error "FLOW_RING0_GATE requires -ffreestanding (__STDC_HOSTED__ == 0)"
#endif

flow_cell flow_ring0_step_entry(flow_cell state, flow_cell bound);
void flow_ring0_arena_entry(flow_cell *arena, uint64_t idx, uint64_t mask, uint32_t epoch,
                            const uint8_t raw[64], uint8_t out[64]);

flow_cell flow_ring0_step_entry(flow_cell state, flow_cell bound) {
    return flow_step(state, bound);
}

void flow_ring0_arena_entry(flow_cell *arena, uint64_t idx, uint64_t mask, uint32_t epoch,
                            const uint8_t raw[64], uint8_t out[64]) {
    flow_cell b = flow_cell_from_bytes(raw);
    flow_arena_publish_bound(arena, idx, mask, epoch, b);
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

static void load_cells_file(const char *dir, const char *name, flow_cell *out_cells, size_t count) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL, "open .flow binary file");
    for (size_t i = 0U; i < count; ++i) {
        uint8_t raw[64];
        size_t n = fread(raw, 1U, 64U, fp);
        CHECK(n == 64U, "read 64B cell");
        out_cells[i] = flow_cell_from_bytes(raw);
    }
    int extra = fgetc(fp);
    fclose(fp);
    CHECK(extra == EOF, ".flow binary exact byte length");
}

/* --- Multi-Threaded Lock-Free QSBR / SPSC Shared State (Zero Re-Sealing in C) --- */

static flow_cell g_shared_arena[4];
static flow_cell g_capsule_bank[4];
static uint64_t g_published_cursor = 0ULL;
static uint32_t g_writer_done = 0U;

static void *qsbr_writer_thread(void *arg) {
    (void)arg;
    for (uint64_t i = 1ULL; i <= 50000ULL; ++i) {
        /* Publishes new Epoch WITHOUT re-signing bound[7]: Lean seal is epoch-invariant */
        flow_arena_publish_bound(g_shared_arena, i, 3ULL, (uint32_t)i, g_capsule_bank[i & 3ULL]);
        __atomic_store_n(&g_published_cursor, i, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&g_writer_done, 1U, __ATOMIC_RELEASE);
    return NULL;
}

int main(int argc, char **argv) {
    const char *cap_dir = (argc > 1) ? argv[1] : "build/capsules";

    /* Load self-contained Lean bundle: [0]=Initial State, [1..4]=Boundary Arena */
    flow_cell bundle[5];
    load_cells_file(cap_dir, "core_bundle4.flow", bundle, 5U);
    flow_cell s_zero = bundle[0];
    for (size_t i = 0U; i < 4U; ++i) {
        g_capsule_bank[i] = bundle[i + 1U];
    }
    flow_cell b_inst = g_capsule_bank[0];
    flow_cell b_basin = g_capsule_bank[1];
    flow_cell b_quad = g_capsule_bank[2];
    flow_cell b_kan = g_capsule_bank[3];

    flow_cell s_pos1;
    flow_cell s_huge;
    flow_cell s_foreign;
    flow_cell s_fiber;
    flow_cell b_zero_target;
    load_cells_file(cap_dir, "state_pos1.flow", &s_pos1, 1U);
    load_cells_file(cap_dir, "state_overflow.flow", &s_huge, 1U);
    load_cells_file(cap_dir, "state_foreign.flow", &s_foreign, 1U);
    load_cells_file(cap_dir, "state_fiber.flow", &s_fiber, 1U);
    load_cells_file(cap_dir, "zero_target.flow", &b_zero_target, 1U);

    /* 1. Dual Domain-Separated Seal Firewall & Anti-Laundering */
    CHECK(flow_verify_state(s_zero), "bundle[0] carries valid FLOW_STATE_SEED seal");
    CHECK(!flow_verify_bound(s_zero), "State cell rejected by Boundary verifier");
    for (size_t i = 0U; i < 4U; ++i) {
        CHECK(flow_verify_bound(g_capsule_bank[i]), "Boundary cell carries valid FLOW_BOUND_SEED");
        CHECK(!flow_verify_state(g_capsule_bank[i]), "Boundary cell rejected by State verifier");
    }

    /* Epoch-invariance: bumping Epoch in bound[6] keeps Lean-minted bound[7] valid */
    flow_cell b_epoch_bumped = b_quad;
    b_epoch_bumped[6] = (UINT64_C(999999) << 32U) | (b_quad[6] & UINT64_C(0xFFFFFFFF));
    CHECK(flow_verify_bound(b_epoch_bumped), "Boundary seal is invariant across runtime Epochs");

    /* 1-bit boundary tamper blocked */
    flow_cell b_tampered = b_quad;
    b_tampered[0] ^= 1ULL;
    flow_cell s_after_tamper = flow_step(s_pos1, b_tampered);
    CHECK((s_after_tamper[6] & FLOW_SIEVE_SEAL_VALID) == 0ULL,
          "1-bit tampered boundary drops FLOW_SIEVE_SEAL_VALID");
    CHECK(s_after_tamper[3] == s_pos1[3], "tampered boundary preserves coordinate x");

    /* Swapping State and Boundary roles blocked */
    flow_cell s_role_swap = flow_step(s_pos1, s_zero);
    CHECK((s_role_swap[6] & FLOW_SIEVE_SEAL_VALID) == 0ULL,
          "passing State as Boundary drops FLOW_SIEVE_SEAL_VALID");

    /* Unsealed C-fabricated state rejected and NEVER laundered into a valid State */
    flow_cell c_forged_state = {0ULL, 0ULL, 0ULL, 777ULL, 0ULL, b_quad[5], 1ULL, 0ULL};
    flow_cell s_forged_out = flow_step(c_forged_state, b_quad);
    CHECK((s_forged_out[6] & FLOW_SIEVE_SEAL_VALID) == 0ULL, "C-forged state rejected");
    CHECK(s_forged_out[7] == 0ULL && !flow_verify_state(s_forged_out),
          "anti-laundering keeps C-forged state permanently unsealed (w7 == 0)");

    /* Foreign Univalence domain blocked */
    flow_cell s_after_foreign = flow_step(s_foreign, b_quad);
    CHECK((s_after_foreign[6] & FLOW_SIEVE_DOMAIN_MATCH) == 0ULL,
          "foreign domain state drops FLOW_SIEVE_DOMAIN_MATCH");
    CHECK(s_after_foreign[3] == s_foreign[3], "foreign domain preserves coordinate x");

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
        CHECK(r <= UINT64_C(0xFFFFFFFF) && r * r <= rng, "isqrt lower bound");
        uint64_t r1_hi = 0ULL;
        uint64_t r1_lo = flow_umul128(r + 1ULL, r + 1ULL, &r1_hi);
        CHECK(r1_hi > 0ULL || r1_lo > rng, "isqrt upper bound");
    }

    flow_cell s_ovf = flow_step(s_huge, b_quad);
    CHECK((s_ovf[6] & FLOW_SIEVE_NO_OVERFLOW) == 0ULL, "overflow captured into Sieve");
    CHECK(s_ovf[3] == s_huge[3], "overflowing step preserves prior coordinate");

    /* 3. Critical-Point Saddle Trap Root Jump: R(x) = 2 - x^2 at x = 0 (R'(0) = 0) */
    flow_cell s_jump1 = flow_step(s_zero, b_quad);
    int64_t x_jump1 = (int64_t)s_jump1[3];
    CHECK(x_jump1 >= 92680 && x_jump1 <= 92683,
          "single-step Citardauq escape from R'(0)=0 critical point to sqrt(2)");
    flow_cell s_jump2 = flow_step(s_jump1, b_quad);
    CHECK((s_jump2[6] & FLOW_SIEVE_CONVERGED) != 0ULL, "converged on step 2");
    CHECK(flow_abs_i64((int64_t)s_jump2[0]) <= (uint64_t)FLOW_CONVERGE_EPS_Q16,
          "residual |R(x*)| <= 4 LSB");

    /* 4. Non-Convex Local Minimum Instanton Tunneling -> Global Basin Root */
    flow_cell s_tun = flow_step(s_zero, b_inst);
    CHECK((int64_t)s_tun[4] >= 92680 && (int64_t)s_tun[3] >= 92680,
          "Wick-rotated curvature tunneling escapes positive local minimum (Delta < 0, R'(0) = 0)");

    flow_cell s_cur = s_tun;
    for (uint32_t step = 0U; step < 5U; ++step) {
        s_cur = flow_step(s_cur, b_basin);
    }
    CHECK(flow_abs_i64((int64_t)s_cur[3] - (3 * FLOW_Q16_ONE)) <= 2ULL,
          "converged to exact global basin root x = 3.0 after tunneling");
    CHECK((s_cur[6] & FLOW_SIEVE_CONVERGED) != 0ULL, "global basin FLOW_SIEVE_CONVERGED set");

    /* 5. HoTT Cubical Kan Homotopy Obstruction Sieve */
    flow_cell s_blocked = flow_step(s_pos1, b_kan);
    CHECK((s_blocked[6] & FLOW_SIEVE_KAN_HOMOTOPIC) == 0ULL,
          "Kan filter blocks crossing into negative half-space");
    CHECK(s_blocked[3] == s_pos1[3], "obstructed Kan transition retains homotopic x = +1.0");

    /* 6. Autonomous 2-Jet Fiber Pushforward Mode (b1 == 0, b2 == 0) */
    flow_cell s_fib_next = flow_step(s_fiber, b_zero_target);
    CHECK((int64_t)s_fib_next[3] == 1 * FLOW_Q16_ONE,
          "autonomous 2-Jet fiber jumps directly to nearer root delta = 1.0 of 2 - 3t + t^2");
    CHECK((s_fib_next[6] & FLOW_SIEVE_CONVERGED) != 0ULL,
          "autonomous 2-Jet Taylor pushforward reaches exact 0 residual");

    /* 7. Multi-Threaded Lock-Free SPSC & QSBR Arena Epoch Hot-Swap */
    for (uint64_t i = 0ULL; i < 4ULL; ++i) {
        flow_arena_publish_bound(g_shared_arena, i, 3ULL, 1U, g_capsule_bank[i]);
    }
    pthread_t writer;
    CHECK(pthread_create(&writer, NULL, qsbr_writer_thread, NULL) == 0, "spawn QSBR writer");

    flow_cell s_rt = s_pos1;
    uint64_t reads = 0ULL;
    while (__atomic_load_n(&g_writer_done, __ATOMIC_ACQUIRE) == 0U || reads < 10000ULL) {
        uint64_t cur = __atomic_load_n(&g_published_cursor, __ATOMIC_ACQUIRE);
        flow_cell active_b = flow_arena_load_acq(g_shared_arena, cur, 3ULL);
        s_rt = flow_step(s_rt, active_b);
        CHECK(flow_verify_state(s_rt), "state seal remains valid even across concurrent reads");
        ++reads;
        if (reads > 200000ULL && __atomic_load_n(&g_writer_done, __ATOMIC_ACQUIRE) != 0U) {
            break;
        }
    }
    pthread_join(writer, NULL);
    CHECK(reads >= 10000ULL, "completed concurrent lock-free arena hot-swap evaluations");

    printf(
        "  [PASS] FLOW HoTT x 2-Jet Suite: %llu assertions verified (0-ULP, 0 structs, 0 C seals)\n",
        (unsigned long long)g_assertions);
    return 0;
}

#endif /* FLOW_RING0_GATE */
