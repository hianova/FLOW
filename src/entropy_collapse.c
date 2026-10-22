#include "entropy_collapse.h"
int flow_bump_qsbr_init(FlowBumpQsbrArena *arena, void *backing_memory, size_t capacity) { if (!arena || !backing_memory || !capacity) return 0;
    *arena = (FlowBumpQsbrArena){ .buffer = (uint8_t *)backing_memory, .capacity = capacity, .generation = 1 }; return 1; }
void *flow_bump_qsbr_alloc(FlowBumpQsbrArena *arena, size_t size_bytes) { if (!arena || !size_bytes) return NULL; size_t aligned = (size_bytes + 7) & ~((size_t)7);
    if (arena->cursor + aligned > arena->capacity) return NULL; void *ptr = arena->buffer + arena->cursor; arena->cursor += aligned; arena->total_allocs++; return ptr; }
int flow_bump_qsbr_quiescent_fold(FlowBumpQsbrArena *arena) { if (!arena) return 0; arena->cursor = 0; arena->generation++; arena->total_folds++; return 1; }
