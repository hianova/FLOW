#ifndef FLOW_FIXED_RING_H
#define FLOW_FIXED_RING_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #include <stdalign.h>
  #ifndef FLOW_CACHE_ALIGNED
    #define FLOW_CACHE_ALIGNED alignas(64)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #ifndef FLOW_CACHE_ALIGNED
    #define FLOW_CACHE_ALIGNED __attribute__((aligned(64)))
  #endif
#else
  #ifndef FLOW_CACHE_ALIGNED
    #define FLOW_CACHE_ALIGNED
  #endif
#endif

#ifndef FLOW_UNUSED_FN
  #if defined(__GNUC__) || defined(__clang__)
    #define FLOW_UNUSED_FN __attribute__((unused))
  #else
    #define FLOW_UNUSED_FN
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Zero-Heap Fixed Ring Buffer Primitive (flow_fixed_ring.h)
 * ============================================================================
 *
 * Provides a pure C17, power-of-2 branchless ring buffer skeleton with
 * 64-byte cache-line alignment, false-sharing padding, and zero dynamic heap
 * allocation on production paths.
 * ============================================================================
 */

#define FLOW_FIXED_RING_DEFINE(RingType, ElementType, Capacity) \
    _Static_assert(((Capacity) > 0) && (((Capacity) & ((Capacity) - 1)) == 0), \
                   #RingType ": Capacity must be a power of 2"); \
    \
    typedef struct RingType { \
        ElementType items[Capacity]; \
        FLOW_CACHE_ALIGNED uint32_t head; \
        uint8_t _pad_head[60]; \
        FLOW_CACHE_ALIGNED uint32_t tail; \
        uint8_t _pad_tail[60]; \
        uint32_t capacity; \
        uint32_t mask; \
    } FLOW_CACHE_ALIGNED RingType; \
    \
    FLOW_UNUSED_FN static inline void RingType##_init(RingType *ring) { \
        if (!ring) return; \
        memset(ring, 0, sizeof(*ring)); \
        ring->capacity = (uint32_t)(Capacity); \
        ring->mask = (uint32_t)((Capacity) - 1); \
    } \
    \
    FLOW_UNUSED_FN static inline uint32_t RingType##_count(const RingType *ring) { \
        if (!ring) return 0; \
        return ring->tail - ring->head; \
    } \
    \
    FLOW_UNUSED_FN static inline int RingType##_is_empty(const RingType *ring) { \
        if (!ring) return 1; \
        return ring->head == ring->tail; \
    } \
    \
    FLOW_UNUSED_FN static inline int RingType##_is_full(const RingType *ring) { \
        if (!ring) return 0; \
        return (ring->tail - ring->head) >= (uint32_t)(Capacity); \
    } \
    \
    FLOW_UNUSED_FN static inline int RingType##_push(RingType *ring, const ElementType *item) { \
        if (!ring || !item) return 0; \
        if ((ring->tail - ring->head) >= (uint32_t)(Capacity)) return 0; \
        ring->items[ring->tail & (uint32_t)((Capacity) - 1)] = *item; \
        ring->tail++; \
        return 1; \
    } \
    \
    FLOW_UNUSED_FN static inline void RingType##_push_overwrite(RingType *ring, const ElementType *item) { \
        if (!ring || !item) return; \
        if ((ring->tail - ring->head) >= (uint32_t)(Capacity)) { \
            ring->head++; \
        } \
        ring->items[ring->tail & (uint32_t)((Capacity) - 1)] = *item; \
        ring->tail++; \
    } \
    \
    FLOW_UNUSED_FN static inline int RingType##_pop(RingType *ring, ElementType *out) { \
        if (!ring || ring->head == ring->tail) return 0; \
        if (out) { \
            *out = ring->items[ring->head & (uint32_t)((Capacity) - 1)]; \
        } \
        ring->head++; \
        return 1; \
    } \
    \
    FLOW_UNUSED_FN static inline int RingType##_peek(const RingType *ring, ElementType **out) { \
        if (!ring || ring->head == ring->tail || !out) return 0; \
        *out = (ElementType *)&ring->items[ring->head & (uint32_t)((Capacity) - 1)]; \
        return 1; \
    } \
    \
    FLOW_UNUSED_FN static inline ElementType *RingType##_at(RingType *ring, uint32_t logical_index) { \
        if (!ring || logical_index >= (ring->tail - ring->head)) return NULL; \
        return &ring->items[(ring->head + logical_index) & (uint32_t)((Capacity) - 1)]; \
    } \
    \
    FLOW_UNUSED_FN static inline void RingType##_clear(RingType *ring) { \
        if (!ring) return; \
        ring->head = 0; \
        ring->tail = 0; \
    } \
    struct _flow_ring_semi_##RingType { int _unused; }

#ifdef __cplusplus
}
#endif

#endif /* FLOW_FIXED_RING_H */
