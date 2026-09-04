#ifndef FLOW_KV_SCHEMA_H
#define FLOW_KV_SCHEMA_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLOW_KV_STR,
    FLOW_KV_U64,
    FLOW_KV_HEX64,
    FLOW_KV_DOUBLE,
    FLOW_KV_U32,
    FLOW_KV_U8,
    FLOW_KV_INT,
    FLOW_KV_SIZE_T
} FlowKVType;

typedef struct {
    const char *key;
    FlowKVType type;
    size_t offset;
    size_t str_size;
} FlowKVField;

static inline int flow_kv_apply_field(void *base, const FlowKVField *fields, size_t field_count,
                                     const char *key, const char *val) {
    if (base == NULL || fields == NULL || key == NULL || val == NULL) return 0;
    for (size_t i = 0; i < field_count; ++i) {
        if (strcmp(fields[i].key, key) == 0) {
            void *ptr = (char *)base + fields[i].offset;
            switch (fields[i].type) {
                case FLOW_KV_STR: {
                    size_t max_sz = fields[i].str_size;
                    if (max_sz > 0) {
                        strncpy((char *)ptr, val, max_sz - 1);
                        ((char *)ptr)[max_sz - 1] = '\0';
                    }
                    break;
                }
                case FLOW_KV_U64:
                    *(uint64_t *)ptr = (uint64_t)strtoull(val, NULL, 10);
                    break;
                case FLOW_KV_HEX64:
                    *(uint64_t *)ptr = (uint64_t)strtoull(val, NULL, 16);
                    break;
                case FLOW_KV_DOUBLE:
                    *(double *)ptr = strtod(val, NULL);
                    break;
                case FLOW_KV_U32:
                    *(uint32_t *)ptr = (uint32_t)strtoul(val, NULL, 10);
                    break;
                case FLOW_KV_U8:
                    *(uint8_t *)ptr = (uint8_t)atoi(val);
                    break;
                case FLOW_KV_INT:
                    *(int *)ptr = atoi(val);
                    break;
                case FLOW_KV_SIZE_T:
                    *(size_t *)ptr = (size_t)strtoull(val, NULL, 10);
                    break;
            }
            return 1;
        }
    }
    return 0;
}

#define FLOW_KV_FIELD_STR(Type, Field) \
    { #Field, FLOW_KV_STR, offsetof(Type, Field), sizeof(((Type *)0)->Field) }
#define FLOW_KV_FIELD_NAMED_STR(Name, Type, Field) \
    { Name, FLOW_KV_STR, offsetof(Type, Field), sizeof(((Type *)0)->Field) }
#define FLOW_KV_FIELD_U64(Type, Field) \
    { #Field, FLOW_KV_U64, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_NAMED_U64(Name, Type, Field) \
    { Name, FLOW_KV_U64, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_HEX64(Type, Field) \
    { #Field, FLOW_KV_HEX64, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_NAMED_HEX64(Name, Type, Field) \
    { Name, FLOW_KV_HEX64, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_DOUBLE(Type, Field) \
    { #Field, FLOW_KV_DOUBLE, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_NAMED_DOUBLE(Name, Type, Field) \
    { Name, FLOW_KV_DOUBLE, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_U32(Type, Field) \
    { #Field, FLOW_KV_U32, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_NAMED_U32(Name, Type, Field) \
    { Name, FLOW_KV_U32, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_U8(Type, Field) \
    { #Field, FLOW_KV_U8, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_NAMED_U8(Name, Type, Field) \
    { Name, FLOW_KV_U8, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_INT(Type, Field) \
    { #Field, FLOW_KV_INT, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_NAMED_INT(Name, Type, Field) \
    { Name, FLOW_KV_INT, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_SIZE_T(Type, Field) \
    { #Field, FLOW_KV_SIZE_T, offsetof(Type, Field), 0 }
#define FLOW_KV_FIELD_NAMED_SIZE_T(Name, Type, Field) \
    { Name, FLOW_KV_SIZE_T, offsetof(Type, Field), 0 }

#ifdef __cplusplus
}
#endif

#endif /* FLOW_KV_SCHEMA_H */
