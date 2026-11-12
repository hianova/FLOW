#include "flow_io.h"

#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ============================================================================
 * 1. Hardware Timer Frequency Probe
 * ============================================================================ */

uint64_t flow_hardware_timer_frequency_hz(void) {
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    uint64_t freq = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq ? freq : FLOW_HW_FALLBACK_ARM_HZ;
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    return FLOW_HW_FALLBACK_X86_HZ;
#else
    return 1000000000ULL;
#endif
}

/* ============================================================================
 * 2. Unified Memory Architecture (UMA) Page-Aligned Shared Pool
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
 * 3. Lock-Free QSBR Epoch Swap & Resource Sieve (Zero Heap, Zero Mutexes)
 * ============================================================================ */

int flow_reload_init(FlowReloadContext *ctx, void *host_context) {
    if (!ctx)
        return FLOW_RELOAD_INVALID;
    memset(ctx, 0, sizeof(*ctx));
    ctx->host_context = host_context;
    atomic_init(&ctx->epoch, 1ULL);
    atomic_init(&ctx->stopping, 0);
    atomic_init(&ctx->active_bank, NULL);
    atomic_init(&ctx->active_unit_slot, NULL);
    atomic_init(&ctx->next_slot_idx, 0U);
    for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
        atomic_init(&ctx->readers[i], NULL);
    }
    return FLOW_RELOAD_OK;
}

int flow_reload_fini(FlowReloadContext *ctx) {
    if (!ctx)
        return FLOW_RELOAD_INVALID;
    atomic_store_explicit(&ctx->stopping, 1, memory_order_release);
    for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
        FlowReloadReader *r = atomic_load_explicit(&ctx->readers[i], memory_order_acquire);
        if (r && atomic_load_explicit(&r->registered, memory_order_acquire) &&
            atomic_load_explicit(&r->active_epoch, memory_order_acquire) != 0ULL) {
            atomic_store_explicit(&ctx->stopping, 0, memory_order_release);
            return FLOW_RELOAD_BUSY;
        }
    }
    const FlowUnitSlot *slot = atomic_load_explicit(&ctx->active_unit_slot, memory_order_acquire);
    if (slot && slot->unit && slot->unit->drop && slot->state) {
        slot->unit->drop(ctx->host_context, slot->state);
    }
    atomic_store_explicit(&ctx->active_bank, NULL, memory_order_release);
    atomic_store_explicit(&ctx->active_unit_slot, NULL, memory_order_release);
    return FLOW_RELOAD_OK;
}

int flow_reload_reader_register(FlowReloadContext *ctx, FlowReloadReader *r) {
    if (!ctx || !r || atomic_load_explicit(&ctx->stopping, memory_order_acquire))
        return FLOW_RELOAD_INVALID;
    memset(r, 0, sizeof(*r));
    r->context = ctx;
    uint64_t ep = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
    atomic_init(&r->active_epoch, 0ULL);
    atomic_init(&r->qsbr_epoch, ep);
    atomic_init(&r->registered, 1);
    atomic_init(&r->is_offline, 0);
    atomic_init(&r->is_quarantined, 0);

    for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
        FlowReloadReader *expected = NULL;
        if (atomic_compare_exchange_strong_explicit(&ctx->readers[i], &expected, r,
                                                    memory_order_acq_rel, memory_order_relaxed)) {
            r->slot_idx = i;
            return FLOW_RELOAD_OK;
        }
    }
    atomic_store_explicit(&r->registered, 0, memory_order_relaxed);
    r->context = NULL;
    return FLOW_RELOAD_BUSY;
}

int flow_reload_reader_unregister(FlowReloadReader *r) {
    if (!r || !r->context || !atomic_load_explicit(&r->registered, memory_order_acquire))
        return FLOW_RELOAD_INVALID;
    if (atomic_load_explicit(&r->active_epoch, memory_order_acquire) != 0ULL)
        return FLOW_RELOAD_BUSY;
    FlowReloadContext *ctx = r->context;
    uint32_t slot = r->slot_idx;
    if (slot < FLOW_QSBR_MAX_READERS) {
        atomic_store_explicit(&ctx->readers[slot], NULL, memory_order_release);
    }
    atomic_store_explicit(&r->registered, 0, memory_order_release);
    r->context = NULL;
    return FLOW_RELOAD_OK;
}

int flow_qsbr_publish_ptr(FlowReloadContext *ctx, const void *constraint_bank) {
    if (!ctx || !constraint_bank)
        return FLOW_RELOAD_INVALID;
    atomic_store_explicit(&ctx->active_bank, constraint_bank, memory_order_release);
    atomic_fetch_add_explicit(&ctx->epoch, 1ULL, memory_order_seq_cst);
    return FLOW_RELOAD_OK;
}

const void *flow_qsbr_load_ptr(const FlowReloadContext *ctx, FlowReloadReader *reader) {
    if (!ctx)
        return NULL;
    if (reader) {
        uint64_t ep = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
        atomic_store_explicit(&reader->qsbr_epoch, ep, memory_order_release);
    }
    return atomic_load_explicit(&ctx->active_bank, memory_order_acquire);
}

int flow_reload_activate(FlowReloadContext *ctx, const FlowUnit *unit) {
    if (!ctx || !unit || !unit->run)
        return FLOW_RELOAD_INVALID;
    if (unit->abi_version != 0U && unit->abi_version != FLOW_RELOAD_ABI_VERSION)
        return FLOW_RELOAD_INCOMPATIBLE;
    void *state = NULL;
    if (unit->init && unit->init(ctx->host_context, &state) != 0)
        return FLOW_RELOAD_INVALID;
    const FlowUnitSlot *prev_slot =
        atomic_load_explicit(&ctx->active_unit_slot, memory_order_acquire);
    uint32_t idx = atomic_fetch_add_explicit(&ctx->next_slot_idx, 1U, memory_order_relaxed) &
                   (FLOW_UNIT_SLOT_RING - 1U);
    FlowUnitSlot *new_slot = &ctx->unit_slots[idx];
    new_slot->unit = unit;
    new_slot->state = state;
    atomic_store_explicit(&ctx->active_unit_slot, (const FlowUnitSlot *)new_slot,
                          memory_order_release);
    atomic_fetch_add_explicit(&ctx->epoch, 1ULL, memory_order_seq_cst);
    if (prev_slot && prev_slot != new_slot && prev_slot->unit && prev_slot->unit->drop &&
        prev_slot->state) {
        (void)flow_qsbr_synchronize(ctx, 1000000ULL);
        prev_slot->unit->drop(ctx->host_context, prev_slot->state);
    }
    return FLOW_RELOAD_OK;
}

int flow_reload_call(FlowReloadContext *ctx, FlowReloadReader *r, const void *input, void *output) {
    if (!ctx || !r || r->context != ctx)
        return -1;
    uint64_t ep = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
    atomic_store_explicit(&r->active_epoch, ep, memory_order_seq_cst);
    const FlowUnitSlot *slot = atomic_load_explicit(&ctx->active_unit_slot, memory_order_acquire);
    int rc =
        (slot && slot->unit) ? slot->unit->run(ctx->host_context, slot->state, input, output) : -1;
    atomic_store_explicit(&r->qsbr_epoch, ep, memory_order_release);
    atomic_store_explicit(&r->active_epoch, 0ULL, memory_order_release);
    return rc;
}

uint64_t flow_reload_generation(const FlowReloadContext *ctx) {
    return ctx ? atomic_load_explicit(&ctx->epoch, memory_order_acquire) : 0ULL;
}

void flow_qsbr_checkpoint(FlowReloadReader *r) {
    if (!r)
        return;
    uint64_t tgt = r->context ? atomic_load_explicit(&r->context->epoch, memory_order_seq_cst)
                              : (atomic_load_explicit(&r->qsbr_epoch, memory_order_relaxed) + 1ULL);
    atomic_store_explicit(&r->qsbr_epoch, tgt, memory_order_seq_cst);
}

void flow_qsbr_offline(FlowReloadReader *r) {
    if (r)
        atomic_store_explicit(&r->is_offline, 1, memory_order_release);
}

void flow_qsbr_online(FlowReloadReader *r) {
    if (r)
        atomic_store_explicit(&r->is_offline, 0, memory_order_release);
}

int flow_qsbr_synchronize(FlowReloadContext *ctx, uint64_t timeout_ns) {
    if (!ctx)
        return FLOW_RELOAD_INVALID;
    uint64_t tgt = atomic_load_explicit(&ctx->epoch, memory_order_seq_cst);
    uint64_t freq = flow_hardware_timer_frequency_hz();
    uint64_t start_cyc = flow_hardware_cycles();
    uint64_t eff_timeout_ns = timeout_ns ? timeout_ns : 10000000ULL;
    uint64_t budget_cyc = eff_timeout_ns;
    if (freq > 0ULL) {
#if FLOW_HAS_INT128
        __extension__ typedef unsigned __int128 u128;
        budget_cyc = (uint64_t)flow_u128_div_u128(
            (u128)eff_timeout_ns * (u128)freq + (u128)999999999ULL, (u128)1000000000ULL);
#else
        FlowU128Pair prod = {0ULL, 0ULL};
        flow_mul_u64_to_u128(eff_timeout_ns, freq, &prod.hi, &prod.lo);
        FlowU128Pair rounded = {0ULL, 0ULL};
        (void)flow_u128_add_checked(prod, flow_u128_make(0ULL, 999999999ULL), &rounded);
        budget_cyc = flow_u128_div_pair(rounded, flow_u128_make(0ULL, 1000000000ULL)).lo;
#endif
    }

    for (;;) {
        int ok = 1;
        for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
            const FlowReloadReader *r =
                atomic_load_explicit(&ctx->readers[i], memory_order_acquire);
            if (!r)
                continue;
            if (atomic_load_explicit(&r->registered, memory_order_acquire) &&
                !atomic_load_explicit(&r->is_offline, memory_order_acquire) &&
                (atomic_load_explicit(&r->active_epoch, memory_order_seq_cst) != 0ULL ||
                 atomic_load_explicit(&r->qsbr_epoch, memory_order_acquire) < tgt)) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return FLOW_RELOAD_OK;
        uint64_t now_cyc = flow_hardware_cycles();
        if (now_cyc >= start_cyc && (now_cyc - start_cyc) >= budget_cyc)
            break;
        sched_yield();
    }
    return FLOW_RELOAD_BUSY;
}

uint64_t flow_reload_resource_sieve(const FlowReloadContext *ctx, const FlowReloadReader *r) {
    uint64_t mask = 0ULL;
    if (ctx && !atomic_load_explicit(&ctx->stopping, memory_order_acquire)) {
        mask |= (uint64_t)FLOW_RES_PAGES_PREFAULTED;
        mask |= (uint64_t)FLOW_RES_RING_HAS_SPACE;
        if (atomic_load_explicit(&ctx->active_bank, memory_order_acquire) != NULL ||
            atomic_load_explicit(&ctx->active_unit_slot, memory_order_acquire) != NULL) {
            mask |= (uint64_t)FLOW_RES_UNIT_PUBLISHED;
        }
    }
    if (r && atomic_load_explicit(&r->registered, memory_order_acquire) &&
        !atomic_load_explicit(&r->is_offline, memory_order_acquire) &&
        !atomic_load_explicit(&r->is_quarantined, memory_order_acquire)) {
        mask |= (uint64_t)FLOW_RES_READER_ONLINE;
    }
    return mask;
}
