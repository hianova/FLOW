#ifndef FLOW_FIXED_VEC_H
#define FLOW_FIXED_VEC_H

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
 * FLOW Zero-Heap Fixed Flat Vector Primitive (flow_fixed_vec.h)
 * ============================================================================
 *
 * Pure C17 declarative flat array with 64-byte cache alignment, O(1) push/pop,
 * O(1) unordered swap removal, and compile-time capacity bounding.
 * ============================================================================
 */

#define FLOW_FIXED_VEC_DEFINE(VecType, ElementType, Capacity) \
    _Static_assert((Capacity) > 0, #VecType ": Capacity must be greater than 0"); \
    \
    typedef struct VecType { \
        ElementType data[Capacity]; \
        uint32_t count; \
        uint32_t capacity; \
    } FLOW_CACHE_ALIGNED VecType; \
    \
    FLOW_UNUSED_FN static inline void VecType##_init(VecType *vec) { \
        if (!vec) return; \
        memset(vec, 0, sizeof(*vec)); \
        vec->capacity = (uint32_t)(Capacity); \
    } \
    \
    FLOW_UNUSED_FN static inline uint32_t VecType##_count(const VecType *vec) { \
        return vec ? vec->count : 0; \
    } \
    \
    FLOW_UNUSED_FN static inline int VecType##_is_empty(const VecType *vec) { \
        return !vec || vec->count == 0; \
    } \
    \
    FLOW_UNUSED_FN static inline int VecType##_is_full(const VecType *vec) { \
        return vec && vec->count >= (uint32_t)(Capacity); \
    } \
    \
    FLOW_UNUSED_FN static inline int VecType##_push(VecType *vec, const ElementType *item) { \
        if (!vec || !item || vec->count >= (uint32_t)(Capacity)) return 0; \
        vec->data[vec->count++] = *item; \
        return 1; \
    } \
    \
    FLOW_UNUSED_FN static inline int VecType##_pop(VecType *vec, ElementType *out) { \
        if (!vec || vec->count == 0) return 0; \
        vec->count--; \
        if (out) *out = vec->data[vec->count]; \
        return 1; \
    } \
    \
    FLOW_UNUSED_FN static inline ElementType *VecType##_get(VecType *vec, uint32_t idx) { \
        if (!vec || idx >= vec->count) return NULL; \
        return &vec->data[idx]; \
    } \
    \
    FLOW_UNUSED_FN static inline const ElementType *VecType##_cget(const VecType *vec, uint32_t idx) { \
        if (!vec || idx >= vec->count) return NULL; \
        return &vec->data[idx]; \
    } \
    \
    FLOW_UNUSED_FN static inline int VecType##_remove_unordered(VecType *vec, uint32_t idx) { \
        if (!vec || idx >= vec->count) return 0; \
        vec->count--; \
        if (idx < vec->count) { \
            vec->data[idx] = vec->data[vec->count]; \
        } \
        return 1; \
    } \
    \
    FLOW_UNUSED_FN static inline void VecType##_clear(VecType *vec) { \
        if (vec) vec->count = 0; \
    } \
    struct _flow_vec_semi_##VecType { int _unused; }

#ifdef __cplusplus
}
#endif

#endif /* FLOW_FIXED_VEC_H */
