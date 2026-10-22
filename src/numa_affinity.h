#ifndef FLOW_NUMA_AFFINITY_H
#define FLOW_NUMA_AFFINITY_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define FLOW_MAX_NUMA_NODES 8
#define FLOW_MAX_PHYSICAL_CORES 256
typedef struct { uint32_t node_id, core_count, core_ids[FLOW_MAX_PHYSICAL_CORES]; size_t local_memory_bytes; } FlowNumaNode;
typedef struct { uint32_t total_logical_cores, total_physical_cores, performance_cores, efficiency_cores, numa_node_count; FlowNumaNode nodes[FLOW_MAX_NUMA_NODES];
    size_t cache_line_size, l1_cache_size, l2_cache_size; bool is_numa_available; } FlowHardwareTopology;
typedef FlowHardwareTopology FlowNumaTopology;
int flow_numa_topology_discover(FlowHardwareTopology *topo); int flow_numa_pin_thread(uint32_t core_id);
void *flow_numa_alloc_local(size_t bytes); void flow_numa_free_local(void *ptr, size_t bytes);
#endif /* FLOW_NUMA_AFFINITY_H */
