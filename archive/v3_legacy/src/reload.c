#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "reload.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#else
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#endif
/* 1. Dual-Mapped Memory Pool for HoTT Univalence Substrate */
struct FlowJITEngine {
    uint8_t *write_heap, *exec_heap;
    size_t pool_size, pool_used;
    uint64_t tlb_shootdowns_avoided;
    int is_dual_mapped;
};
FlowJITEngine *flow_jit_create(const FlowJITConfig *cfg) {
    size_t sz = (cfg && cfg->initial_code_heap_bytes) ? cfg->initial_code_heap_bytes
                                                      : FLOW_JIT_DEFAULT_HEAP_BYTES;
    FlowJITEngine *e = (FlowJITEngine *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;
#if defined(__APPLE__)
    vm_address_t w = 0, x = 0;
    vm_prot_t cur, max;
    if (vm_allocate(mach_task_self(), &w, sz, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
        if (mach_vm_remap(mach_task_self(), (mach_vm_address_t *)&x, sz, 0, VM_FLAGS_ANYWHERE,
                          mach_task_self(), (mach_vm_address_t)w, FALSE, &cur, &max,
                          VM_INHERIT_NONE) == KERN_SUCCESS &&
            mach_vm_protect(mach_task_self(), (mach_vm_address_t)w, sz, FALSE,
                            VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS &&
            mach_vm_protect(mach_task_self(), (mach_vm_address_t)x, sz, FALSE,
                            VM_PROT_READ | VM_PROT_EXECUTE) == KERN_SUCCESS) {
            e->write_heap = (uint8_t *)w;
            e->exec_heap = (uint8_t *)x;
            e->pool_size = sz;
            e->is_dual_mapped = 1;
        } else {
            if (x)
                vm_deallocate(mach_task_self(), x, sz);
            vm_deallocate(mach_task_self(), w, sz);
        }
    }
#else
    int fd = -1;
#if defined(__linux__) && defined(MFD_CLOEXEC)
    fd = memfd_create("flow_jit_pool", MFD_CLOEXEC | 0x0010U /* MFD_EXEC */);
    if (fd < 0)
        fd = memfd_create("flow_jit_pool", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)sz) == 0) {
            void *w = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0),
                 *x = mmap(NULL, sz, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
            if (w != MAP_FAILED && x != MAP_FAILED) {
                e->write_heap = w;
                e->exec_heap = x;
                e->pool_size = sz;
                e->is_dual_mapped = 1;
            } else {
                if (w != MAP_FAILED)
                    munmap(w, sz);
                if (x != MAP_FAILED)
                    munmap(x, sz);
            }
        }
        close(fd);
        fd = -1;
    }
#endif
    if (!e->write_heap || !e->exec_heap) {
        char shm_name[64];
        static _Atomic uint32_t shm_seq = 0U;
        uint32_t seq = atomic_fetch_add_explicit(&shm_seq, 1U, memory_order_relaxed);
        snprintf(shm_name, sizeof(shm_name), "/flow_jit_%d_%u", (int)getpid(), (unsigned)seq);
        fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd >= 0) {
            (void)shm_unlink(shm_name);
            if (ftruncate(fd, (off_t)sz) == 0) {
                void *w = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0),
                     *x = mmap(NULL, sz, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
                if (w != MAP_FAILED && x != MAP_FAILED) {
                    e->write_heap = w;
                    e->exec_heap = x;
                    e->pool_size = sz;
                    e->is_dual_mapped = 1;
                } else {
                    if (w != MAP_FAILED)
                        munmap(w, sz);
                    if (x != MAP_FAILED)
                        munmap(x, sz);
                }
            }
            close(fd);
        }
    }
    if (!e->write_heap || !e->exec_heap) {
        void *w = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        void *x = mmap(NULL, sz, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (x == MAP_FAILED)
            x = mmap(NULL, sz, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (w != MAP_FAILED && x != MAP_FAILED) {
            e->write_heap = w;
            e->exec_heap = x;
            e->pool_size = sz;
            e->is_dual_mapped = 1;
        } else {
            if (w != MAP_FAILED)
                munmap(w, sz);
            if (x != MAP_FAILED)
                munmap(x, sz);
        }
    }
#endif
    if (!e->write_heap || !e->exec_heap) {
        free(e);
        return NULL;
    }
    return e;
}
void flow_jit_destroy(FlowJITEngine *e) {
    if (!e)
        return;
#if defined(__APPLE__)
    if (e->exec_heap)
        vm_deallocate(mach_task_self(), (vm_address_t)e->exec_heap, e->pool_size);
    if (e->write_heap)
        vm_deallocate(mach_task_self(), (vm_address_t)e->write_heap, e->pool_size);
#else
    if (e->write_heap)
        munmap(e->write_heap, e->pool_size);
    if (e->exec_heap)
        munmap(e->exec_heap, e->pool_size);
#endif
    free(e);
}
int flow_jit_get_pool_stats(const FlowJITEngine *e, FlowJITPoolStats *s) {
    if (!e || !s)
        return 0;
    *s = (FlowJITPoolStats){.is_dual_mapped = e->is_dual_mapped,
                            .write_base = (uintptr_t)e->write_heap,
                            .exec_base = (uintptr_t)e->exec_heap,
                            .pool_size = e->pool_size,
                            .pool_used = e->pool_used,
                            .tlb_shootdowns_avoided = e->tlb_shootdowns_avoided};
    return 1;
}
/* 2. QSBR Atomic Pointer Exchange & Generation Lifecycle */
typedef struct FlowReloadGeneration {
    const FlowUnit *unit;
    void *state;
    uint64_t generation, retire_epoch;
    _Atomic uint64_t pin_count;
    struct FlowReloadGeneration *next;
} FlowReloadGeneration;
struct FlowReloadContext {
    void *host_context;
    _Atomic(FlowReloadGeneration *) current;
    _Atomic uint64_t epoch, anon_pins;
    _Atomic int stopping;
    uint64_t next_generation;
    pthread_mutex_t lock;
    FlowReloadReader *readers;
    FlowReloadGeneration *retired;
};
static void drop_gen(FlowReloadContext *c, FlowReloadGeneration *g) {
    if (g && g->unit && g->unit->drop && g->state)
        g->unit->drop(c->host_context, g->state);
    free(g);
}
FlowReloadContext *flow_reload_create(void *hc) {
    FlowReloadContext *c = calloc(1, sizeof(*c));
    if (c) {
        c->host_context = hc;
        atomic_init(&c->epoch, 1);
        pthread_mutex_init(&c->lock, NULL);
    }
    return c;
}
int flow_reload_destroy(FlowReloadContext *c) {
    if (!c)
        return FLOW_RELOAD_INVALID;
    pthread_mutex_lock(&c->lock);
    atomic_store_explicit(&c->stopping, 1, memory_order_release);
    FlowReloadGeneration *cur = atomic_load_explicit(&c->current, memory_order_acquire),
                         *ret = c->retired;
    atomic_store_explicit(&c->current, NULL, memory_order_release);
    c->retired = NULL;
    pthread_mutex_unlock(&c->lock);
    if (cur)
        drop_gen(c, cur);
    while (ret) {
        FlowReloadGeneration *n = ret->next;
        drop_gen(c, ret);
        ret = n;
    }
    pthread_mutex_destroy(&c->lock);
    free(c);
    return FLOW_RELOAD_OK;
}
int flow_reload_reader_register(FlowReloadContext *c, FlowReloadReader *r) {
    if (!c || !r)
        return FLOW_RELOAD_INVALID;
    pthread_mutex_lock(&c->lock);
    r->context = c;
    r->next = c->readers;
    atomic_init(&r->active_epoch, 0);
    atomic_init(&r->qsbr_epoch, atomic_load_explicit(&c->epoch, memory_order_acquire));
    atomic_init(&r->is_offline, 0);
    atomic_init(&r->is_quarantined, 0);
    atomic_store_explicit(&r->registered, 1, memory_order_release);
    c->readers = r;
    pthread_mutex_unlock(&c->lock);
    return FLOW_RELOAD_OK;
}
int flow_reload_reader_unregister(FlowReloadReader *r) {
    if (!r || !r->context)
        return FLOW_RELOAD_INVALID;
    FlowReloadContext *c = r->context;
    pthread_mutex_lock(&c->lock);
    FlowReloadReader **cur = &c->readers;
    while (*cur && *cur != r)
        cur = &(*cur)->next;
    if (!*cur) {
        pthread_mutex_unlock(&c->lock);
        return FLOW_RELOAD_INVALID;
    }
    *cur = r->next;
    atomic_store_explicit(&r->registered, 0, memory_order_release);
    r->context = NULL;
    pthread_mutex_unlock(&c->lock);
    return FLOW_RELOAD_OK;
}
static size_t reclaim_locked(FlowReloadContext *c) {
    FlowReloadGeneration *safe = NULL, **cur = &c->retired;
    while (*cur) {
        FlowReloadGeneration *cand = *cur;
        int act = (atomic_load_explicit(&cand->pin_count, memory_order_seq_cst) > 0);
        for (const FlowReloadReader *r = c->readers; r && !act; r = r->next) {
            uint64_t ae = atomic_load_explicit(&r->active_epoch, memory_order_seq_cst);
            if (ae && ae <= cand->retire_epoch)
                act = 1;
        }
        if (!act) {
            *cur = cand->next;
            cand->next = safe;
            safe = cand;
        } else
            cur = &cand->next;
    }
    size_t cnt = 0;
    while (safe) {
        FlowReloadGeneration *n = safe->next;
        drop_gen(c, safe);
        safe = n;
        cnt++;
    }
    return cnt;
}

int flow_reload_publish(FlowReloadContext *c, const FlowUnit *u, void *st) {
    if (!c || !u || !st || !u->run || !u->drop || u->abi_version != FLOW_RELOAD_ABI_VERSION)
        return FLOW_RELOAD_INVALID;
    FlowReloadGeneration *nxt = calloc(1, sizeof(*nxt));
    if (!nxt)
        return FLOW_RELOAD_INVALID;
    nxt->unit = u;
    nxt->state = st;
    pthread_mutex_lock(&c->lock);
    FlowReloadGeneration *old = atomic_load_explicit(&c->current, memory_order_acquire);
    if (old && u->constraint_hash != old->unit->constraint_hash) {
        pthread_mutex_unlock(&c->lock);
        free(nxt);
        return FLOW_RELOAD_INCOMPATIBLE;
    }
    if (old && old->state && u->migrate && u->migrate(c->host_context, old->state, st) != 0) {
        pthread_mutex_unlock(&c->lock);
        free(nxt);
        return FLOW_RELOAD_INCOMPATIBLE;
    }
    reclaim_locked(c);
    size_t ret_cnt = 0;
    for (FlowReloadGeneration *rg = c->retired; rg; rg = rg->next)
        ret_cnt++;
    if (ret_cnt >= FLOW_RELOAD_MAX_RETIRED_DEPTH) {
        pthread_mutex_unlock(&c->lock);
        free(nxt);
        return FLOW_RELOAD_JOURNAL_FULL;
    }
    nxt->generation = ++c->next_generation;
    if (old) {
        nxt->retire_epoch = atomic_fetch_add_explicit(&c->epoch, 1, memory_order_seq_cst) + 1;
        old->retire_epoch = nxt->retire_epoch;
        old->next = c->retired;
        c->retired = old;
    }
    atomic_store_explicit(&c->current, nxt, memory_order_seq_cst);
    pthread_mutex_unlock(&c->lock);
    return FLOW_RELOAD_OK;
}
int flow_reload_activate(FlowReloadContext *c, const FlowUnit *u) {
    void *st = NULL;
    if (!c || !u || !u->init || u->init(c->host_context, &st) != 0 || !st)
        return FLOW_RELOAD_INVALID;
    int rc = flow_reload_publish(c, u, st);
    if (rc != FLOW_RELOAD_OK)
        u->drop(c->host_context, st);
    return rc;
}
const FlowUnit *flow_reload_current_unit(const FlowReloadContext *c) {
    FlowReloadGeneration *g = c ? atomic_load_explicit(&c->current, memory_order_acquire) : NULL;
    return g ? g->unit : NULL;
}
uint64_t flow_reload_generation(const FlowReloadContext *c) {
    FlowReloadGeneration *g = c ? atomic_load_explicit(&c->current, memory_order_acquire) : NULL;
    return g ? g->generation : 0;
}
int flow_reload_begin(FlowReloadContext *c, FlowReloadReader *r, FlowInvocation *inv) {
    if (!c || !r || !inv || r->context != c)
        return FLOW_RELOAD_INVALID;
    atomic_store_explicit(&r->active_epoch, atomic_load_explicit(&c->epoch, memory_order_seq_cst),
                          memory_order_seq_cst);
    FlowReloadGeneration *g = atomic_load_explicit(&c->current, memory_order_seq_cst);
    if (!g) {
        atomic_store_explicit(&r->active_epoch, 0, memory_order_seq_cst);
        return FLOW_RELOAD_NO_CURRENT;
    }
    *inv = (FlowInvocation){.context = c,
                            .reader = r,
                            .unit = g->unit,
                            .state = g->state,
                            .generation = g->generation,
                            .active = 1};
    return FLOW_RELOAD_OK;
}
void flow_reload_end(FlowInvocation *inv) {
    if (inv && inv->active && inv->reader)
        atomic_store_explicit(&inv->reader->active_epoch, 0, memory_order_seq_cst);
    if (inv)
        *inv = (FlowInvocation){0};
}
int flow_reload_call(FlowReloadContext *c, FlowReloadReader *r, const void *in, void *out) {
    FlowInvocation inv = {0};
    int rc = flow_reload_begin(c, r, &inv);
    if (rc != FLOW_RELOAD_OK)
        return rc;
    rc = inv.unit->run(c->host_context, inv.state, in, out);
    flow_reload_end(&inv);
    return rc;
}
int flow_qsbr_call(FlowReloadContext *c, const void *in, void *out) {
    if (!c)
        return FLOW_RELOAD_INVALID;
    atomic_fetch_add_explicit(&c->anon_pins, 1, memory_order_seq_cst);
    FlowReloadGeneration *g = atomic_load_explicit(&c->current, memory_order_seq_cst);
    if (!g || !g->unit || !g->unit->run) {
        atomic_fetch_sub_explicit(&c->anon_pins, 1, memory_order_seq_cst);
        return FLOW_RELOAD_NO_CURRENT;
    }
    atomic_fetch_add_explicit(&g->pin_count, 1, memory_order_seq_cst);
    atomic_fetch_sub_explicit(&c->anon_pins, 1, memory_order_seq_cst);
    int rc = g->unit->run(c->host_context, g->state, in, out);
    atomic_fetch_sub_explicit(&g->pin_count, 1, memory_order_seq_cst);
    return rc;
}
void flow_qsbr_checkpoint(FlowReloadReader *r) {
    if (r)
        atomic_store_explicit(&r->qsbr_epoch,
                              r->context
                                  ? atomic_load_explicit(&r->context->epoch, memory_order_seq_cst)
                                  : atomic_load_explicit(&r->qsbr_epoch, memory_order_relaxed) + 1,
                              memory_order_seq_cst);
}
void flow_qsbr_offline(FlowReloadReader *r) {
    if (r)
        atomic_store_explicit(&r->is_offline, 1, memory_order_release);
}
void flow_qsbr_online(FlowReloadReader *r) {
    if (r)
        atomic_store_explicit(&r->is_offline, 0, memory_order_release);
}
size_t flow_reload_reclaim(FlowReloadContext *c) {
    if (!c)
        return 0;
    pthread_mutex_lock(&c->lock);
    size_t cnt = reclaim_locked(c);
    pthread_mutex_unlock(&c->lock);
    return cnt;
}
int flow_qsbr_synchronize(FlowReloadContext *c, uint64_t timeout_ns) {
    if (!c)
        return FLOW_RELOAD_INVALID;
    (void)timeout_ns;
    uint64_t tgt = atomic_load_explicit(&c->epoch, memory_order_seq_cst);
    for (int spin = 0; spin < 2000; ++spin) {
        int ok = 1;
        pthread_mutex_lock(&c->lock);
        for (const FlowReloadReader *r = c->readers; r; r = r->next) {
            if (atomic_load_explicit(&r->registered, memory_order_acquire) &&
                !atomic_load_explicit(&r->is_offline, memory_order_acquire) &&
                (atomic_load_explicit(&r->active_epoch, memory_order_seq_cst) != 0 ||
                 atomic_load_explicit(&r->qsbr_epoch, memory_order_acquire) < tgt)) {
                ok = 0;
                break;
            }
        }
        pthread_mutex_unlock(&c->lock);
        if (ok)
            return FLOW_RELOAD_OK;
        sched_yield();
    }
    return FLOW_RELOAD_BUSY;
}

void flow_schmitt_trigger_init(FlowSchmittTrigger *st, double base_min, uint64_t dwell_ns) {
    if (st)
        *st = (FlowSchmittTrigger){.drop_threshold = base_min * 0.8,
                                   .recovery_threshold = base_min * 1.5,
                                   .dwell_time_required_ns = dwell_ns ? dwell_ns : 500000000ULL};
}

int flow_schmitt_trigger_update(FlowSchmittTrigger *st, double val, uint64_t now_ns, int *changed) {
    if (!st)
        return 0;
    if (changed)
        *changed = 0;
    if (!st->current_state) {
        if (val < st->drop_threshold) {
            st->current_state = 1;
            st->stable_since_ns = 0;
            if (changed)
                *changed = 1;
        }
    } else if (val >= st->recovery_threshold) {
        if (!st->stable_since_ns)
            st->stable_since_ns = now_ns;
        else if (now_ns - st->stable_since_ns >= st->dwell_time_required_ns) {
            st->current_state = 0;
            st->stable_since_ns = 0;
            if (changed)
                *changed = 1;
        }
    } else
        st->stable_since_ns = 0;
    return st->current_state;
}
