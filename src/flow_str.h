#ifndef FLOW_STR_H
#define FLOW_STR_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Safe String & Fast 64-Bit Hashing Primitives (flow_str.h)
 * ============================================================================
 *
 * Pure C17 zero-dependency header providing safe bounded string operations,
 * NULL-tolerant comparisons, and constant-time 64-bit hashing.
 * ============================================================================
 */

#define FLOW_FNV1A_64_OFFSET UINT64_C(14695981039346656037)
#define FLOW_FNV1A_64_PRIME  UINT64_C(1099511628211)

/**
 * flow_str_copy:
 * Safe bounded string copy that is guaranteed to always null-terminate dst
 * if dst_sz > 0. Returns the number of characters copied (excluding null).
 */
static inline size_t flow_str_copy(char *dst, size_t dst_sz, const char *src) {
    if (dst == NULL || dst_sz == 0) return 0;
    if (src == NULL) {
        dst[0] = '\0';
        return 0;
    }
    size_t i = 0;
    while (i + 1 < dst_sz && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

/**
 * flow_str_fmt:
 * Safe bounded vsnprintf wrapper that guarantees null-termination.
 * Returns the number of characters that would have been written.
 */
static inline int flow_str_fmt(char *dst, size_t dst_sz, const char *fmt, ...) {
    if (dst == NULL || dst_sz == 0 || fmt == NULL) return 0;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(dst, dst_sz, fmt, args);
    va_end(args);
    if (written < 0) {
        dst[0] = '\0';
        return 0;
    }
    if ((size_t)written >= dst_sz) {
        dst[dst_sz - 1] = '\0';
    }
    return written;
}

/**
 * flow_hash64_bytes:
 * 64-bit FNV-1a byte hash with zero heap allocation.
 */
static inline uint64_t flow_hash64_bytes(const void *data, size_t len) {
    if (data == NULL || len == 0) return 0;
    const uint8_t *p = (const uint8_t *)data;
    uint64_t hash = FLOW_FNV1A_64_OFFSET;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint64_t)p[i];
        hash *= FLOW_FNV1A_64_PRIME;
    }
    return hash;
}

/**
 * flow_hash64_str:
 * Convenience 64-bit string hash.
 */
static inline uint64_t flow_hash64_str(const char *str) {
    if (str == NULL) return 0;
    return flow_hash64_bytes(str, strlen(str));
}

/**
 * flow_hash64_u64:
 * SplitMix64 single-cycle integer hash for uint64 keys.
 */
static inline uint64_t flow_hash64_u64(uint64_t val) {
    val ^= val >> 30;
    val *= UINT64_C(0xbf58476d1ce4e5b9);
    val ^= val >> 27;
    val *= UINT64_C(0x94d049bb133111eb);
    val ^= val >> 31;
    return val;
}

/**
 * flow_str_eq:
 * NULL-tolerant string equality comparison.
 */
static inline int flow_str_eq(const char *a, const char *b) {
    if (a == b) return 1;
    if (a == NULL || b == NULL) return 0;
    return strcmp(a, b) == 0;
}

/**
 * flow_str_starts_with:
 * Returns 1 if str begins with prefix, 0 otherwise.
 */
static inline int flow_str_starts_with(const char *str, const char *prefix) {
    if (str == NULL || prefix == NULL) return 0;
    size_t len_p = strlen(prefix);
    return strncmp(str, prefix, len_p) == 0;
}

/**
 * flow_str_contains:
 * Returns 1 if str contains substr, 0 otherwise.
 */
static inline int flow_str_contains(const char *str, const char *substr) {
    if (str == NULL || substr == NULL) return 0;
    return strstr(str, substr) != NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_STR_H */
