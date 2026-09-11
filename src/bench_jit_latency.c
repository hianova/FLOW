#include "jit.h"
#include "flow_speculative_jit.h"
#include "reload.h"
#include "flow_jet.h"
#include "bitspace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

static inline uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#define WARMUP  200
#define SAMPLES 2000

static uint64_t arr[SAMPLES];

static int cmp64(const void *a, const void *b) {
    uint64_t x = *(uint64_t*)a, y = *(uint64_t*)b;
    return (x > y) - (x < y);
}
static void report(const char *tag) {
    qsort(arr, SAMPLES, sizeof(uint64_t), cmp64);
    printf("%s\n    min=%.0f  p50=%.0f  p95=%.0f  p99=%.0f  max=%.0f  ns\n\n",
           tag,
           (double)arr[0],
           (double)arr[SAMPLES/2],
           (double)arr[(int)(SAMPLES*0.95)],
           (double)arr[(int)(SAMPLES*0.99)],
           (double)arr[SAMPLES-1]);
}

int main(void) {
    printf("============================================================\n");
    printf("  FLOW JIT 延遲實測  (%d samples, warmup=%d)\n", SAMPLES, WARMUP);
    printf("  Platform: %s\n",
#if defined(__aarch64__)
        "AArch64 (Apple Silicon)"
#elif defined(__x86_64__)
        "x86_64"
#else
        "unknown"
#endif
    );
    printf("============================================================\n\n");

    /* ---- F: 裸 mutex ---- */
    {
        pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
        for (int i = 0; i < WARMUP; i++) { pthread_mutex_lock(&mu); pthread_mutex_unlock(&mu); }
        for (int i = 0; i < SAMPLES; i++) {
            uint64_t t0 = ns_now(); pthread_mutex_lock(&mu); pthread_mutex_unlock(&mu);
            arr[i] = ns_now() - t0;
        }
        pthread_mutex_destroy(&mu);
        report("[F] pthread_mutex lock+unlock  (所有 submit 的下界)");
    }

    /* ---- A: async_jit_submit — 主線程 enqueue ---- */
    {
        FlowAsyncJITConfig cfg = {.worker_threads = 1};
        FlowAsyncJITPool *pool = flow_async_jit_create(&cfg);
        const char *ir = "define double @f(double %a, double %b) { %r = fmul double %a, %b  ret double %r }";
        for (int i = 0; i < WARMUP; i++)
            flow_async_jit_submit(pool, ir, "wm", FLOW_LAYOUT_AOS, 0);
        flow_async_jit_wait_idle(pool);
        for (int i = 0; i < SAMPLES; i++) {
            uint64_t t0 = ns_now();
            flow_async_jit_submit(pool, ir, "unit", FLOW_LAYOUT_AOS, 0);
            arr[i] = ns_now() - t0;
        }
        flow_async_jit_wait_idle(pool);
        flow_async_jit_destroy(pool);
        report("[A] flow_async_jit_submit()  (主線程 enqueue + strncpy 2048B)");
    }

    /* ---- B2: 重用 engine 的同步 compile ---- */
    {
        const char *ir = "define double @f(double %a, double %b) { %r = fmul double %a, %b  ret double %r }";
        FlowJITEngine *engine = flow_jit_create(NULL);
        size_t base_used = 0; /* 記住初始偏移 */
        FlowUnit unit; FlowJITCodeBlock block;
        for (int i = 0; i < WARMUP; i++) {
            flow_jit_compile_llvm_ir(engine, ir, "wm", FLOW_LAYOUT_AOS, &unit, &block);
        }
        base_used = 0; /* 只是為了量 compile 本身，不 reset heap（heap 只增不減 OK） */
        for (int i = 0; i < SAMPLES; i++) {
            uint64_t t0 = ns_now();
            flow_jit_compile_llvm_ir(engine, ir, "unit", FLOW_LAYOUT_AOS, &unit, &block);
            arr[i] = ns_now() - t0;
        }
        (void)base_used;
        flow_jit_destroy(engine);
        report("[B2] flow_jit_compile_llvm_ir()  (重用 engine, heap bump 遞增)");
    }

    /* ---- C: reload_activate QSBR pointer swap ---- */
    {
        FlowReloadContext *ctx = flow_reload_create(NULL);
        static FlowUnit ua, ub;
        memset(&ua,0,sizeof ua); memset(&ub,0,sizeof ub);
        ua.abi_version = ub.abi_version = FLOW_RELOAD_ABI_VERSION;
        ua.name = "a"; ub.name = "b";
        for (int i = 0; i < WARMUP; i++)
            flow_reload_activate(ctx, (i&1)?&ub:&ua);
        for (int i = 0; i < SAMPLES; i++) {
            uint64_t t0 = ns_now();
            flow_reload_activate(ctx, (i&1)?&ub:&ua);
            arr[i] = ns_now() - t0;
        }
        flow_reload_destroy(ctx);
        report("[C] flow_reload_activate()  (QSBR pointer swap)");
    }

    /* ---- D: speculative_jit_evaluate 完整熱路徑（觸發 dispatch 那一次） ---- */
    {
        FlowAsyncJITConfig cfg = {.worker_threads = 1};
        FlowAsyncJITPool *pool = flow_async_jit_create(&cfg);
        FlowReloadContext *ctx  = flow_reload_create(NULL);
        FlowJet jet;
        flow_jet_init(&jet, "bj", "Benchmark Jet");
        FlowSpeculativeJIT sjit;
        flow_speculative_jit_init(&sjit, &jet, pool, ctx, 5000.0, 1.2, 0);

        for (int i = 0; i < WARMUP; i++) {
            sjit.is_compilation_dispatched = 0;
            sjit.is_compilation_ready = 0;
            sjit.is_hot_swapped = 0;
            jet.payload.q[0] = 0.8; jet.payload.p[0] = 0.5;
            flow_speculative_jit_evaluate(&sjit, 0.0);
        }
        flow_async_jit_wait_idle(pool);

        for (int i = 0; i < SAMPLES; i++) {
            sjit.is_compilation_dispatched = 0;
            sjit.is_compilation_ready = 0;
            sjit.is_hot_swapped = 0;
            jet.payload.q[0] = 0.8; jet.payload.p[0] = 0.5;
            uint64_t t0 = ns_now();
            flow_speculative_jit_evaluate(&sjit, 0.0);
            arr[i] = ns_now() - t0;
        }
        flow_async_jit_wait_idle(pool);
        flow_async_jit_destroy(pool);
        flow_reload_destroy(ctx);
        report("[D] flow_speculative_jit_evaluate()  (完整熱路徑含 Koopman + submit)");
    }

    /* ---- E: 對照 — atomic canvas store ---- */
    {
        static _Atomic uint64_t hard_mask, soft_bias;
        atomic_init(&hard_mask, ~0ULL); atomic_init(&soft_bias, 0ULL);
        for (int i = 0; i < WARMUP; i++) {
            atomic_store_explicit(&hard_mask, (uint64_t)i,   memory_order_release);
            atomic_store_explicit(&soft_bias, (uint64_t)i*2, memory_order_release);
        }
        for (int i = 0; i < SAMPLES; i++) {
            uint64_t mask = 0xAAAAAAAAAAAAAAAAULL ^ (uint64_t)i;
            uint64_t t0 = ns_now();
            atomic_store_explicit(&hard_mask, mask,      memory_order_release);
            atomic_store_explicit(&soft_bias, mask >> 1, memory_order_release);
            arr[i] = ns_now() - t0;
        }
        report("[E] atomic_store x2 canvas mask  (Hardwired Template 替代方案)");
    }

    printf("============================================================\n");
    printf("  解讀：\n");
    printf("  A vs F：submit 相對裸 mutex 的額外 strncpy 成本\n");
    printf("  B/B2  ：icache flush 實際代價（背景 worker 承擔，主線程無感）\n");
    printf("  C     ：主線程 commit_swap 的真實代價\n");
    printf("  D     ：完整 speculative hot-path（Koopman + A）主線程視角\n");
    printf("  E     ：替代方案的真實代價（終態）\n");
    printf("============================================================\n");
    return 0;
}
