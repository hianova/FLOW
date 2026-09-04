#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "numa_affinity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#if defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <pthread.h>
#include <pthread/qos.h>
#elif defined(__linux__)
#include <sched.h>
#include <pthread.h>
#endif

int flow_numa_topology_discover(FlowHardwareTopology *topo) {
    if (!topo) return 0;
    memset(topo, 0, sizeof(*topo));

    topo->cache_line_size = 64;
    topo->l1_cache_size = 32768;
    topo->l2_cache_size = 4194304;

#if defined(__APPLE__)
    int ncpu = 0;
    size_t len = sizeof(ncpu);
    if (sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0) == 0 && ncpu > 0) {
        topo->total_logical_cores = (uint32_t)ncpu;
    } else {
        topo->total_logical_cores = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    }

    int physical_cpu = 0;
    len = sizeof(physical_cpu);
    if (sysctlbyname("hw.physicalcpu", &physical_cpu, &len, NULL, 0) == 0 && physical_cpu > 0) {
        topo->total_physical_cores = (uint32_t)physical_cpu;
    } else {
        topo->total_physical_cores = topo->total_logical_cores;
    }

    int p_cores = 0;
    len = sizeof(p_cores);
    if (sysctlbyname("hw.perflevel0.physicalcpu", &p_cores, &len, NULL, 0) == 0 && p_cores > 0) {
        topo->performance_cores = (uint32_t)p_cores;
    } else {
        topo->performance_cores = topo->total_physical_cores;
    }

    int e_cores = 0;
    len = sizeof(e_cores);
    if (sysctlbyname("hw.perflevel1.physicalcpu", &e_cores, &len, NULL, 0) == 0 && e_cores > 0) {
        topo->efficiency_cores = (uint32_t)e_cores;
    } else {
        topo->efficiency_cores = (topo->total_physical_cores > topo->performance_cores) ?
                                  (topo->total_physical_cores - topo->performance_cores) : 0;
    }

    size_t cache_line = 0;
    len = sizeof(cache_line);
    if (sysctlbyname("hw.cachelinesize", &cache_line, &len, NULL, 0) == 0 && cache_line > 0) {
        topo->cache_line_size = cache_line;
    }

    size_t l1 = 0;
    len = sizeof(l1);
    if (sysctlbyname("hw.l1dcachesize", &l1, &len, NULL, 0) == 0 && l1 > 0) {
        topo->l1_cache_size = l1;
    }

    size_t l2 = 0;
    len = sizeof(l2);
    if (sysctlbyname("hw.l2cachesize", &l2, &len, NULL, 0) == 0 && l2 > 0) {
        topo->l2_cache_size = l2;
    }

    /* Apple Silicon is Unified Memory Architecture (UMA) -> 1 coherent high-speed NUMA/UMA node */
    topo->numa_node_count = 1;
    topo->nodes[0].node_id = 0;
    topo->nodes[0].core_count = topo->total_logical_cores;
    for (uint32_t i = 0; i < topo->total_logical_cores && i < FLOW_MAX_PHYSICAL_CORES; ++i) {
        topo->nodes[0].core_ids[i] = i;
    }
    topo->is_numa_available = true;

#elif defined(__linux__)
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    topo->total_logical_cores = cpus > 0 ? (uint32_t)cpus : 4;
    topo->total_physical_cores = topo->total_logical_cores;
    topo->performance_cores = topo->total_logical_cores;
    topo->efficiency_cores = 0;

    /* Detect NUMA nodes via sysfs */
    uint32_t node_count = 0;
    for (uint32_t n = 0; n < FLOW_MAX_NUMA_NODES; ++n) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/node/node%u", n);
        if (access(path, F_OK) == 0) {
            topo->nodes[n].node_id = n;
            topo->nodes[n].core_count = 0;
            node_count++;
        }
    }
    if (node_count == 0) {
        node_count = 1;
        topo->nodes[0].node_id = 0;
        topo->nodes[0].core_count = topo->total_logical_cores;
        for (uint32_t i = 0; i < topo->total_logical_cores && i < FLOW_MAX_PHYSICAL_CORES; ++i) {
            topo->nodes[0].core_ids[i] = i;
        }
    }
    topo->numa_node_count = node_count;
    topo->is_numa_available = (node_count > 1);
#else
    topo->total_logical_cores = 4;
    topo->total_physical_cores = 4;
    topo->performance_cores = 4;
    topo->numa_node_count = 1;
    topo->nodes[0].core_count = 4;
#endif

    return 1;
}

int flow_numa_pin_thread(uint32_t core_id) {
#if defined(__APPLE__)
    (void)core_id;
    /* On Apple Silicon, user-interactive QoS binds threads to the maximum bandwidth P-cores */
    return (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) ? 1 : 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus <= 0) ncpus = 1;
    CPU_SET(core_id % (uint32_t)ncpus, &cpuset);
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0) ? 1 : 0;
#else
    (void)core_id;
    return 1;
#endif
}

int flow_numa_unpin_thread(void) {
#if defined(__APPLE__)
    return (pthread_set_qos_class_self_np(QOS_CLASS_DEFAULT, 0) == 0) ? 1 : 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus <= 0) ncpus = 1;
    for (long i = 0; i < ncpus; ++i) {
        CPU_SET(i, &cpuset);
    }
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0) ? 1 : 0;
#else
    return 1;
#endif
}

void *flow_numa_alloc_local(size_t bytes) {
    if (bytes == 0) return NULL;
    size_t page_size = 4096;
    size_t aligned_bytes = (bytes + page_size - 1) & ~(page_size - 1);

    void *ptr = mmap(NULL, aligned_bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return NULL;

    /*
     * NUMA First-Touch Invariant:
     * Physically touching every 4KB page in the calling thread forces the OS
     * page fault handler to allocate physical memory frames on the local NUMA node!
     */
    volatile uint8_t *touch = (volatile uint8_t *)ptr;
    for (size_t offset = 0; offset < aligned_bytes; offset += page_size) {
        touch[offset] = 0;
    }
    return ptr;
}

void flow_numa_free_local(void *ptr, size_t bytes) {
    if (!ptr || bytes == 0) return;
    size_t page_size = 4096;
    size_t aligned_bytes = (bytes + page_size - 1) & ~(page_size - 1);
    munmap(ptr, aligned_bytes);
}
