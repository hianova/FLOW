#ifndef FLOW_BUS_HYBRID_POLL_H
#define FLOW_BUS_HYBRID_POLL_H

#include "primitive.h"
#include "moreau_hysteresis.h"
#include "hardware_telemetry.h"
#include "smt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW Heterogeneous Accelerator & I/O Bus Hybrid Phase Transition Engine
 * (bus_hybrid_poll.h)
 * ============================================================================
 * Replaces empirical sleep heuristics, static busy-spinning, and hardcoded
 * timeouts in heterogeneous accelerators (GPU, NPU, TPU, FPGA, CXL) and
 * high-speed I/O queues (io_uring, RDMA, NVMe) with:
 * 
 * 1. Moreau Sweeping Process Geometric Normal Cone Hysteresis:
 *    Prevents thrashing between Interrupt (WFI/Eventfd) and Busy-Poll (Spin)
 *    within convex normal cone C = [q_exit, q_enter].
 * 
 * 2. Online Convex Optimization (OCO) with Thermodynamic Dual Multipliers:
 *    Dynamically tunes optimal spin budget tau* balancing P99 latency and
 *    microjoules (uJ) from physical telemetry probes (flow_hardware_energy_uj).
 * 
 * 3. Lock-Free Heterogeneous Accelerator Command Ring & Completion Wait:
 *    Guarantees 0-ns fast path completion and Lost-Wakeup Freedom.
 * 
 * 4. SMT Formal Verification:
 *    Proves Lost-Wakeup Freedom and Bounded Completion Latency Theorems.
 * ============================================================================
 */

#define FLOW_BUS_RING_CAPACITY 256
#define FLOW_BUS_DEFAULT_MIN_BUDGET_NS 100ULL
#define FLOW_BUS_DEFAULT_MAX_BUDGET_NS 1000000ULL /* 1 ms */

typedef enum {
    FLOW_BUS_MODE_INTERRUPT = 0, /* Low-power interrupt wait (WFI / sleep) */
    FLOW_BUS_MODE_BUSY_POLL = 1  /* Zero-overhead register/memory spin-polling */
} FlowBusMode;

/* Heterogeneous Accelerator Command Packet */
typedef struct {
    uint64_t command_id;
    uint32_t opcode;
    uint32_t queue_id;
    uint64_t dma_addr;
    size_t data_length;
    uint64_t submit_cycles;
} FlowBusCommand;

/* Heterogeneous Accelerator Completion Event */
typedef struct {
    uint64_t command_id;
    int status;
    uint64_t completion_cycles;
    double latency_ns;
    bool completed;
} FlowBusCompletion;

/* Hybrid Bus Engine Governor */
typedef struct {
    char bus_name[64];
    FlowBusMode current_mode;

    /* 1. Moreau Sweeping Process for Non-Smooth Phase Transition */
    FlowMoreauHysteresis moreau_hysteresis;
    double q_exit_threshold;       /* Normal cone lower bound (e.g. 4.0) */
    double q_enter_threshold;      /* Normal cone upper bound (e.g. 16.0) */

    /* 2. Online Convex Optimization (OCO) for Thermodynamic Polling Budget */
    double polling_budget_ns;      /* tau*: optimal spin duration in nanoseconds */
    double min_budget_ns;
    double max_budget_ns;
    double shadow_price_lambda;    /* Thermodynamic dissipation shadow price */
    double learning_rate_eta;      /* Subgradient step size eta */
    double weight_latency;         /* alpha in loss L */
    double weight_energy;          /* beta in loss L */

    /* 3. Physical Hardware Telemetry */
    FlowPhysicalProbe last_probe;
    uint64_t total_spin_cycles;
    uint64_t total_interrupt_waits;
    double total_energy_uj;

    /* 4. Lock-Free Command & Completion Ring Buffer */
    FlowBusCommand sq_ring[FLOW_BUS_RING_CAPACITY];
    FlowBusCompletion cq_ring[FLOW_BUS_RING_CAPACITY];
    _Atomic uint64_t sq_head;
    _Atomic uint64_t sq_tail;
    _Atomic uint64_t cq_head;
    _Atomic uint64_t cq_tail;

    /* 5. Metrics & Verification Counters */
    uint64_t total_submitted;
    uint64_t total_completed;
    uint64_t flutters_suppressed;
    uint64_t lost_wakeups_prevented;
} FlowBusHybridPoll;

/* Initialize Hybrid Bus Engine */
int flow_bus_hybrid_init(FlowBusHybridPoll *bus,
                         const char *name,
                         double q_exit,
                         double q_enter,
                         double initial_budget_ns);

/* Submit Command to Accelerator Ring Buffer */
int flow_bus_hybrid_submit(FlowBusHybridPoll *bus, const FlowBusCommand *cmd);

/* Mark Command Complete (called by Accelerator ISR or DMA engine) */
int flow_bus_hybrid_complete(FlowBusHybridPoll *bus, uint64_t command_id, int status);

/* Hybrid Wait for Command Completion */
int flow_bus_hybrid_wait(FlowBusHybridPoll *bus,
                         uint64_t command_id,
                         FlowBusCompletion *comp_out,
                         uint64_t timeout_ns);

/* Evaluate Moreau Sweeping Process Phase Transition on Queue Depth */
FlowBusMode flow_bus_hybrid_evaluate_phase(FlowBusHybridPoll *bus, double current_queue_depth);

/* Execute One OCO Subgradient Step to Update Polling Budget tau* */
int flow_bus_hybrid_step_oco(FlowBusHybridPoll *bus, double observed_latency_ns, double observed_energy_uj);

/* SMT Formal Verification: Lost-Wakeup Freedom & Bounded Latency Invariant */
FlowSMTResult flow_bus_hybrid_verify_smt(const FlowBusHybridPoll *bus, FlowSMTProofAttestation *proof_out);

/* Standard Primitive Driver Singleton for Heterogeneous Accelerator */
const FlowPrimitiveDriver *flow_primitive_accelerator_driver(void);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_BUS_HYBRID_POLL_H */
