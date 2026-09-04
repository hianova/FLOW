#ifndef FLOW_BMF_SCHEMA_H
#define FLOW_BMF_SCHEMA_H

#include "bitmanifold.h"

#include <stdint.h>
#include <stddef.h>

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
 * FLOW BitManifold (BMF) Declarative Field Schema Generator (flow_bmf_schema.h)
 * ============================================================================
 *
 * Generates compile-time, zero-cost, branchless getters, setters, pack, and mask
 * functions for 64-bit BitManifold discrete subspace fields.
 * ============================================================================
 */

#define FLOW_BMF_FIELD_DECLARE(FieldPrefix, Offset, Width) \
    FLOW_UNUSED_FN static inline uint64_t FieldPrefix##_get(uint64_t genome) { \
        return FLOW_GENOME_GET(genome, (Offset), (Width)); \
    } \
    \
    FLOW_UNUSED_FN static inline uint64_t FieldPrefix##_set(uint64_t genome, uint64_t val) { \
        return FLOW_GENOME_SET(genome, (Offset), (Width), val); \
    } \
    \
    FLOW_UNUSED_FN static inline uint64_t FieldPrefix##_pack(uint64_t val) { \
        return FLOW_GENOME_PACK(val, (Offset), (Width)); \
    } \
    \
    FLOW_UNUSED_FN static inline uint64_t FieldPrefix##_mask(void) { \
        return FLOW_GENOME_FIELD_MASK((Offset), (Width)); \
    } \
    struct _flow_bmf_field_semi_##FieldPrefix { int _unused; }

#ifdef __cplusplus
}
#endif

#endif /* FLOW_BMF_SCHEMA_H */
