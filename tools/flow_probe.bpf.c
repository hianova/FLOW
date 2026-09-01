/*
 * FLOW eBPF Silicon-Grade Telemetry Kernel Probe (flow_probe.bpf.c)
 * Attaches to hardware PMU perf_events and filters by JIT IP ranges.
 */

#ifndef __KERNEL__
#define __KERNEL__
#endif

typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;

#define BPF_MAP_TYPE_RINGBUF 27
#define BPF_MAP_TYPE_ARRAY 2
#define FLOW_MAX_IP_RANGES 16

struct flow_ip_range_entry {
    __u64 start_ip;
    __u64 end_ip;
    __u32 candidate_index;
    __u32 pad;
};

struct flow_pmu_event {
    __u64 timestamp_ns;
    __u64 ip;
    __u64 l3_cache_misses;
    __u64 l3_cache_references;
    __u64 instructions;
    __u64 cpu_cycles;
    __u32 candidate_index;
    __u32 pid;
};

/* In real compilation with clang -target bpf, <linux/bpf.h> & BPF helpers are used.
 * Here we provide the complete self-contained specification and reference structures. */
