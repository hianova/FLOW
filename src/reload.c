#include "reload.h"
#include "bitspace.h"
#include "registry.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct FlowReloadGeneration {
    const FlowUnit *unit;
    void *state;
    uint64_t generation;
    uint64_t retire_epoch;
    struct FlowReloadGeneration *next;
} FlowReloadGeneration;

typedef struct {
    FlowMutation mutation;
    void *key_copy;
    void *value_copy;
} FlowJournalEntry;

typedef struct {
    int active;
    int overflow;
    uint64_t next_sequence;
    FlowReloadGeneration *old;
    const FlowUnit *candidate;
    void *candidate_state;
    FlowJournalEntry *entries;
    size_t count;
    size_t capacity;
} FlowLiveMigration;

struct FlowReloadContext {
    void *host_context;
    _Atomic(FlowReloadGeneration *) current;
    _Atomic uint64_t epoch;
    _Atomic int quiescence_waiters;
    _Atomic int stopping;
    uint64_t next_generation;
    pthread_mutex_t lock;
    pthread_mutex_t mutation_lock;
    pthread_cond_t quiesced;
    FlowReloadReader *readers;
    FlowReloadGeneration *retired;
    FlowLiveMigration live;
};

static uint64_t schema_mix(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    size_t i;
    for (i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t schema_string(uint64_t hash, const char *value) {
    static const unsigned char separator = 0;
    if (value == NULL) return schema_mix(hash, &separator, 1);
    hash = schema_mix(hash, value, strlen(value));
    return schema_mix(hash, &separator, 1);
}

static uint64_t schema_u32(uint64_t hash, uint32_t value) {
    unsigned char bytes[4] = {
        (unsigned char)(value & 0xffu),
        (unsigned char)((value >> 8) & 0xffu),
        (unsigned char)((value >> 16) & 0xffu),
        (unsigned char)((value >> 24) & 0xffu)
    };
    return schema_mix(hash, bytes, sizeof(bytes));
}

uint64_t flow_schema_hash(const FlowSchema *schema) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    if (schema == NULL || schema->name == NULL ||
        (schema->field_count != 0 && schema->fields == NULL)) return 0;
    hash = schema_string(hash, schema->name);
    hash = schema_u32(hash, schema->version);
    for (i = 0; i < schema->field_count; ++i) {
        const FlowSchemaField *field = &schema->fields[i];
        if (field->name == NULL || field->type == NULL) return 0;
        hash = schema_string(hash, field->name);
        hash = schema_string(hash, field->type);
        hash = schema_u32(hash, field->flags);
    }
    return hash;
}

int flow_schema_migration_compatible(const FlowSchema *old_schema,
                                     const FlowSchema *new_schema) {
    size_t i;
    if (old_schema == NULL || new_schema == NULL ||
        old_schema->name == NULL || new_schema->name == NULL ||
        strcmp(old_schema->name, new_schema->name) != 0 ||
        new_schema->version <= old_schema->version ||
        new_schema->field_count < old_schema->field_count)
        return 0;

    for (i = 0; i < old_schema->field_count; ++i) {
        const FlowSchemaField *old_field = &old_schema->fields[i];
        const FlowSchemaField *new_field = &new_schema->fields[i];
        if (old_field->name == NULL || new_field->name == NULL ||
            old_field->type == NULL || new_field->type == NULL ||
            strcmp(old_field->name, new_field->name) != 0 ||
            strcmp(old_field->type, new_field->type) != 0)
            return 0;
        if ((old_field->flags & FLOW_SCHEMA_FIELD_PERSISTENT) &&
            !(new_field->flags & FLOW_SCHEMA_FIELD_PERSISTENT))
            return 0;
        if ((old_field->flags & FLOW_SCHEMA_FIELD_ORDERED) &&
            !(new_field->flags & FLOW_SCHEMA_FIELD_ORDERED))
            return 0;
    }

    for (i = old_schema->field_count; i < new_schema->field_count; ++i) {
        const FlowSchemaField *new_field = &new_schema->fields[i];
        if (!(new_field->flags & FLOW_SCHEMA_FIELD_DEFAULTABLE))
            return 0;
    }
    return 1;
}

static int unit_valid(const FlowUnit *unit) {
    return unit != NULL &&
           unit->abi_version == FLOW_RELOAD_ABI_VERSION &&
           unit->name != NULL &&
           unit->run != NULL &&
           unit->drop != NULL;
}

int flow_reload_compatible(const FlowUnit *current, const FlowUnit *candidate) {
    if (!unit_valid(current) || !unit_valid(candidate)) return 0;
    if (current->constraint_hash != candidate->constraint_hash ||
        current->capability_hash != candidate->capability_hash)
        return 0;
    if (current->schema != NULL || candidate->schema != NULL) {
        if (current->schema == NULL || candidate->schema == NULL) return 0;
        if (current->schema->version == candidate->schema->version) {
            return flow_schema_hash(current->schema) ==
                   flow_schema_hash(candidate->schema);
        }
        return flow_schema_migration_compatible(current->schema,
                                                candidate->schema);
    }
    return current->semantic_schema_hash == candidate->semantic_schema_hash;
}

const char *flow_reload_status_name(FlowReloadStatus status) {
    switch (status) {
    case FLOW_RELOAD_OK: return "ok";
    case FLOW_RELOAD_INVALID: return "invalid";
    case FLOW_RELOAD_INCOMPATIBLE: return "incompatible";
    case FLOW_RELOAD_BUSY: return "busy";
    case FLOW_RELOAD_NO_CURRENT: return "no_current";
    case FLOW_RELOAD_JOURNAL_FULL: return "journal_full";
    default: return "unknown";
    }
}

FlowReloadContext *flow_reload_create(void *host_context) {
    FlowReloadContext *context = calloc(1, sizeof(*context));
    if (context == NULL) return NULL;
    context->host_context = host_context;
    atomic_init(&context->epoch, 1);
    atomic_init(&context->current, NULL);
    atomic_init(&context->quiescence_waiters, 0);
    atomic_init(&context->stopping, 0);
    context->next_generation = 0;
    pthread_mutex_init(&context->lock, NULL);
    pthread_mutex_init(&context->mutation_lock, NULL);
    pthread_cond_init(&context->quiesced, NULL);
    return context;
}

static void drop_generation(FlowReloadContext *context,
                            FlowReloadGeneration *generation) {
    if (generation == NULL) return;
    if (generation->unit != NULL && generation->unit->drop != NULL &&
        generation->state != NULL) {
        generation->unit->drop(context->host_context, generation->state);
        generation->state = NULL;
    }
}

static void journal_entry_dispose(FlowJournalEntry *entry);
static void journal_dispose(FlowJournalEntry *entries, size_t count);

int flow_reload_destroy(FlowReloadContext *context) {
    FlowReloadGeneration *generation;
    FlowReloadGeneration *retired;
    if (context == NULL) return FLOW_RELOAD_INVALID;
    pthread_mutex_lock(&context->lock);
    atomic_store_explicit(&context->stopping, 1, memory_order_release);
    if (context->readers != NULL) {
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_BUSY;
    }
    atomic_fetch_add_explicit(&context->quiescence_waiters, 1, memory_order_seq_cst);
    while (1) {
        int active = 0;
        const FlowReloadReader *reader;
        for (reader = context->readers; reader != NULL; reader = reader->next) {
            if (atomic_load_explicit(&reader->active_epoch,
                                     memory_order_seq_cst) != 0) {
                active = 1;
                break;
            }
        }
        if (!active) break;
        pthread_cond_wait(&context->quiesced, &context->lock);
    }
    atomic_fetch_sub_explicit(&context->quiescence_waiters, 1, memory_order_seq_cst);

    if (context->live.active) {
        if (context->live.candidate != NULL &&
            context->live.candidate->drop != NULL &&
            context->live.candidate_state != NULL) {
            context->live.candidate->drop(context->host_context,
                                          context->live.candidate_state);
        }
        journal_dispose(context->live.entries, context->live.count);
        context->live = (FlowLiveMigration){0};
    }

    generation = atomic_load_explicit(&context->current, memory_order_acquire);
    atomic_store_explicit(&context->current, NULL, memory_order_release);
    retired = context->retired;
    context->retired = NULL;
    pthread_mutex_unlock(&context->lock);

    if (generation != NULL) {
        drop_generation(context, generation);
        free(generation);
    }
    while (retired != NULL) {
        FlowReloadGeneration *next = retired->next;
        drop_generation(context, retired);
        free(retired);
        retired = next;
    }
    pthread_cond_destroy(&context->quiesced);
    pthread_mutex_destroy(&context->mutation_lock);
    pthread_mutex_destroy(&context->lock);
    free(context);
    return FLOW_RELOAD_OK;
}

int flow_reload_reader_register(FlowReloadContext *context,
                                FlowReloadReader *reader) {
    if (context == NULL || reader == NULL) return FLOW_RELOAD_INVALID;
    pthread_mutex_lock(&context->lock);
    if (context->stopping) {
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_BUSY;
    }
    reader->context = context;
    reader->next = context->readers;
    atomic_init(&reader->active_epoch, 0);
    atomic_store_explicit(&reader->registered, 1, memory_order_release);
    context->readers = reader;
    pthread_mutex_unlock(&context->lock);
    return FLOW_RELOAD_OK;
}

int flow_reload_reader_unregister(FlowReloadReader *reader) {
    FlowReloadContext *context;
    FlowReloadReader **cursor;
    if (reader == NULL) return FLOW_RELOAD_INVALID;
    context = reader->context;
    if (context == NULL) return FLOW_RELOAD_INVALID;
    pthread_mutex_lock(&context->lock);
    cursor = &context->readers;
    while (*cursor != NULL && *cursor != reader) {
        cursor = &(*cursor)->next;
    }
    if (*cursor == NULL) {
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_INVALID;
    }
    *cursor = reader->next;
    reader->next = NULL;
    atomic_store_explicit(&reader->registered, 0, memory_order_release);
    atomic_store_explicit(&reader->active_epoch, 0, memory_order_seq_cst);
    if (atomic_load_explicit(&context->quiescence_waiters, memory_order_acquire) > 0) {
        pthread_cond_broadcast(&context->quiesced);
    }
    reader->context = NULL;
    pthread_mutex_unlock(&context->lock);
    return FLOW_RELOAD_OK;
}

int flow_reload_publish(FlowReloadContext *context, const FlowUnit *unit,
                        void *state) {
    FlowReloadGeneration *next;
    FlowReloadGeneration *old;
    if (context == NULL || !unit_valid(unit) || state == NULL)
        return FLOW_RELOAD_INVALID;
    next = calloc(1, sizeof(*next));
    if (next == NULL) return FLOW_RELOAD_INVALID;
    next->unit = unit;
    next->state = state;

    pthread_mutex_lock(&context->lock);
    if (atomic_load_explicit(&context->stopping, memory_order_acquire) || context->live.active) {
        pthread_mutex_unlock(&context->lock);
        free(next);
        return FLOW_RELOAD_BUSY;
    }
    old = atomic_load_explicit(&context->current, memory_order_acquire);
    if (old != NULL && !flow_reload_compatible(old->unit, unit)) {
        pthread_mutex_unlock(&context->lock);
        free(next);
        return FLOW_RELOAD_INCOMPATIBLE;
    }
    next->generation = ++context->next_generation;
    if (old != NULL) {
        next->retire_epoch =
            atomic_fetch_add_explicit(&context->epoch, UINT64_C(1),
                                      memory_order_seq_cst) + UINT64_C(1);
        old->retire_epoch = next->retire_epoch;
        old->next = context->retired;
        context->retired = old;
    }
    atomic_store_explicit(&context->current, next, memory_order_release);
    pthread_mutex_unlock(&context->lock);
    return FLOW_RELOAD_OK;
}

int flow_reload_activate(FlowReloadContext *context, const FlowUnit *unit) {
    void *state = NULL;
    int result;
    if (context == NULL || !unit_valid(unit) || unit->init == NULL)
        return FLOW_RELOAD_INVALID;
    if (unit->init(context->host_context, &state) != 0 || state == NULL)
        return FLOW_RELOAD_INVALID;
    result = flow_reload_publish(context, unit, state);
    if (result != FLOW_RELOAD_OK) unit->drop(context->host_context, state);
    return result;
}

static void migration_stop(FlowReloadContext *context) {
    atomic_store_explicit(&context->stopping, 0, memory_order_release);
    pthread_cond_broadcast(&context->quiesced);
}

const FlowUnit *flow_reload_current_unit(const FlowReloadContext *context) {
    FlowReloadGeneration *generation;
    if (context == NULL) return NULL;
    generation = atomic_load_explicit(&context->current, memory_order_acquire);
    return generation == NULL ? NULL : generation->unit;
}

int flow_reload_migrate(FlowReloadContext *context, const FlowUnit *candidate) {
    return flow_reload_migrate_mode(context, candidate, FLOW_MIGRATE_AUTO);
}

int flow_reload_migrate_mode(FlowReloadContext *context, const FlowUnit *candidate,
                             FlowMigrationMode mode) {
    FlowReloadGeneration *old;
    void *new_state = NULL;
    int result;

    if (context == NULL || !unit_valid(candidate) || candidate->init == NULL ||
        candidate->migrate == NULL || candidate->schema == NULL)
        return FLOW_RELOAD_INVALID;

    if (mode == FLOW_MIGRATE_SNAPSHOT_COW && !candidate->supports_snapshot_cow) {
        return FLOW_RELOAD_INCOMPATIBLE;
    }

    if ((mode == FLOW_MIGRATE_SNAPSHOT_COW || mode == FLOW_MIGRATE_AUTO) &&
        candidate->supports_snapshot_cow && candidate->apply != NULL) {
        int begin_res = flow_reload_live_begin(context, candidate, 128);
        if (begin_res == FLOW_RELOAD_OK) {
            if (mode == FLOW_MIGRATE_SNAPSHOT_COW) {
                return flow_reload_live_finish(context);
            } else {
                return flow_reload_live_finish_or_fallback(context);
            }
        }
        if (mode == FLOW_MIGRATE_SNAPSHOT_COW) {
            return begin_res;
        }
    }

    pthread_mutex_lock(&context->lock);
    if (atomic_load_explicit(&context->stopping, memory_order_acquire) || context->live.active) {
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_BUSY;
    }
    old = atomic_load_explicit(&context->current, memory_order_acquire);
    if (old == NULL) {
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_NO_CURRENT;
    }
    if (!flow_reload_compatible(old->unit, candidate)) {
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_INCOMPATIBLE;
    }
    atomic_store_explicit(&context->stopping, 1, memory_order_release);
    atomic_fetch_add_explicit(&context->quiescence_waiters, 1, memory_order_seq_cst);
    while (1) {
        int active = 0;
        const FlowReloadReader *reader;
        for (reader = context->readers; reader != NULL; reader = reader->next) {
            if (atomic_load_explicit(&reader->active_epoch,
                                     memory_order_seq_cst) != 0) {
                active = 1;
                break;
            }
        }
        if (!active) break;
        pthread_cond_wait(&context->quiesced, &context->lock);
    }
    atomic_fetch_sub_explicit(&context->quiescence_waiters, 1, memory_order_seq_cst);
    pthread_mutex_unlock(&context->lock);

    if (candidate->init(context->host_context, &new_state) != 0 ||
        new_state == NULL) {
        pthread_mutex_lock(&context->lock);
        migration_stop(context);
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_INVALID;
    }
    result = candidate->migrate(context->host_context, old->state, new_state);
    if (result != 0) {
        candidate->drop(context->host_context, new_state);
        pthread_mutex_lock(&context->lock);
        migration_stop(context);
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_INVALID;
    }

    pthread_mutex_lock(&context->lock);
    if (atomic_load_explicit(&context->stopping, memory_order_acquire) &&
        atomic_load_explicit(&context->current, memory_order_acquire) == old) {
        FlowReloadGeneration *next = calloc(1, sizeof(*next));
        if (next == NULL) {
            migration_stop(context);
            pthread_mutex_unlock(&context->lock);
            candidate->drop(context->host_context, new_state);
            return FLOW_RELOAD_INVALID;
        }
        next->unit = candidate;
        next->state = new_state;
        next->generation = ++context->next_generation;
        next->retire_epoch =
            atomic_fetch_add_explicit(&context->epoch, UINT64_C(1),
                                      memory_order_seq_cst) + UINT64_C(1);
        old->retire_epoch = next->retire_epoch;
        old->next = context->retired;
        context->retired = old;
        atomic_store_explicit(&context->current, next, memory_order_release);
        migration_stop(context);
        pthread_mutex_unlock(&context->lock);
        return FLOW_RELOAD_OK;
    }
    migration_stop(context);
    pthread_mutex_unlock(&context->lock);
    candidate->drop(context->host_context, new_state);
    return FLOW_RELOAD_BUSY;
}

static void journal_entry_dispose(FlowJournalEntry *entry) {
    free(entry->key_copy);
    free(entry->value_copy);
    *entry = (FlowJournalEntry){0};
}

static void journal_dispose(FlowJournalEntry *entries, size_t count) {
    size_t i;
    if (entries == NULL) return;
    for (i = 0; i < count; ++i) journal_entry_dispose(&entries[i]);
    free(entries);
}

static int mutation_valid(const FlowMutation *mutation) {
    return mutation != NULL &&
           (mutation->kind == FLOW_MUTATION_UPSERT ||
            mutation->kind == FLOW_MUTATION_DELETE) &&
           (mutation->key_size == 0 || mutation->key != NULL) &&
           (mutation->value_size == 0 || mutation->value != NULL);
}

static int journal_capture(FlowJournalEntry *entry,
                           const FlowMutation *mutation, uint64_t sequence) {
    *entry = (FlowJournalEntry){0};
    entry->mutation = *mutation;
    entry->mutation.sequence = sequence;
    if (mutation->key_size != 0) {
        entry->key_copy = malloc(mutation->key_size);
        if (entry->key_copy == NULL) return 0;
        memcpy(entry->key_copy, mutation->key, mutation->key_size);
        entry->mutation.key = entry->key_copy;
    }
    if (mutation->value_size != 0) {
        entry->value_copy = malloc(mutation->value_size);
        if (entry->value_copy == NULL) {
            journal_entry_dispose(entry);
            return 0;
        }
        memcpy(entry->value_copy, mutation->value, mutation->value_size);
        entry->mutation.value = entry->value_copy;
    }
    return 1;
}

static void live_detach_locked(FlowReloadContext *context,
                               const FlowUnit **candidate,
                               void **candidate_state,
                               FlowJournalEntry **entries,
                               size_t *entry_count) {
    *candidate = context->live.candidate;
    *candidate_state = context->live.candidate_state;
    *entries = context->live.entries;
    *entry_count = context->live.count;
    context->live = (FlowLiveMigration){0};
}

int flow_reload_live_begin(FlowReloadContext *context,
                           const FlowUnit *candidate,
                           size_t journal_capacity) {
    FlowReloadGeneration *old;
    void *candidate_state = NULL;
    FlowJournalEntry *entries;
    int result;
    if (context == NULL || !unit_valid(candidate) || candidate->init == NULL ||
        candidate->migrate == NULL || candidate->apply == NULL ||
        candidate->schema == NULL || journal_capacity == 0)
        return FLOW_RELOAD_INVALID;

    entries = calloc(journal_capacity, sizeof(*entries));
    if (entries == NULL) return FLOW_RELOAD_INVALID;

    if (candidate->init(context->host_context, &candidate_state) != 0 ||
        candidate_state == NULL) {
        free(entries);
        return FLOW_RELOAD_INVALID;
    }

    pthread_mutex_lock(&context->lock);
    if (atomic_load_explicit(&context->stopping, memory_order_acquire) || context->live.active) {
        pthread_mutex_unlock(&context->lock);
        candidate->drop(context->host_context, candidate_state);
        free(entries);
        return FLOW_RELOAD_BUSY;
    }
    old = atomic_load_explicit(&context->current, memory_order_acquire);
    if (old == NULL) {
        pthread_mutex_unlock(&context->lock);
        candidate->drop(context->host_context, candidate_state);
        free(entries);
        return FLOW_RELOAD_NO_CURRENT;
    }
    if (!flow_reload_compatible(old->unit, candidate)) {
        pthread_mutex_unlock(&context->lock);
        candidate->drop(context->host_context, candidate_state);
        free(entries);
        return FLOW_RELOAD_INCOMPATIBLE;
    }
    context->live = (FlowLiveMigration){
        .active = 1,
        .next_sequence = 1,
        .old = old,
        .candidate = candidate,
        .candidate_state = candidate_state,
        .entries = entries,
        .capacity = journal_capacity
    };
    pthread_mutex_unlock(&context->lock);

    result = candidate->migrate(context->host_context, old->state,
                                candidate_state);
    if (result != 0) {
        (void)flow_reload_live_abort(context);
        return FLOW_RELOAD_INVALID;
    }
    return FLOW_RELOAD_OK;
}

int flow_reload_live_abort(FlowReloadContext *context) {
    const FlowUnit *candidate;
    void *candidate_state;
    FlowJournalEntry *entries;
    size_t entry_count;
    if (context == NULL) return FLOW_RELOAD_INVALID;
    pthread_mutex_lock(&context->mutation_lock);
    pthread_mutex_lock(&context->lock);
    if (!context->live.active) {
        pthread_mutex_unlock(&context->lock);
        pthread_mutex_unlock(&context->mutation_lock);
        return FLOW_RELOAD_INVALID;
    }
    live_detach_locked(context, &candidate, &candidate_state, &entries,
                       &entry_count);
    pthread_mutex_unlock(&context->lock);
    pthread_mutex_unlock(&context->mutation_lock);
    candidate->drop(context->host_context, candidate_state);
    journal_dispose(entries, entry_count);
    return FLOW_RELOAD_OK;
}

static int flow_reload_live_finish_internal(FlowReloadContext *context, int allow_fallback) {
    const FlowUnit *candidate;
    void *candidate_state;
    FlowJournalEntry *entries;
    size_t entry_count;
    FlowReloadGeneration *old;
    FlowReloadGeneration *next;
    size_t i;
    int result = FLOW_RELOAD_OK;
    if (context == NULL) return FLOW_RELOAD_INVALID;

    pthread_mutex_lock(&context->mutation_lock);
    pthread_mutex_lock(&context->lock);
    if (!context->live.active) {
        pthread_mutex_unlock(&context->lock);
        pthread_mutex_unlock(&context->mutation_lock);
        return FLOW_RELOAD_INVALID;
    }
    if (context->live.overflow && !allow_fallback) {
        live_detach_locked(context, &candidate, &candidate_state, &entries,
                           &entry_count);
        pthread_mutex_unlock(&context->lock);
        pthread_mutex_unlock(&context->mutation_lock);
        candidate->drop(context->host_context, candidate_state);
        journal_dispose(entries, entry_count);
        return FLOW_RELOAD_JOURNAL_FULL;
    }
    candidate = context->live.candidate;
    candidate_state = context->live.candidate_state;
    old = context->live.old;
    entries = context->live.entries;
    entry_count = context->live.count;
    for (i = 0; i < entry_count; ++i) {
        result = candidate->apply(context->host_context, candidate_state,
                                  &entries[i].mutation);
        if (result != 0) break;
    }
    if (result != 0 ||
        atomic_load_explicit(&context->current, memory_order_acquire) != old) {
        live_detach_locked(context, &candidate, &candidate_state, &entries,
                           &entry_count);
        pthread_mutex_unlock(&context->lock);
        pthread_mutex_unlock(&context->mutation_lock);
        candidate->drop(context->host_context, candidate_state);
        journal_dispose(entries, entry_count);
        return FLOW_RELOAD_INVALID;
    }
    next = calloc(1, sizeof(*next));
    if (next == NULL) {
        live_detach_locked(context, &candidate, &candidate_state, &entries,
                           &entry_count);
        pthread_mutex_unlock(&context->lock);
        pthread_mutex_unlock(&context->mutation_lock);
        candidate->drop(context->host_context, candidate_state);
        journal_dispose(entries, entry_count);
        return FLOW_RELOAD_INVALID;
    }
    next->unit = candidate;
    next->state = candidate_state;
    next->generation = ++context->next_generation;
    next->retire_epoch =
        atomic_fetch_add_explicit(&context->epoch, UINT64_C(1),
                                  memory_order_seq_cst) + UINT64_C(1);
    old->retire_epoch = next->retire_epoch;
    old->next = context->retired;
    context->retired = old;
    atomic_store_explicit(&context->current, next, memory_order_release);
    context->live = (FlowLiveMigration){0};
    pthread_mutex_unlock(&context->lock);
    pthread_mutex_unlock(&context->mutation_lock);
    journal_dispose(entries, entry_count);
    return FLOW_RELOAD_OK;
}

int flow_reload_live_finish(FlowReloadContext *context) {
    return flow_reload_live_finish_internal(context, 0);
}

int flow_reload_live_finish_or_fallback(FlowReloadContext *context) {
    return flow_reload_live_finish_internal(context, 1);
}

/* ========================================================================= */
/* Lock-Free RCU Fast Path Read Operations                                   */
/* ========================================================================= */

int flow_reload_begin(FlowReloadContext *context, FlowReloadReader *reader,
                      FlowInvocation *invocation) {
    FlowReloadGeneration *generation;
    uint64_t epoch;
    if (context == NULL || reader == NULL || invocation == NULL)
        return FLOW_RELOAD_INVALID;
    if (invocation->active) return FLOW_RELOAD_BUSY;
    if (reader->context != context ||
        !atomic_load_explicit(&reader->registered, memory_order_acquire)) {
        return FLOW_RELOAD_INVALID;
    }
    if (atomic_load_explicit(&context->stopping, memory_order_acquire)) {
        return FLOW_RELOAD_BUSY;
    }
    epoch = atomic_load_explicit(&context->epoch, memory_order_acquire);
    atomic_store_explicit(&reader->active_epoch, epoch, memory_order_seq_cst);
    generation = atomic_load_explicit(&context->current, memory_order_acquire);
    if (generation == NULL) {
        atomic_store_explicit(&reader->active_epoch, 0, memory_order_seq_cst);
        return FLOW_RELOAD_NO_CURRENT;
    }
    *invocation = (FlowInvocation){
        .context = context,
        .reader = reader,
        .unit = generation->unit,
        .state = generation->state,
        .generation = generation->generation,
        .active = 1
    };
    return FLOW_RELOAD_OK;
}

void flow_reload_end(FlowInvocation *invocation) {
    if (invocation == NULL || !invocation->active ||
        invocation->reader == NULL) return;
    atomic_store_explicit(&invocation->reader->active_epoch, 0,
                          memory_order_seq_cst);
    if (atomic_load_explicit(&invocation->context->quiescence_waiters, memory_order_acquire) > 0) {
        pthread_mutex_lock(&invocation->context->lock);
        pthread_cond_broadcast(&invocation->context->quiesced);
        pthread_mutex_unlock(&invocation->context->lock);
    }
    *invocation = (FlowInvocation){0};
}

int flow_reload_call(FlowReloadContext *context, FlowReloadReader *reader,
                     const void *input, void *output) {
    FlowInvocation invocation = {0};
    int result = flow_reload_begin(context, reader, &invocation);
    if (result != FLOW_RELOAD_OK) return result;
    result = invocation.unit->run(context->host_context, invocation.state,
                                  input, output);
    flow_reload_end(&invocation);
    return result;
}

int flow_reload_apply(FlowReloadContext *context, FlowReloadReader *reader,
                      const FlowMutation *mutation) {
    FlowInvocation invocation = {0};
    FlowReloadGeneration *current;
    FlowJournalEntry entry;
    int result;
    if (!mutation_valid(mutation)) return FLOW_RELOAD_INVALID;
    result = flow_reload_begin(context, reader, &invocation);
    if (result != FLOW_RELOAD_OK) return result;

    pthread_mutex_lock(&context->mutation_lock);
    pthread_mutex_lock(&context->lock);
    current = atomic_load_explicit(&context->current, memory_order_acquire);
    if (current == NULL || current->generation != invocation.generation) {
        result = FLOW_RELOAD_BUSY;
    } else if (context->live.active) {
        if (context->live.old != current ||
            context->live.count >= context->live.capacity) {
            context->live.overflow = 1;
            result = FLOW_RELOAD_JOURNAL_FULL;
        } else if (!journal_capture(&entry, mutation,
                                    context->live.next_sequence++)) {
            context->live.overflow = 1;
            result = FLOW_RELOAD_JOURNAL_FULL;
        } else {
            result = invocation.unit->apply(context->host_context,
                                            invocation.state,
                                            &entry.mutation);
            if (result == 0) {
                context->live.entries[context->live.count++] = entry;
            } else {
                journal_entry_dispose(&entry);
            }
        }
    } else if (invocation.unit->apply == NULL) {
        result = FLOW_RELOAD_INVALID;
    } else {
        result = invocation.unit->apply(context->host_context,
                                        invocation.state, mutation);
    }
    pthread_mutex_unlock(&context->lock);
    pthread_mutex_unlock(&context->mutation_lock);
    flow_reload_end(&invocation);
    return result;
}

static int generation_safe(FlowReloadContext *context, uint64_t retire_epoch) {
    const FlowReloadReader *reader;
    for (reader = context->readers; reader != NULL; reader = reader->next) {
        uint64_t active =
            atomic_load_explicit(&reader->active_epoch, memory_order_seq_cst);
        if (active != 0 && active <= retire_epoch) return 0;
    }
    return 1;
}

size_t flow_reload_reclaim(FlowReloadContext *context) {
    FlowReloadGeneration *ready = NULL;
    size_t reclaimed = 0;
    if (context == NULL) return 0;
    pthread_mutex_lock(&context->lock);
    {
        FlowReloadGeneration **cursor = &context->retired;
        while (*cursor != NULL) {
            FlowReloadGeneration *candidate = *cursor;
            if (!generation_safe(context, candidate->retire_epoch)) {
                cursor = &candidate->next;
                continue;
            }
            *cursor = candidate->next;
            candidate->next = ready;
            ready = candidate;
        }
    }
    pthread_mutex_unlock(&context->lock);
    while (ready != NULL) {
        FlowReloadGeneration *next = ready->next;
        drop_generation(context, ready);
        free(ready);
        ready = next;
        ++reclaimed;
    }
    return reclaimed;
}

/* ========================================================================= */
/* FlowPlanArtifact Reload Integration                                       */
/* ========================================================================= */

typedef struct {
    size_t capacity;
    size_t threads;
    size_t shards;
    size_t item_count;
    int *data;
} FlowPlanUnitState;

static int flow_plan_unit_init(void *host_ctx, void **state_out) {
    (void)host_ctx;
    FlowPlanUnitState *st = calloc(1, sizeof(*st));
    if (st == NULL) return -1;
    st->capacity = 1024;
    st->threads = 1;
    st->shards = 1;
    st->data = calloc(st->capacity, sizeof(int));
    if (st->data == NULL) {
        free(st);
        return -1;
    }
    *state_out = st;
    return 0;
}

static int flow_plan_unit_run(void *host_ctx, void *state, const void *in, void *out) {
    (void)host_ctx;
    FlowPlanUnitState *st = (FlowPlanUnitState *)state;
    if (st == NULL) return -1;
    if (in != NULL && st->item_count < st->capacity) {
        st->data[st->item_count++] = *(const int *)in;
    }
    if (out != NULL) {
        *(int *)out = (int)st->item_count;
    }
    return 0;
}

static int flow_plan_unit_apply(void *host_ctx, void *state, const FlowMutation *mutation) {
    (void)host_ctx;
    FlowPlanUnitState *st = (FlowPlanUnitState *)state;
    if (st == NULL || mutation == NULL) return -1;
    if (mutation->kind == FLOW_MUTATION_UPSERT && mutation->value != NULL &&
        mutation->value_size == sizeof(int) && st->item_count < st->capacity) {
        st->data[st->item_count++] = *(const int *)mutation->value;
        return 0;
    }
    return 0;
}

static int flow_plan_unit_migrate(void *host_ctx, const void *old_state, void *new_state) {
    (void)host_ctx;
    const FlowPlanUnitState *old_st = (const FlowPlanUnitState *)old_state;
    FlowPlanUnitState *new_st = (FlowPlanUnitState *)new_state;
    if (old_st == NULL || new_st == NULL) return -1;
    size_t copy_count = old_st->item_count;
    if (copy_count > new_st->capacity) copy_count = new_st->capacity;
    if (copy_count > 0 && old_st->data != NULL && new_st->data != NULL) {
        memcpy(new_st->data, old_st->data, copy_count * sizeof(int));
    }
    new_st->item_count = copy_count;
    return 0;
}

static void flow_plan_unit_drop(void *host_ctx, void *state) {
    (void)host_ctx;
    FlowPlanUnitState *st = (FlowPlanUnitState *)state;
    if (st != NULL) {
        free(st->data);
        free(st);
    }
}

static const FlowSchemaField PLAN_SCHEMA_FIELDS[] = {
    {"data", "i32[]", FLOW_SCHEMA_FIELD_PERSISTENT | FLOW_SCHEMA_FIELD_ORDERED},
    {"item_count", "u64", FLOW_SCHEMA_FIELD_PERSISTENT}
};
static const FlowSchema PLAN_SCHEMA_V1 = {
    "plan_collection", 1, PLAN_SCHEMA_FIELDS, 2
};

int flow_reload_plan(FlowReloadContext *context, const FlowPlanArtifact *artifact,
                     const SemanticIR *ir, FlowMigrationMode mode) {
    char val_err[256];
    if (context == NULL || artifact == NULL || ir == NULL) return FLOW_RELOAD_INVALID;

    /* 1. Strict artifact validation before activation */
    if (!flow_artifact_validate(artifact, ir, NULL, val_err, sizeof(val_err))) {
        return FLOW_RELOAD_INCOMPATIBLE;
    }

    /* 2. Locate component */
    const Component *comp = NULL;
    for (size_t i = 0; i < component_count(); ++i) {
        const Component *c = component_at(i);
        if (c != NULL && strcmp(c->id, artifact->component_id) == 0) {
            comp = c;
            break;
        }
    }
    if (comp == NULL) return FLOW_RELOAD_INVALID;

    /* 3. Build executable FlowUnit for the plan via plugin hook or fallback */
    FlowUnit *unit = calloc(1, sizeof(*unit));
    if (unit == NULL) return FLOW_RELOAD_INVALID;
    const FlowPlugin *plugin = flow_component_plugin(comp);
    if (plugin != NULL && plugin->create_unit != NULL) {
        if (!plugin->create_unit(artifact, ir, comp, unit)) {
            free(unit);
            return FLOW_RELOAD_INVALID;
        }
    } else {
        unit->abi_version = FLOW_RELOAD_ABI_VERSION;
        unit->name = comp->id;
        unit->init = flow_plan_unit_init;
        unit->run = flow_plan_unit_run;
        unit->apply = flow_plan_unit_apply;
        unit->migrate = flow_plan_unit_migrate;
        unit->drop = flow_plan_unit_drop;
        unit->schema = &PLAN_SCHEMA_V1;
        unit->constraint_hash = flow_compute_contract_hash(ir);
        unit->capability_hash = 0x55;
        unit->semantic_schema_hash = artifact->plan_schema_hash;
        unit->supports_snapshot_cow = 1;
    }

    /* Check if already has active generation */
    const FlowUnit *curr = flow_reload_current_unit(context);
    if (curr == NULL) {
        void *state = NULL;
        if (unit->init(context->host_context, &state) != 0 || state == NULL) {
            free(unit);
            return FLOW_RELOAD_INVALID;
        }
        int res = flow_reload_publish(context, unit, state);
        if (res != FLOW_RELOAD_OK) {
            unit->drop(context->host_context, state);
            free(unit);
        }
        return res;
    } else {
        int res = flow_reload_migrate_mode(context, unit, mode);
        if (res != FLOW_RELOAD_OK) free(unit);
        return res;
    }
}
