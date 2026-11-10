#ifndef FLOW_IMAGE64_H
#define FLOW_IMAGE64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if (defined(__unix__) || defined(__APPLE__)) && !defined(FLOW_FREESTANDING) && \
    (!defined(__STDC_HOSTED__) || (__STDC_HOSTED__ != 0))
#define FLOW_IMAGE64_HAS_POSIX_MMAP 1
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#define FLOW_IMAGE64_HAS_POSIX_MMAP 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FLOW_IMAGE64 (.flow): Domain-Agnostic 64-Byte Memory-Mapped Container Format
 * ============================================================================
 * Owned by FLOW (libflow_rt). Zero coupling to BLOOM or any domain engine.
 * Replaces fragmented text/page formats (TOML, JSON, CSV, binary blobs) with
 * a single power-of-two array of 64-Byte Cache-Line Blocks (`FlowImageBlock64`):
 *
 *   - Block 0            : `FlowImageHeader64` (Superblock + Z/2^kZ mask + MAC)
 *   - Section 1 (Config) : 64B ABI / Clause / Parameter blocks
 *   - Section 2 (Data)   : `4 x 16B` (`UnitCell128`) or `8 x 64-bit` lanes
 *   - Section 3 (Domain) : Domain-specific 64B descriptors
 * ============================================================================ */

#define FLOW_IMAGE64_MAGIC   UINT64_C(0x464C4F57494D4734) /* "FLOWIMG4" */
#define FLOW_IMAGE64_VERSION UINT32_C(0x00010100)         /* v1.1.0     */

typedef struct __attribute__((aligned(64))) {
    uint64_t magic;              /* [0..7]   FLOW_IMAGE64_MAGIC ("FLOWIMG4")        */
    uint64_t block_cap_mask;     /* [8..15]  2^k - 1 total 64B blocks mask (0 OOB)  */
    uint32_t abi_block_off;      /* [16..19] Section 1: 64B Lean ABI / Config start */
    uint32_t abi_block_count;    /* [20..23] Section 1: 64B Lean ABI block count    */
    uint32_t unit_block_off;     /* [24..27] Section 2: 64B UnitCell128x4 start     */
    uint32_t unit_cell_count;    /* [28..31] Section 2: Total 16B UnitCell128 count */
    uint32_t prefab_block_off;   /* [32..35] Section 3: 64B Domain/Prefab start     */
    uint32_t prefab_block_count; /* [36..39] Section 3: 64B Domain/Prefab count     */
    uint32_t sieve_flags;        /* [40..43] 16-bit FLOW + UNIT hardware sieve      */
    uint32_t version;            /* [44..47] FLOW_IMAGE64_VERSION                   */
    uint64_t schema_hash;        /* [48..55] 64-bit semantic schema digest          */
    uint64_t merkle_mac;         /* [56..63] 64-bit MAC over whole image            */
} FlowImageHeader64;

_Static_assert(sizeof(FlowImageHeader64) == 64U, "FlowImageHeader64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowImageHeader64) == 64U, "FlowImageHeader64 must be 64-byte aligned");

typedef struct {
    uint64_t protocol_header;
    uint64_t body_slice;
} FlowImageUnitCellView16;

typedef union __attribute__((aligned(64))) {
    FlowImageHeader64 header;
#if defined(FLOW_LEAN_ABI_H)
    FlowLeanClause64 clause;
#endif
#if defined(FLOW_GPU_ABI_H)
    FlowGpuCsgOp64 csg_op;
    FlowGpuJointDq64 joint_dq;
    FlowGpuBvhNode64 bvh_node;
    FlowGpuSdfBrick64 sdf_brick;
    FlowStreamEgress64 stream_egress;
    FlowMatterState64 matter_state;
    FlowSvgUiNode64 svg_node;
#endif
    FlowImageUnitCellView16 cells[4];
    uint64_t raw_u64[8];
    int64_t raw_i64[8];
    uint32_t raw_u32[16];
    int32_t raw_i32[16];
    uint8_t raw_bytes[64];
} FlowImageBlock64;

_Static_assert(sizeof(FlowImageBlock64) == 64U, "FlowImageBlock64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowImageBlock64) == 64U, "FlowImageBlock64 must be 64-byte aligned");

static inline uint64_t flow_image64_compute_mac(const FlowImageBlock64 *blocks,
                                                size_t block_count) {
    if (!blocks || block_count == 0U)
        return 0ULL;
    uint64_t h = UINT64_C(0x9e3779b97f4a7c15);
    for (size_t b = 0U; b < 56U; ++b) {
        h ^= (uint64_t)blocks[0].raw_bytes[b];
        h *= UINT64_C(0x100000001b3);
    }
    for (size_t i = 1U; i < block_count; ++i) {
        for (size_t w = 0U; w < 8U; ++w) {
            h ^= blocks[i].raw_u64[w];
            h *= UINT64_C(0x100000001b3);
        }
    }
    return h;
}

static inline bool flow_image64_verify(const FlowImageBlock64 *blocks, size_t byte_len) {
    if (!blocks || byte_len < sizeof(FlowImageBlock64) || (byte_len & 63U) != 0U)
        return false;
    size_t block_count = byte_len / sizeof(FlowImageBlock64);
    const FlowImageHeader64 *hdr = &blocks[0].header;
    if (hdr->magic != FLOW_IMAGE64_MAGIC)
        return false;
    if ((hdr->block_cap_mask + 1ULL) != (uint64_t)block_count)
        return false;
    if ((block_count & (block_count - 1U)) != 0U)
        return false;
    uint64_t expected_mac = flow_image64_compute_mac(blocks, block_count);
    return hdr->merkle_mac == expected_mac;
}

static inline const FlowImageBlock64 *flow_image64_get_block_masked(const FlowImageBlock64 *blocks,
                                                                    uint64_t raw_block_idx,
                                                                    bool *out_inbounds) {
    uint64_t mask = blocks[0].header.block_cap_mask;
    uint64_t midx = raw_block_idx & mask;
    uint64_t oob = raw_block_idx & ~mask;
    if (out_inbounds) {
        *out_inbounds = (oob == 0ULL);
    }
    return &blocks[midx];
}

static inline bool flow_image64_stream_ingest_block(FlowImageBlock64 *arena_blocks,
                                                    size_t arena_block_count,
                                                    uint32_t incoming_block_idx,
                                                    const FlowImageBlock64 *incoming_block,
                                                    uint64_t *inout_received_mask64) {
    if (!arena_blocks || !incoming_block || arena_block_count == 0U ||
        (arena_block_count & (arena_block_count - 1U)) != 0U) {
        return false;
    }
    uint64_t cap_mask = (uint64_t)(arena_block_count - 1U);
    if (((uint64_t)incoming_block_idx & ~cap_mask) != 0ULL) {
        return false;
    }
    uint64_t slot = (uint64_t)incoming_block_idx & cap_mask;
    arena_blocks[slot] = *incoming_block;
    if (inout_received_mask64 && slot < 64ULL) {
        *inout_received_mask64 |= (1ULL << slot);
    }
    return true;
}

#if FLOW_IMAGE64_HAS_POSIX_MMAP
static inline const FlowImageBlock64 *flow_image64_mmap_readonly(const char *path,
                                                                 size_t *out_byte_len) {
    if (!path || !out_byte_len)
        return NULL;
    *out_byte_len = 0U;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(FlowImageBlock64) ||
        ((size_t)st.st_size & 63U) != 0U) {
        close(fd);
        return NULL;
    }
    size_t len = (size_t)st.st_size;
    void *ptr = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED)
        return NULL;
    const FlowImageBlock64 *blocks = (const FlowImageBlock64 *)ptr;
    if (!flow_image64_verify(blocks, len)) {
        munmap(ptr, len);
        return NULL;
    }
    *out_byte_len = len;
    return blocks;
}

static inline void flow_image64_munmap(const FlowImageBlock64 *blocks, size_t byte_len) {
    if (blocks && byte_len > 0U) {
        munmap((void *)(uintptr_t)blocks, byte_len);
    }
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* FLOW_IMAGE64_H */
