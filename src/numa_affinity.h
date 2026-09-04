#ifndef FLOW_NUMA_AFFINITY_H
#define FLOW_NUMA_AFFINITY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW NUMA & Core Pinning Subsystem (numa_affinity.h)
 * ============================================================================
 * Eliminates cross-socket interconnect penalties (QPI/UPI/Infinity Fabric)
 * and cache line bouncing through:
 * 1. Physical CPU and NUMA topology discovery.
 * 2. Hardware thread-to-core pinning (Linux sched_setaffinity / macOS QoS P-Core).
 * 3. Page-aligned, NUMA-local arena allocation with first-touch guarantee.
 * ============================================================================
 */

#define FLOW_MAX_NUMA_NODES 8
#define FLOW_MAX_PHYSICAL_CORES 256

typedef struct {
    uint32_t node_id;
    uint32_t core_count;
    uint32_t core_ids[FLOW_MAX_PHYSICAL_CORES];
    size_t local_memory_bytes;
} FlowNumaNode;

typedef struct {
    uint32_t total_logical_cores;
    uint32_t total_physical_cores;
    uint32_t performance_cores; /* Apple Silicon P-cores or base physical */
    uint32_t efficiency_cores;  /* Apple Silicon E-cores or SMT siblings */
    uint32_t numa_node_count;
    FlowNumaNode nodes[FLOW_MAX_NUMA_NODES];
    size_t cache_line_size;
    size_t l1_cache_size;
    size_t l2_cache_size;
    bool is_numa_available;
} FlowHardwareTopology;

typedef FlowHardwareTopology FlowNumaTopology;

/* Discover physical hardware topology */
int flow_numa_topology_discover(FlowHardwareTopology *topo);

/* Pin the calling thread to a specific physical core / P-Core QoS class */
int flow_numa_pin_thread(uint32_t core_id);

/* Unpin the calling thread (restore default scheduler policy) */
int flow_numa_unpin_thread(void);

/* Allocate page-aligned, NUMA-local memory with first-touch guarantee */
void *flow_numa_alloc_local(size_t bytes);

/* Free NUMA-local allocated memory */
void flow_numa_free_local(void *ptr, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_NUMA_AFFINITY_H */
