#ifndef FLOW_MOCK_DRIVER_H
#define FLOW_MOCK_DRIVER_H

#include "primitive.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Minimal ABI Mock Driver Declarator (flow_mock_driver.h)
 * ============================================================================
 *
 * Provides a pure C17 declarative macro to generate complete 3-Function Minimal
 * Primitive Drivers (register_primitive, get_hardware_bounds, execute_primitive)
 * for testing, rapid prototyping, and SMT polytope verification.
 * ============================================================================
 */

typedef struct {
    const char *driver_name;
    const char *driver_version;
    uint64_t queue_depth;
    uint64_t buffer_bytes;
    uint32_t zero_copy;
    uint32_t is_kernel_bypass;
    uint32_t genome_bits_required;
    uint64_t simulated_latency_cycles;
    int (*custom_execute)(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out);
} FlowMockDriverConfig;

#define FLOW_DECLARE_MOCK_DRIVER(drv_sym, ...) \
    static inline FlowMockDriverConfig _flow_mock_cfg_##drv_sym(void) { \
        FlowMockDriverConfig _cfg = { __VA_ARGS__ }; \
        if (_cfg.driver_name == NULL) _cfg.driver_name = #drv_sym; \
        if (_cfg.driver_version == NULL) _cfg.driver_version = "v1.0-mock"; \
        if (_cfg.queue_depth == 0) _cfg.queue_depth = 1024; \
        if (_cfg.buffer_bytes == 0) _cfg.buffer_bytes = 64 * 1024 * 1024; \
        if (_cfg.genome_bits_required == 0) _cfg.genome_bits_required = 4; \
        if (_cfg.simulated_latency_cycles == 0) _cfg.simulated_latency_cycles = 50; \
        return _cfg; \
    } \
    \
    static int _flow_mock_reg_##drv_sym(void) { \
        return 1; \
    } \
    \
    static int _flow_mock_bounds_##drv_sym(FlowHardwareBounds *bounds_out) { \
        if (!bounds_out) return 0; \
        FlowMockDriverConfig _c = _flow_mock_cfg_##drv_sym(); \
        strncpy(bounds_out->name, _c.driver_name ? _c.driver_name : #drv_sym, sizeof(bounds_out->name) - 1); \
        bounds_out->name[sizeof(bounds_out->name) - 1] = '\0'; \
        bounds_out->max_queue_depth = _c.queue_depth; \
        bounds_out->max_buffer_bytes = _c.buffer_bytes; \
        bounds_out->supports_zero_copy = _c.zero_copy; \
        bounds_out->is_kernel_bypass = _c.is_kernel_bypass; \
        bounds_out->genome_bits_required = _c.genome_bits_required; \
        return 1; \
    } \
    \
    static int _flow_mock_exec_##drv_sym(const FlowPrimitiveContext *ctx, FlowPrimitiveResult *res_out) { \
        if (!res_out) return -1; \
        FlowMockDriverConfig _c = _flow_mock_cfg_##drv_sym(); \
        if (_c.custom_execute) { \
            return _c.custom_execute(ctx, res_out); \
        } \
        res_out->status_code = 0; \
        res_out->bytes_transferred = ctx ? ctx->data_len : 0; \
        res_out->latency_cycles = _c.simulated_latency_cycles; \
        res_out->zero_copy_active = _c.zero_copy; \
        return 0; \
    } \
    \
    static const FlowPrimitiveDriver drv_sym = { \
        .driver_name = #drv_sym, \
        .driver_version = "v1.0-mock", \
        .register_primitive = _flow_mock_reg_##drv_sym, \
        .get_hardware_bounds = _flow_mock_bounds_##drv_sym, \
        .execute_primitive = _flow_mock_exec_##drv_sym \
    }

#ifdef __cplusplus
}
#endif

#endif /* FLOW_MOCK_DRIVER_H */
