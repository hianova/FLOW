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
#define FLOW_FNV1A_64_OFFSET UINT64_C(14695981039346656037)
#define FLOW_FNV1A_64_PRIME  UINT64_C(1099511628211)
static inline size_t flow_str_copy(char *dst, size_t dst_sz, const char *src) { if (!dst || !dst_sz) return 0; if (!src) { dst[0] = '\0'; return 0; } size_t i = 0;
    while (i + 1 < dst_sz && src[i] != '\0') { dst[i] = src[i]; i++; } dst[i] = '\0'; return i; }
static inline int flow_str_fmt(char *dst, size_t dst_sz, const char *fmt, ...) { if (!dst || !dst_sz || !fmt) return 0; va_list args; va_start(args, fmt);
    int written = vsnprintf(dst, dst_sz, fmt, args); va_end(args); if (written < 0) { dst[0] = '\0'; return 0; } if ((size_t)written >= dst_sz) dst[dst_sz - 1] = '\0';
    return written; } static inline uint64_t flow_hash64_bytes(const void *data, size_t len) { if (!data || !len) return 0; const uint8_t *p = (const uint8_t *)data;
    uint64_t hash = FLOW_FNV1A_64_OFFSET; for (size_t i = 0; i < len; ++i) { hash ^= (uint64_t)p[i]; hash *= FLOW_FNV1A_64_PRIME; } return hash; }
static inline uint64_t flow_hash64_str(const char *str) { return str ? flow_hash64_bytes(str, strlen(str)) : 0; } static inline uint64_t flow_hash64_u64(uint64_t val) {
    val ^= val >> 30; val *= UINT64_C(0xbf58476d1ce4e5b9); val ^= val >> 27; val *= UINT64_C(0x94d049bb133111eb); return val ^ (val >> 31); }
static inline int flow_str_eq(const char *a, const char *b) { return (a == b) ? 1 : ((!a || !b) ? 0 : (strcmp(a, b) == 0)); }
static inline int flow_str_starts_with(const char *str, const char *prefix) { return (!str || !prefix) ? 0 : (strncmp(str, prefix, strlen(prefix)) == 0); }
static inline int flow_str_contains(const char *str, const char *substr) { return (!str || !substr) ? 0 : (strstr(str, substr) != NULL); }
#ifdef __cplusplus
}
#endif
#endif /* FLOW_STR_H */
