#ifndef FLOW_PRIMITIVE_H
#define FLOW_PRIMITIVE_H
#include "smt.h"
#include "bitmanifold.h"
#include <stddef.h>
#include <stdint.h>
#define FLOW_PRIMITIVE_NAME_MAX 64
#define FLOW_PRIMITIVE_VERSION_MAX 32
#define FLOW_MAX_PRIMITIVE_DRIVERS 16
typedef struct { char name[FLOW_PRIMITIVE_NAME_MAX]; uint64_t max_queue_depth, max_buffer_bytes; uint32_t supports_zero_copy, is_kernel_bypass, genome_bits_required; } FlowHardwareBounds;
typedef struct { uint64_t active_genome; void *user_data; size_t data_len; uint64_t flags; } FlowPrimitiveContext;
typedef struct { int status_code; size_t bytes_transferred; uint64_t latency_cycles; int zero_copy_active; } FlowPrimitiveResult;
typedef struct FlowPrimitiveDriver { char driver_name[FLOW_PRIMITIVE_NAME_MAX], driver_version[FLOW_PRIMITIVE_VERSION_MAX];
    int (*register_primitive)(void); int (*get_hardware_bounds)(FlowHardwareBounds *bounds_out); int (*execute_primitive)(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out); } FlowPrimitiveDriver;
typedef struct { size_t count; const FlowPrimitiveDriver *drivers[FLOW_MAX_PRIMITIVE_DRIVERS]; } FlowPrimitiveRegistry;
void flow_primitive_registry_init(FlowPrimitiveRegistry *reg); int flow_primitive_register(FlowPrimitiveRegistry *reg, const FlowPrimitiveDriver *driver);
const FlowPrimitiveDriver *flow_primitive_lookup(const FlowPrimitiveRegistry *reg, const char *name); size_t flow_primitive_count(const FlowPrimitiveRegistry *reg);
#endif
