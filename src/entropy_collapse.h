#ifndef FLOW_ENTROPY_COLLAPSE_H
#define FLOW_ENTROPY_COLLAPSE_H
#include "smt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct FlowBumpQsbrArena { uint8_t *buffer; size_t capacity, cursor, total_allocs, total_folds; uint64_t generation, bound_wavefront_epoch; } FlowBumpQsbrArena;
int flow_bump_qsbr_init(FlowBumpQsbrArena *arena, void *backing_memory, size_t capacity);
void *flow_bump_qsbr_alloc(FlowBumpQsbrArena *arena, size_t size_bytes);
int flow_bump_qsbr_quiescent_fold(FlowBumpQsbrArena *arena);
#endif
