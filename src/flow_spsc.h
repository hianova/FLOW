#ifndef FLOW_SPSC_H
#define FLOW_SPSC_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define FLOW_SPSC_UNUSED __attribute__((unused))
#else
#define FLOW_SPSC_UNUSED
#endif

/* =========================================================================
 * FLOW Zero-Allocation Lock-Free Single-Producer Single-Consumer (SPSC)
 * Ring Buffer Primitive for Explicit C17 Pipelines (Cache-Line Padded)
 * ========================================================================= */

#define FLOW_SPSC_DEFINE(RingType, ElemType, CapacityPow2)                                 \
    _Static_assert(((CapacityPow2) > 0) && (((CapacityPow2) & ((CapacityPow2) - 1)) == 0), \
                   "FLOW_SPSC_DEFINE CapacityPow2 must be a power of 2");                  \
    typedef struct {                                                                       \
        _Alignas(64) _Atomic size_t head;                                                  \
        uint8_t _pad_head[64 - sizeof(_Atomic size_t)];                                    \
        _Alignas(64) _Atomic size_t tail;                                                  \
        uint8_t _pad_tail[64 - sizeof(_Atomic size_t)];                                    \
        _Alignas(64) ElemType slots[CapacityPow2];                                         \
    } RingType;                                                                            \
                                                                                           \
    static inline FLOW_SPSC_UNUSED void RingType##_init(RingType *r) {                     \
        if (!r)                                                                            \
            return;                                                                        \
        atomic_init(&r->head, 0u);                                                         \
        atomic_init(&r->tail, 0u);                                                         \
    }                                                                                      \
                                                                                           \
    static inline FLOW_SPSC_UNUSED int RingType##_push(RingType *r, ElemType val) {        \
        if (!r)                                                                            \
            return 0;                                                                      \
        const size_t h = atomic_load_explicit(&r->head, memory_order_relaxed);             \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_acquire);             \
        if ((h - t) >= (size_t)(CapacityPow2))                                             \
            return 0;                                                                      \
        r->slots[h & ((size_t)(CapacityPow2) - 1u)] = val;                                 \
        atomic_store_explicit(&r->head, h + 1u, memory_order_release);                     \
        return 1;                                                                          \
    }                                                                                      \
                                                                                           \
    static inline FLOW_SPSC_UNUSED int RingType##_pop(RingType *r, ElemType *out) {        \
        if (!r || !out)                                                                    \
            return 0;                                                                      \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);             \
        const size_t h = atomic_load_explicit(&r->head, memory_order_acquire);             \
        if (t == h)                                                                        \
            return 0;                                                                      \
        *out = r->slots[t & ((size_t)(CapacityPow2) - 1u)];                                \
        atomic_store_explicit(&r->tail, t + 1u, memory_order_release);                     \
        return 1;                                                                          \
    }                                                                                      \
                                                                                           \
    static inline FLOW_SPSC_UNUSED size_t RingType##_size(const RingType *r) {             \
        if (!r)                                                                            \
            return 0u;                                                                     \
        const size_t h = atomic_load_explicit(&r->head, memory_order_acquire);             \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_acquire);             \
        return h - t;                                                                      \
    }

#ifdef __cplusplus
}
#endif

#endif /* FLOW_SPSC_H */
