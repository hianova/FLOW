#include "flow.h"

/* ============================================================================
 * Freestanding L0 Q-Polyhedron Quantization & 2-Jet Next-Face Forecast
 * ============================================================================
 * Pure integer Ring-0 / bare-metal implementation (0 OS headers, 0 heap, 0 FPU,
 * 0 undefined runtime symbols on RV32IMAC / ARMv7-M / x86_64 / AArch64).
 * Single unified implementation using FlowI128 / FlowU128 (0 #if forks).
 * ============================================================================ */

int flow_qpoly_quantize_inner(size_t plane_count, size_t dim, uint32_t shift, const int64_t *a_num,
                              const int64_t *b_num, int64_t denom, const int64_t *box_min,
                              const int64_t *box_max, int32_t *a_q_out, int64_t *b_q_out) {
    if (!a_num || !b_num || !box_min || !box_max || !a_q_out || !b_q_out || plane_count == 0 ||
        plane_count > FLOW_QPOLY_PLANES || dim == 0 || shift > 30U || denom <= 0)
        return 0;

    for (size_t j = 0; j < dim; ++j) {
        if (box_min[j] > box_max[j])
            return 0;
    }

    FlowI128 D = flow_i128_from_i64(denom);
    FlowI128 D_half = flow_i128_from_i64(denom >> 1U);
    FlowI128 D_m1 = flow_i128_from_i64(denom - 1);
    FlowU128 uD = flow_u128_from_u64((uint64_t)denom);
    FlowI128 scale = flow_i128_shl(flow_i128_from_i64(1), shift);

    for (size_t i = 0; i < plane_count; ++i) {
        FlowU128 err_num = flow_u128_from_u64(0ULL);
        for (size_t j = 0; j < dim; ++j) {
            uint64_t lo_abs = (box_min[j] < 0) ? (0ULL - (uint64_t)box_min[j]) : (uint64_t)box_min[j];
            uint64_t hi_abs = (box_max[j] < 0) ? (0ULL - (uint64_t)box_max[j]) : (uint64_t)box_max[j];
            uint64_t max_x = (lo_abs > hi_abs) ? lo_abs : hi_abs;

            FlowI128 num = flow_i128_mul(flow_i128_from_i64(a_num[i * dim + j]), scale);
            FlowI128 a_hat;
            if (flow_i128_cmp(num, flow_i128_from_i64(0)) >= 0) {
                a_hat = flow_i128_div(flow_i128_add(num, D_half), D);
            } else {
                a_hat = flow_i128_neg(flow_i128_div(flow_i128_add(flow_i128_neg(num), D_half), D));
            }
            if (flow_i128_cmp(a_hat, flow_i128_from_i64(INT32_MAX)) > 0 ||
                flow_i128_cmp(a_hat, flow_i128_from_i64(INT32_MIN)) < 0)
                return 0;
            a_q_out[i * dim + j] = (int32_t)flow_i128_lo(a_hat);

            FlowI128 rem = flow_i128_sub(flow_i128_mul(a_hat, D), num);
            FlowU128 urem = (flow_i128_cmp(rem, flow_i128_from_i64(0)) < 0)
                                ? flow_u128_neg(flow_u128_from_i128(rem))
                                : flow_u128_from_i128(rem);
            FlowU128 term = flow_u128_from_u64(0ULL);
            if (flow_u128_mul_u64_checked(urem, max_x, &term) ||
                flow_u128_add_checked(err_num, term, &err_num))
                return 0;
        }
        FlowU128 ceil_e_u = flow_u128_from_u64(0ULL);
        if (flow_u128_add_checked(err_num, flow_u128_from_u64((uint64_t)(denom - 1)), &ceil_e_u))
            return 0;
        ceil_e_u = flow_u128_div(ceil_e_u, uD);
        if (flow_u128_cmp(ceil_e_u, flow_u128_from_u64((uint64_t)INT64_MAX)) > 0)
            return 0;
        FlowI128 ceil_e = flow_i128_from_i64((int64_t)flow_u128_lo(ceil_e_u));
        FlowI128 b_scaled = flow_i128_mul(flow_i128_from_i64(b_num[i]), scale);
        FlowI128 floor_b;
        if (flow_i128_cmp(b_scaled, flow_i128_from_i64(0)) >= 0) {
            floor_b = flow_i128_div(b_scaled, D);
        } else {
            floor_b = flow_i128_neg(flow_i128_div(flow_i128_add(flow_i128_neg(b_scaled), D_m1), D));
        }
        FlowI128 b_hat = flow_i128_sub(floor_b, ceil_e);
        if (flow_i128_cmp(b_hat, flow_i128_from_i64(INT64_MAX)) > 0 ||
            flow_i128_cmp(b_hat, flow_i128_from_i64(INT64_MIN)) < 0)
            return 0;
        b_q_out[i] = (int64_t)flow_i128_lo(b_hat);
    }
    return 1;
}

FlowQPolyForecast flow_qpoly_predict_next_face(const int32_t *A_rows, const int64_t *b,
                                               uint64_t active, size_t dim, const int32_t *x,
                                               const int32_t *dx, const int32_t *ddx) {
    FlowQPolyForecast fc = {0U, INT64_MAX, false};
    if (!A_rows || !b || !x || !dx || dim == 0 || active == 0ULL)
        return fc;

    int64_t best_t_q16 = INT64_MAX;
    uint64_t rem = active;
    while (rem != 0ULL) {
        uint32_t i = flow_ctz64_nonzero(rem);
        rem &= (rem - 1ULL);

        const int32_t *row = &A_rows[(size_t)i * dim];
        FlowResultU64 dot_x_res = flow_asm_qdot_i32(row, x, dim, NULL);
        int64_t dot_x = (int64_t)dot_x_res.value;
        bool dot_x_pos_ovf = flow_result_u64_is_boundary(dot_x_res) && (dot_x == INT64_MAX);
        int64_t r_i = (int64_t)flow_asm_qdot_i32(row, dx, dim, NULL).value;
        int64_t c_i = ddx ? (int64_t)flow_asm_qdot_i32(row, ddx, dim, NULL).value : 0;

        if (dot_x >= b[i] || dot_x_pos_ovf) {
            if (r_i > 0 || (r_i == 0 && c_i > 0)) {
                fc.plane = i;
                fc.t_hit_q = 0;
                fc.will_exit = true;
                return fc;
            }
            if (dot_x == b[i] && !dot_x_pos_ovf && r_i < 0 && c_i > 0) {
                uint64_t abs_r = 0ULL - (uint64_t)r_i;
                FlowU128 num_q16 = flow_u128_shl(flow_u128_from_u64(abs_r), FLOW_Q16_SHIFT + 1U);
                FlowU128 den = flow_u128_from_u64((uint64_t)c_i);
                FlowU128 den_half = flow_u128_shr(den, 1U);
                FlowU128 rounded = flow_u128_from_u64(0ULL);
                (void)flow_u128_add_checked(num_q16, den_half, &rounded);
                FlowU128 t_rehit_u = flow_u128_div(rounded, den);
                int64_t t_rehit_q16 = (flow_u128_cmp(t_rehit_u, flow_u128_from_u64((uint64_t)INT64_MAX)) >= 0)
                                          ? INT64_MAX
                                          : (int64_t)flow_u128_lo(t_rehit_u);
                if (t_rehit_q16 > 0 && t_rehit_q16 < best_t_q16) {
                    best_t_q16 = t_rehit_q16;
                    fc.t_hit_q = t_rehit_q16;
                    fc.plane = i;
                    fc.will_exit = true;
                }
            }
            continue;
        }

        int64_t s_i = flow_i128_clamp_i64(flow_i128_sub(flow_i128_from_i64(b[i]), flow_i128_from_i64(dot_x)));
        if (s_i < 0) s_i = 0;

        /*
         * Solve slack(t) = s_i - r_i * t - 0.5 * c_i * t^2 = 0 via pure Q16 2-Jet root primitive:
         * v = s_i, d1 = -r_i, d2 = -c_i
         */
        int64_t neg_r = (r_i == INT64_MIN) ? INT64_MAX : -r_i;
        int64_t neg_c = (c_i == INT64_MIN) ? INT64_MAX : -c_i;
        FlowJet2 slack_jet = flow_jet2_make(s_i, neg_r, neg_c);
        int64_t t_cand_q16 = 0;
        if (flow_jet2_solve_root(slack_jet, best_t_q16, &t_cand_q16) && t_cand_q16 > 0 &&
            t_cand_q16 < best_t_q16) {
            best_t_q16 = t_cand_q16;
            fc.t_hit_q = t_cand_q16;
            fc.plane = i;
            fc.will_exit = true;
        }
    }
    return fc;
}

#if !FLOW_IS_FREESTANDING

#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ============================================================================
 * Hardware Timer Frequency Probe
 * ============================================================================ */

uint64_t flow_hardware_timer_frequency_hz(void) {
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    uint64_t freq = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq ? freq : FLOW_HW_FALLBACK_ARM_HZ;
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    return FLOW_HW_FALLBACK_X86_HZ;
#else
    return FLOW_HW_FALLBACK_GENERIC_HZ;
#endif
}

/* ============================================================================
 * Unified Memory Architecture (UMA) Page-Aligned Shared Pool
 * ============================================================================ */

int flow_uma_buffer_alloc(FlowUmaBuffer *buf, size_t capacity_bytes) {
    if (!buf || capacity_bytes == 0)
        return -1;
    memset(buf, 0, sizeof(*buf));
    long psize = sysconf(_SC_PAGESIZE);
    if (psize <= 0)
        psize = (long)FLOW_HW_PAGE_BYTES;
    size_t page_sz = (size_t)psize;
    size_t aligned_sz = (capacity_bytes + page_sz - 1U) & ~(page_sz - 1U);

#if defined(MAP_ANONYMOUS)
    int flags = MAP_ANONYMOUS | MAP_SHARED;
#elif defined(MAP_ANON)
    int flags = MAP_ANON | MAP_SHARED;
#else
    int flags = MAP_SHARED;
#endif

    void *ptr = mmap(NULL, aligned_sz, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (ptr == MAP_FAILED)
        return -1;

    memset(ptr, 0, aligned_sz);
    buf->cpu_ptr = ptr;
    buf->gpu_shared_handle = ptr;
    buf->capacity_bytes = aligned_sz;
    buf->page_size = page_sz;
    buf->is_uma_coherent = 1;
    return 0;
}

void flow_uma_buffer_free(FlowUmaBuffer *buf) {
    if (!buf || !buf->cpu_ptr || buf->capacity_bytes == 0)
        return;
    munmap(buf->cpu_ptr, buf->capacity_bytes);
    memset(buf, 0, sizeof(*buf));
}

/* ============================================================================
 * Lock-Free QSBR Epoch Pointer Swapping (Zero Heap, Zero Mutexes)
 * ============================================================================ */

int flow_qsbr_init(FlowQsbrEpoch *ctx, void *host_context) {
    if (!ctx)
        return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->host_context = host_context;
    atomic_init(&ctx->epoch, 1ULL);
    atomic_init(&ctx->stopping, 0);
    atomic_init(&ctx->active_bank, NULL);
    for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
        atomic_init(&ctx->readers[i], NULL);
    }
    return 0;
}

int flow_qsbr_fini(FlowQsbrEpoch *ctx) {
    if (!ctx)
        return -1;
    atomic_store_explicit(&ctx->stopping, 1, memory_order_release);
    return 0;
}

int flow_qsbr_reader_register(FlowQsbrEpoch *ctx, FlowQsbrReader *reader) {
    if (!ctx || !reader)
        return -1;
    memset(reader, 0, sizeof(*reader));
    reader->context = ctx;
    atomic_init(&reader->active_epoch, 0ULL);
    atomic_init(&reader->qsbr_epoch, 0ULL);
    atomic_init(&reader->registered, 1);
    atomic_init(&reader->is_offline, 0);
    atomic_init(&reader->is_quarantined, 0);

    for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
        FlowQsbrReader *expected = NULL;
        if (atomic_compare_exchange_strong_explicit(&ctx->readers[i], &expected, reader,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            reader->slot_idx = i;
            return 0;
        }
    }
    return -1;
}

int flow_qsbr_reader_unregister(FlowQsbrReader *reader) {
    if (!reader)
        return -1;
    FlowQsbrEpoch *ctx = reader->context;
    atomic_store_explicit(&reader->is_offline, 1, memory_order_release);
    atomic_store_explicit(&reader->registered, 0, memory_order_release);
    atomic_store_explicit(&reader->active_epoch, 0ULL, memory_order_release);
    if (ctx && reader->slot_idx < FLOW_QSBR_MAX_READERS) {
        atomic_store_explicit(&ctx->readers[reader->slot_idx], NULL, memory_order_release);
    }
    return 0;
}

int flow_qsbr_publish_ptr(FlowQsbrEpoch *ctx, const void *ptr) {
    if (!ctx)
        return -1;
    atomic_store_explicit(&ctx->active_bank, ptr, memory_order_release);
    uint64_t next_ep = atomic_fetch_add_explicit(&ctx->epoch, 1ULL, memory_order_acq_rel) + 1ULL;
    return (int)(next_ep & 0x7FFFFFFFULL);
}

const void *flow_qsbr_load_ptr(const FlowQsbrEpoch *ctx, FlowQsbrReader *reader) {
    if (!ctx)
        return NULL;
    if (reader) {
        uint64_t ep = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
        atomic_store_explicit(&reader->active_epoch, ep, memory_order_release);
    }
    return atomic_load_explicit(&ctx->active_bank, memory_order_acquire);
}

void flow_qsbr_checkpoint(FlowQsbrReader *reader) {
    if (!reader)
        return;
    if (reader->context) {
        uint64_t ep = atomic_load_explicit(&reader->context->epoch, memory_order_acquire);
        atomic_store_explicit(&reader->active_epoch, ep, memory_order_release);
    } else {
        atomic_store_explicit(&reader->active_epoch, 0ULL, memory_order_release);
    }
}

void flow_qsbr_offline(FlowQsbrReader *reader) {
    if (!reader)
        return;
    atomic_store_explicit(&reader->is_offline, 1, memory_order_release);
    atomic_store_explicit(&reader->active_epoch, 0ULL, memory_order_release);
}

void flow_qsbr_online(FlowQsbrReader *reader) {
    if (!reader)
        return;
    atomic_store_explicit(&reader->is_offline, 0, memory_order_release);
    if (reader->context) {
        uint64_t ep = atomic_load_explicit(&reader->context->epoch, memory_order_acquire);
        atomic_store_explicit(&reader->active_epoch, ep, memory_order_release);
    }
}

int flow_qsbr_synchronize(FlowQsbrEpoch *ctx, uint64_t timeout_ns) {
    if (!ctx)
        return -1;
    uint64_t target_ep = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
    uint64_t start_cyc = flow_hardware_cycles();
    uint64_t freq = flow_hardware_timer_frequency_hz();
    FlowU128 timeout_cyc_128 = flow_u128_mul_u64(timeout_ns ? timeout_ns : 10000000ULL, freq);
    uint64_t budget_cyc = (freq > 0ULL)
                              ? (uint64_t)flow_u128_lo(flow_u128_div(timeout_cyc_128, flow_u128_from_u64(1000000000ULL)))
                              : 10000000ULL;
    for (;;) {
        bool all_quiescent = true;
        for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
            FlowQsbrReader *r = atomic_load_explicit(&ctx->readers[i], memory_order_acquire);
            if (!r || !atomic_load_explicit(&r->registered, memory_order_acquire) ||
                atomic_load_explicit(&r->is_offline, memory_order_acquire)) {
                continue;
            }
            uint64_t act_ep = atomic_load_explicit(&r->active_epoch, memory_order_acquire);
            if (act_ep != 0ULL && act_ep < target_ep) {
                all_quiescent = false;
                break;
            }
        }
        if (all_quiescent)
            return 0;
        uint64_t now_cyc = flow_hardware_cycles();
        if (now_cyc >= start_cyc && (now_cyc - start_cyc) >= budget_cyc) {
            return -1;
        }
        sched_yield();
    }
}

#endif /* !FLOW_IS_FREESTANDING */
