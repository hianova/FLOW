#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "numa_affinity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
#if defined(__APPLE__)
static int sysctl_u32(const char *name, uint32_t *out) { int v = 0; size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, NULL, 0) == 0 && v > 0) { *out = (uint32_t)v; return 1; } return 0; } static void sysctl_sz(const char *name, size_t *out) {
    size_t v = 0, len = sizeof(v); if (sysctlbyname(name, &v, &len, NULL, 0) == 0 && v > 0) *out = v; }
#endif
int flow_numa_topology_discover(FlowHardwareTopology *topo) { if (!topo) return 0; memset(topo, 0, sizeof(*topo));
    topo->cache_line_size = 64; topo->l1_cache_size = 32768; topo->l2_cache_size = 4194304;
#if defined(__APPLE__)
    if (!sysctl_u32("hw.ncpu", &topo->total_logical_cores)) topo->total_logical_cores = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    if (!sysctl_u32("hw.physicalcpu", &topo->total_physical_cores)) topo->total_physical_cores = topo->total_logical_cores;
    if (!sysctl_u32("hw.perflevel0.physicalcpu", &topo->performance_cores)) topo->performance_cores = topo->total_physical_cores;
    if (!sysctl_u32("hw.perflevel1.physicalcpu", &topo->efficiency_cores))
        topo->efficiency_cores = (topo->total_physical_cores > topo->performance_cores) ? (topo->total_physical_cores - topo->performance_cores) : 0;
    sysctl_sz("hw.cachelinesize", &topo->cache_line_size); sysctl_sz("hw.l1dcachesize", &topo->l1_cache_size); sysctl_sz("hw.l2cachesize", &topo->l2_cache_size);
    topo->numa_node_count = 1; topo->nodes[0].node_id = 0; topo->nodes[0].core_count = topo->total_logical_cores;
    for (uint32_t i = 0; i < topo->total_logical_cores && i < FLOW_MAX_PHYSICAL_CORES; ++i) topo->nodes[0].core_ids[i] = i; topo->is_numa_available = true;
#elif defined(__linux__)
    long cpus = sysconf(_SC_NPROCESSORS_ONLN); topo->total_logical_cores = cpus > 0 ? (uint32_t)cpus : 4;
    topo->total_physical_cores = topo->performance_cores = topo->total_logical_cores; uint32_t node_count = 0; for (uint32_t n = 0; n < FLOW_MAX_NUMA_NODES; ++n) {
        char path[128]; snprintf(path, sizeof(path), "/sys/devices/system/node/node%u", n);
        if (access(path, F_OK) == 0) { topo->nodes[n].node_id = n; topo->nodes[n].core_count = 0; node_count++; } } if (!node_count) {
        node_count = 1; topo->nodes[0].node_id = 0; topo->nodes[0].core_count = topo->total_logical_cores;
        for (uint32_t i = 0; i < topo->total_logical_cores && i < FLOW_MAX_PHYSICAL_CORES; ++i) topo->nodes[0].core_ids[i] = i; }
    topo->numa_node_count = node_count; topo->is_numa_available = (node_count > 1);
#else
    topo->total_logical_cores = topo->total_physical_cores = topo->performance_cores = 4; topo->numa_node_count = 1; topo->nodes[0].core_count = 4;
#endif
    return 1; } int flow_numa_pin_thread(uint32_t core_id) {
#if defined(__APPLE__)
    (void)core_id; return (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) ? 1 : 0;
#elif defined(__linux__)
    cpu_set_t cpuset; CPU_ZERO(&cpuset); long ncpus = sysconf(_SC_NPROCESSORS_ONLN); if (ncpus <= 0) ncpus = 1; CPU_SET(core_id % (uint32_t)ncpus, &cpuset);
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0) ? 1 : 0;
#else
    (void)core_id; return 1;
#endif
} int flow_numa_unpin_thread(void) {
#if defined(__APPLE__)
    return (pthread_set_qos_class_self_np(QOS_CLASS_DEFAULT, 0) == 0) ? 1 : 0;
#elif defined(__linux__)
    cpu_set_t cpuset; CPU_ZERO(&cpuset); long ncpus = sysconf(_SC_NPROCESSORS_ONLN); if (ncpus <= 0) ncpus = 1; for (long i = 0; i < ncpus; ++i) CPU_SET(i, &cpuset);
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0) ? 1 : 0;
#else
    return 1;
#endif
} void *flow_numa_alloc_local(size_t bytes) { if (!bytes) return NULL; size_t page_size = 4096, aligned_bytes = (bytes + page_size - 1) & ~(page_size - 1);
    void *ptr = mmap(NULL, aligned_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); if (ptr == MAP_FAILED) return NULL;
    volatile uint8_t *touch = (volatile uint8_t *)ptr; for (size_t offset = 0; offset < aligned_bytes; offset += page_size) touch[offset] = 0; return ptr; }
void flow_numa_free_local(void *ptr, size_t bytes) { if (!ptr || !bytes) return; size_t page_size = 4096, aligned_bytes = (bytes + page_size - 1) & ~(page_size - 1);
    munmap(ptr, aligned_bytes); }
