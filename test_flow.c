#include "flow.h"

#if defined(FLOW_RING0_GATE)

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ != 0)
#error "FLOW_RING0_GATE requires -ffreestanding (__STDC_HOSTED__ == 0)"
#endif

flow_cell flow_ring0_step_entry(flow_cell state, flow_cell bound);
flow_cell flow_ring0_atlas_entry(flow_cell state, const flow_cell *arena, uint64_t mask,
                                 int64_t u_ext, uint16_t mode_req);
void flow_ring0_arena_entry(flow_cell *arena, uint64_t idx, uint64_t mask, uint32_t epoch,
                            const uint8_t raw[64], uint8_t out[64]);

flow_cell flow_ring0_step_entry(flow_cell state, flow_cell bound) {
    return flow_step(state, bound);
}

flow_cell flow_ring0_atlas_entry(flow_cell state, const flow_cell *arena, uint64_t mask,
                                 int64_t u_ext, uint16_t mode_req) {
    return flow_step_atlas(state, arena, mask, u_ext, mode_req);
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

static flow_cell g_shared_arena[4];
static flow_cell g_chart_bank[4];
static uint32_t g_writer_done = 0U;

static void load_cells_file(const char *dir, const char *name, flow_cell *out_cells, size_t count) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *fp = fopen(path, "rb");
    CHECK(fp != NULL, "open Lean-minted .flow artifact");
    for (size_t i = 0U; i < count; ++i) {
        uint8_t raw[64];
        size_t got = fread(raw, 1U, 64U, fp);
        CHECK(got == 64U, "read 64-byte cell");
        out_cells[i] = flow_cell_from_bytes(raw);
    }
    int extra = fgetc(fp);
    fclose(fp);
    CHECK(extra == EOF, "exact expected .flow byte size");
}

static void *qsbr_writer_thread(void *arg) {
    (void)arg;
    for (uint64_t step = 1ULL; step <= 20000ULL; ++step) {
        uint32_t epoch = (uint32_t)(step + 1U);
        flow_arena_publish_bound(g_shared_arena, step, 3ULL, epoch, g_chart_bank[step & 3ULL]);
    }
    __atomic_store_n(&g_writer_done, 1U, __ATOMIC_RELEASE);
    return NULL;
}

int main(int argc, char **argv) {
    const char *cap_dir = (argc > 1) ? argv[1] : "build/capsules";

    /* 1. Exact 0-ULP Branchless Integer Square Root Audit */
    CHECK(flow_isqrt_u64(0ULL) == 0ULL, "isqrt(0) == 0");
    CHECK(flow_isqrt_u64(1ULL) == 1ULL, "isqrt(1) == 1");
    CHECK(flow_isqrt_u64(UINT64_MAX) == UINT64_C(4294967295), "isqrt(UINT64_MAX) == 2^32 - 1");
    for (uint64_t n = 1ULL; n <= 4096ULL; ++n) {
        uint64_t sq = n * n;
        CHECK(flow_isqrt_u64(sq) == n, "exact square isqrt(n^2) == n");
        CHECK(flow_isqrt_u64(sq - 1ULL) == n - 1ULL, "lower edge isqrt(n^2 - 1) == n - 1");
    }

    /* 2. Load Self-Contained Lean Manifold Atlas & Probes */
    flow_cell bundle[5];
    load_cells_file(cap_dir, "core_atlas4.flow", bundle, 5U);
    flow_cell s_init = bundle[0];
    for (size_t i = 0U; i < 4U; ++i) {
        g_chart_bank[i] = bundle[i + 1U];
        flow_arena_publish_bound(g_shared_arena, (uint64_t)i, 3ULL, 1U, g_chart_bank[i]);
    }

    flow_cell s_chart0;
    flow_cell s_chart2;
    flow_cell s_overflow;
    flow_cell s_foreign;
    flow_cell s_fiber;
    flow_cell b_zero_target;
    load_cells_file(cap_dir, "state_chart0.flow", &s_chart0, 1U);
    load_cells_file(cap_dir, "state_chart2.flow", &s_chart2, 1U);
    load_cells_file(cap_dir, "state_overflow.flow", &s_overflow, 1U);
    load_cells_file(cap_dir, "state_foreign.flow", &s_foreign, 1U);
    load_cells_file(cap_dir, "state_fiber.flow", &s_fiber, 1U);
    load_cells_file(cap_dir, "zero_target.flow", &b_zero_target, 1U);

    CHECK(flow_verify_state(s_init), "initial bundle State seal valid");
    CHECK(!flow_verify_bound(s_init), "State cell rejected as Chart (domain-separated seed)");
    for (size_t i = 0U; i < 4U; ++i) {
        CHECK(flow_verify_bound(g_chart_bank[i]), "Chart seal valid");
        CHECK(!flow_verify_state(g_chart_bank[i]), "Chart cell rejected as State");
    }

    /* 3. Epoch-Invariant Hot-Swap Seal Preservation */
    uint64_t orig_seal = g_chart_bank[1][7];
    flow_arena_publish_bound(g_shared_arena, 1ULL, 3ULL, 99U, g_chart_bank[1]);
    flow_cell hot_b1 = flow_arena_load_acq(g_shared_arena, 1ULL, 3ULL);
    CHECK((uint32_t)(hot_b1[6] >> 32U) == 99U, "published epoch updated to 99");
    CHECK(hot_b1[7] == orig_seal && flow_verify_bound(hot_b1),
          "Lean chart seal remains valid without C re-signing");

    /* 4. Seal Tamper, Foreign Domain & Anti-Laundering Firewall */
    flow_cell b_tampered = g_chart_bank[1];
    b_tampered[0] ^= 1ULL;
    flow_cell s_tamp_b = flow_step(s_init, b_tampered);
    CHECK((s_tamp_b[6] & FLOW_SIEVE_SEAL_VALID) == 0ULL && s_tamp_b[3] == s_init[3],
          "1-bit tampered chart rejected");

    flow_cell s_forged = {0ULL, 0ULL, 0ULL, 0ULL, 0ULL, s_init[5], 1ULL, 0ULL};
    flow_cell s_forged_next = flow_step(s_forged, g_chart_bank[1]);
    CHECK((s_forged_next[6] & FLOW_SIEVE_SEAL_VALID) == 0ULL && s_forged_next[7] == 0ULL &&
              !flow_verify_state(s_forged_next),
          "unsealed C-forged state rejected and never laundered");

    flow_cell s_for_next = flow_step(s_foreign, g_chart_bank[1]);
    CHECK((s_for_next[6] & FLOW_SIEVE_DOMAIN_MATCH) == 0ULL && s_for_next[3] == s_foreign[3],
          "foreign DomainTag state rejected");

    /* 5. Chart 0: Instanton Curvature Tunneling (Delta < 0, no real roots) */
    flow_cell s_tun = flow_step_atlas(s_chart0, g_shared_arena, 3ULL, 0, 0U);
    CHECK((int64_t)s_tun[4] == FLOW_Q16_ONE,
          "Wick-rotated curvature tunneling advances sqrt(4)/1 = 1.0");
    CHECK((s_tun[6] & FLOW_SIEVE_CONVERGED) == 0ULL && (s_tun[6] & FLOW_SIEVE_NO_OVERFLOW) != 0ULL,
          "tunneling step reports non-converged barrier without overflow");

    /* 6. Chart 2: Stationary Saddle Escape (b1 == 0 at x == 0 -> exact Citardauq jump) */
    flow_cell s_sad = flow_step_atlas(s_chart2, g_shared_arena, 3ULL, 12 * FLOW_Q16_ONE, 0x0003U);
    CHECK((int64_t)s_sad[3] == 4 * FLOW_Q16_ONE && (s_sad[6] & FLOW_SIEVE_CONVERGED) != 0ULL,
          "2-Jet Citardauq jumps from x=0 critical point directly to x* = sqrt(16) = 4.0");

    /* 7. Multi-Chart Schmitt Hysteresis Gluing & Orthogonal Cubical Kan Mode Lock */
    flow_cell st = s_init; /* Starts on Chart 1, x = 0.0, mode = 0x0003 */
    st = flow_step_atlas(st, g_shared_arena, 3ULL, 3 * FLOW_Q16_ONE, 0x0003U);
    CHECK(((st[5] >> 16U) & 0xFFFFULL) == 1ULL && (int64_t)st[3] == 3 * FLOW_Q16_ONE,
          "Chart 1 tracks u_ext = 3.0 and stays on Chart 1");
    CHECK((st[3] & 0xFFFFULL) == 0ULL && (st[5] & 0xFFFFULL) == 0x0003ULL,
          "discrete mode 0x0003 lives orthogonally in lane[5] without polluting Q48.16 lane[3]");

    /* Surge u_ext = 12.0 > bEnter(10.0) -> transitions Chart 1 -> Chart 2 */
    st = flow_step_atlas(st, g_shared_arena, 3ULL, 12 * FLOW_Q16_ONE, 0x0003U);
    CHECK(((st[5] >> 16U) & 0xFFFFULL) == 2ULL, "u_ext > 10.0 transitions to Chart 2 (Surge)");

    /* Deadband u_ext = 8.0 in [6.0, 50.0] + request mode 0xFF00 (tries to clear locked 0x0003) */
    st = flow_step_atlas(st, g_shared_arena, 3ULL, 8 * FLOW_Q16_ONE, 0xFF00U);
    CHECK(((st[5] >> 16U) & 0xFFFFULL) == 2ULL, "Schmitt deadband [6, 50] prevents chart flapping");
    CHECK((st[5] & 0xFFFFULL) == 0xFF03ULL,
          "Kan lock preserves 0x0003 while updating unlocked 0xFF00 bits orthogonally");

    /* Extreme shock u_ext = 80.0 > bEnter(50.0) -> transitions Chart 2 -> Chart 3 (Safe Harbor) */
    st = flow_step_atlas(st, g_shared_arena, 3ULL, 80 * FLOW_Q16_ONE, 0xFF03U);
    CHECK(((st[5] >> 16U) & 0xFFFFULL) == 3ULL,
          "u_ext > 50.0 transitions to Chart 3 (Safe Harbor)");

    /* On Chart 3 (uGain = 0, reqSieve includes KAN_HOMOTOPIC): Kan violation blocks commit */
    flow_cell st_kan_fail = flow_step_atlas(st, g_shared_arena, 3ULL, 200 * FLOW_Q16_ONE, 0x0000U);
    CHECK((st_kan_fail[6] & FLOW_SIEVE_KAN_HOMOTOPIC) == 0ULL && st_kan_fail[3] == st[3],
          "Kan homotopy violation on Safe Harbor blocks state mutation");

    /* Homotopic mode request on Chart 3 isolates u_ext = 200.0 (uGain == 0) and pulls x -> 1.0 */
    st = flow_step_atlas(st, g_shared_arena, 3ULL, 200 * FLOW_Q16_ONE, 0x0003U);
    CHECK((int64_t)st[3] == 1 * FLOW_Q16_ONE && (st[6] & FLOW_SIEVE_CONVERGED) != 0ULL,
          "Safe Harbor chart (uGain == 0) isolates shock and stabilizes at x* = 1.0");

    /* Cool-down u_ext = 1.0 < bExit(2.0) -> recovers from Chart 3 back to Chart 1 */
    st = flow_step_atlas(st, g_shared_arena, 3ULL, 1 * FLOW_Q16_ONE, 0x0003U);
    CHECK(((st[5] >> 16U) & 0xFFFFULL) == 1ULL, "cool-down u_ext < 2.0 recovers to Chart 1");

    /* 8. Signed Overflow Capture & Autonomous 2-Jet Fiber Pushforward */
    flow_cell s_ovf = flow_step_atlas(s_overflow, g_shared_arena, 3ULL, 0, 0U);
    CHECK((s_ovf[6] & FLOW_SIEVE_NO_OVERFLOW) == 0ULL && s_ovf[3] == s_overflow[3],
          "hardware overflow capture aborts commit");

    flow_cell s_fib = flow_step(s_fiber, b_zero_target);
    CHECK((int64_t)s_fib[3] == 1 * FLOW_Q16_ONE && (s_fib[6] & FLOW_SIEVE_CONVERGED) != 0ULL,
          "autonomous 2-Jet fiber solves 2 - 3t + t^2 = 0 -> x* = 1.0");

    /* 9. Multi-Threaded Lock-Free QSBR Atlas Hot-Swap */
    pthread_t writer;
    CHECK(pthread_create(&writer, NULL, qsbr_writer_thread, NULL) == 0, "spawn QSBR writer");
    flow_cell s_mt = s_init;
    uint64_t reads = 0ULL;
    while (__atomic_load_n(&g_writer_done, __ATOMIC_ACQUIRE) == 0U || reads < 10000ULL) {
        s_mt = flow_step_atlas(s_mt, g_shared_arena, 3ULL, 4 * FLOW_Q16_ONE, 0x0003U);
        CHECK(flow_verify_state(s_mt),
              "state seal remains valid across concurrent Atlas hot-swaps");
        ++reads;
        if (reads > 200000ULL && __atomic_load_n(&g_writer_done, __ATOMIC_ACQUIRE) != 0U) {
            break;
        }
    }
    pthread_join(writer, NULL);
    CHECK(reads >= 10000ULL, "completed concurrent lock-free Atlas evaluations");

    printf("  [PASS] FLOW HoTT x 2-Jet Atlas Suite: %llu assertions verified (0-ULP, 0 structs)\n",
           (unsigned long long)g_assertions);
    return 0;
}

#endif /* FLOW_RING0_GATE */
