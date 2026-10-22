#include "primitive.h"
#include <string.h>
void flow_primitive_registry_init(FlowPrimitiveRegistry *reg) { if (reg) memset(reg, 0, sizeof(*reg)); }
int flow_primitive_register(FlowPrimitiveRegistry *reg, const FlowPrimitiveDriver *driver) { if (!reg || !driver || reg->count >= FLOW_MAX_PRIMITIVE_DRIVERS) return 0;
    if (driver->register_primitive && !driver->register_primitive()) return 0; reg->drivers[reg->count++] = driver; return 1; }
const FlowPrimitiveDriver *flow_primitive_lookup(const FlowPrimitiveRegistry *reg, const char *name) { if (!reg || !name) return NULL;
    for (size_t i = 0; i < reg->count; ++i) if (reg->drivers[i] && !strcmp(reg->drivers[i]->driver_name, name)) return reg->drivers[i]; return NULL; }
size_t flow_primitive_count(const FlowPrimitiveRegistry *reg) { return reg ? reg->count : 0; }
