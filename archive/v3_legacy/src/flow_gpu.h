#ifndef FLOW_GPU_H
#define FLOW_GPU_H

#include "flow.h"
#include "bitspace.h"
#include "cubical_hott.h"
#include "flow_jet.h"
#include "flow_bridge.h"
#include "reload.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW GPU-Native Bit-Manifold (BMF) Meshlet & Sub-Byte Geometry Pipeline
 * ============================================================================
 * Unifies:
 *   1. Pointerless Flat Index-Based Hierarchy (DoD / 0-Pointer Chasing)
 *   2. 1.58-Bit Ternary ({-1, 0, +1}) & 2-Bit (BC1/DXT1 LUT) Sub-Byte Packing
 *   3. GPU-Native BMF Meshlets (10-bit XYZ Quantization + 6-bit Micro-Indices)
 *   4. 2-Jet Contact Geometry -> AABB & Normal Cone Backface Culling
 *   5. 64-Lane Subgroup Ballot (simd_ballot / __ballot_sync) + POPCNT/CTZ
 *      1-Cycle Stream Compaction
 *   6. Watertight Global Lattice Snap (No T-Junctions / Crack-Free Seams)
 * ============================================================================
 */

#define FLOW_BMF_INVALID_HANDLE    UINT32_C(0xFFFFFFFF)
#define FLOW_BMF_MESHLET_MAX_VERTS 64U
#define FLOW_BMF_MESHLET_MAX_PRIMS 124U
#define FLOW_BMF_QUANT_BITS        10U
#define FLOW_BMF_QUANT_MAX         ((1U << FLOW_BMF_QUANT_BITS) - 1U) /* 1023 */
#define FLOW_BMF_MICRO_INDEX_BITS  6U
#define FLOW_BMF_MICRO_INDEX_MASK  ((1U << FLOW_BMF_MICRO_INDEX_BITS) - 1U) /* 63 */
#define FLOW_BMF_WAVE_SIZE         64U

/*
 * Canonical 64-bit Grothendieck Topos Sieve Axes for GPU-Native BMF Meshlets.
 */
typedef enum {
    FLOW_GPU_SIEVE_FINITE_AABB = (1ULL << 0),        /* AABB & cone parameters are finite/valid */
    FLOW_GPU_SIEVE_MICRO_INDEX_VALID = (1ULL << 1),  /* All 6-bit micro-indices < vertex_count */
    FLOW_GPU_SIEVE_DECODE_NO_OVERFLOW = (1ULL << 2), /* 10-bit fixed-point decode proven bounded */
    FLOW_GPU_SIEVE_WATERTIGHT_SEAM = (1ULL << 3),    /* Seam vertices locked to global lattice */
    FLOW_GPU_SIEVE_FRUSTUM_VISIBLE = (1ULL << 4),    /* Cluster AABB intersects view frustum */
    FLOW_GPU_SIEVE_CONE_VISIBLE = (1ULL << 5),       /* 2-Jet normal cone is not backface-culled */
    FLOW_GPU_SIEVE_FLAT_DAG_VALID = (1ULL << 6),     /* Pointerless flat parent_idx < self_idx */
    FLOW_GPU_SIEVE_CANONICAL_ALL = 0x7FULL
} FlowGpuToposSieveAxis;

/*
 * 2-bit topological vertex classification stored in bits [31:30] of each 32-bit BMF vertex.
 */
typedef enum {
    FLOW_BMF_VERT_INTERIOR = 0U,    /* Interior cluster vertex */
    FLOW_BMF_VERT_SEAM_LOCKED = 1U, /* Shared meshlet boundary vertex locked to global lattice */
    FLOW_BMF_VERT_CREASE = 2U,      /* C^0 sharp feature crease vertex */
    FLOW_BMF_VERT_CORNER = 3U       /* Topological corner anchor vertex */
} FlowBmfVertexFlag;

/* ============================================================================
 * 1. 1.58-Bit Ternary Bit-Manifold Vector (BitNet b1.58 Isomorphic)
 * ============================================================================
 * Encodes 64 ternary values w_i in {-1, 0, +1} using two 64-bit bit-planes:
 *   - nonzero_mask: bit i is 1 iff w_i != 0
 *   - sign_mask:    bit i is 1 iff w_i == -1 (canonical invariant: sign_mask & ~nonzero_mask == 0)
 * State encoding per lane: 00 -> 0, 01 -> +1, 11 -> -1 (10 forbidden/canonicalized to 00).
 */
typedef struct {
    uint64_t nonzero_mask;
    uint64_t sign_mask;
} FlowBmfTernary64;

static inline FlowBmfTernary64 flow_bmf_ternary64_zero(void) {
    FlowBmfTernary64 t = {0ULL, 0ULL};
    return t;
}

static inline bool flow_bmf_ternary64_is_canonical(FlowBmfTernary64 t) {
    return (t.sign_mask & ~t.nonzero_mask) == 0ULL;
}

static inline FlowBmfTernary64 flow_bmf_ternary64_canonicalize(FlowBmfTernary64 t) {
    t.sign_mask &= t.nonzero_mask;
    return t;
}

/*
 * 1-cycle branchless hardware bitwise decode for lane i in [0, 63]:
 *   nz = (nonzero_mask >> i) & 1
 *   sg = (sign_mask >> i) & 1 (where sg <= nz by canonical invariant)
 *   value = nz - 2 * sg  in {-1, 0, +1}
 */
static inline int8_t flow_bmf_ternary64_get(FlowBmfTernary64 t, uint32_t lane) {
    if (lane >= 64U)
        return 0;
    uint64_t nz = (t.nonzero_mask >> lane) & 1ULL;
    uint64_t sg = (t.sign_mask & t.nonzero_mask) >> lane & 1ULL;
    return (int8_t)((int32_t)nz - (int32_t)(sg << 1U));
}

/*
 * 1-cycle SWAR / SIMD 64-dimensional ternary dot product <a, b> using hardware POPCNT:
 *   active    = a.nonzero & b.nonzero
 *   same_sign = active & ~(a.sign ^ b.sign)  -> contributes +1
 *   opp_sign  = active &  (a.sign ^ b.sign)  -> contributes -1
 */
static inline int32_t flow_bmf_ternary64_dot(FlowBmfTernary64 a, FlowBmfTernary64 b) {
    uint64_t active = a.nonzero_mask & b.nonzero_mask;
    uint64_t xor_sign = (a.sign_mask & a.nonzero_mask) ^ (b.sign_mask & b.nonzero_mask);
    uint64_t same_sign = active & ~xor_sign;
    uint64_t opp_sign = active & xor_sign;
    return (int32_t)__builtin_popcountll(same_sign) - (int32_t)__builtin_popcountll(opp_sign);
}

FlowBmfTernary64 flow_bmf_ternary64_encode(const int8_t values[64]);
void flow_bmf_ternary64_decode_all(FlowBmfTernary64 t, int8_t out_values[64]);

/* ============================================================================
 * 2. 2-Bit Block Interpolation (BC1 / DXT1 Hardware Texture LUT Isomorphic)
 * ============================================================================
 * Compresses 16 signed 16-bit fixed-point samples (e.g. 4x4 geometric/surface tile)
 * into 8 bytes (4 bits/sample total: two 16-bit endpoints c0, c1 + sixteen 2-bit codes):
 *   00 -> c0
 *   01 -> c1
 *   10 -> (2*c0 + c1) / 3
 *   11 -> (c0 + 2*c1) / 3
 */
typedef struct {
    int16_t c0;
    int16_t c1;
    uint32_t selectors; /* 16 x 2-bit indices: lane i in bits [2*i + 1 : 2*i] */
} FlowBmfBlock2Bit;
_Static_assert(sizeof(FlowBmfBlock2Bit) == 8, "FlowBmfBlock2Bit must be 8 bytes");

static inline int16_t flow_bmf_block2bit_decode_lane(FlowBmfBlock2Bit blk, uint32_t lane) {
    if (lane >= 16U)
        return 0;
    uint32_t code = (blk.selectors >> (lane * 2U)) & 0x3U;
    int32_t c0 = (int32_t)blk.c0;
    int32_t c1 = (int32_t)blk.c1;
    int32_t lut[4] = {c0, c1, (2 * c0 + c1) / 3, (c0 + 2 * c1) / 3};
    return (int16_t)lut[code];
}

FlowBmfBlock2Bit flow_bmf_block2bit_encode(const int16_t samples[16]);
void flow_bmf_block2bit_decode_all(FlowBmfBlock2Bit blk, int16_t out_samples[16]);

/* ============================================================================
 * 3. Pointerless Flat Index-Based Scene Hierarchy (0-Pointer Chasing DoD)
 * ============================================================================
 * Replaces OOP scene-graph trees (Transform*) with a contiguous flat array
 * where parent_idx < node_idx (or FLOW_BMF_INVALID_HANDLE for root nodes).
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t parent_idx;       /* FLOW_BMF_INVALID_HANDLE or strictly < current node index */
    uint32_t meshlet_base_idx; /* Base index into global bindless meshlet buffer */
    uint32_t meshlet_count;    /* Number of contiguous meshlets owned by this node */
    uint32_t flags;
    float local_pos[3];
    float local_scale;
    float world_pos[3];
    float world_scale;
    uint64_t node_sieve_mask;
    uint64_t reserved;
} FlowBmfFlatNode;
_Static_assert(sizeof(FlowBmfFlatNode) == 64, "FlowBmfFlatNode must be 64 bytes (1 cache line)");

int flow_gpu_propagate_flat_nodes(FlowBmfFlatNode *nodes, size_t count);

/* ============================================================================
 * 4. GPU-Native BMF Meshlet Prefab (10-Bit XYZ + 6-Bit Micro-Indices)
 * ============================================================================
 * Each vertex is packed into a single 32-bit word (4 Bytes vs 36 Bytes traditional):
 *   Bits [ 9: 0] : qx in [0, 1023]
 *   Bits [19:10] : qy in [0, 1023]
 *   Bits [29:20] : qz in [0, 1023]
 *   Bits [31:30] : FlowBmfVertexFlag (interior / seam_locked / crease / corner)
 *
 * Each triangle is packed using 6-bit micro-indices (i0, i1, i2 in [0, 63]):
 *   18 bits per triangle: i0 | (i1 << 6) | (i2 << 12)
 */
static inline uint32_t flow_bmf_pack_vertex_10bit(uint32_t qx, uint32_t qy, uint32_t qz,
                                                  uint32_t flag) {
    return (qx & FLOW_BMF_QUANT_MAX) | ((qy & FLOW_BMF_QUANT_MAX) << 10U) |
           ((qz & FLOW_BMF_QUANT_MAX) << 20U) | ((flag & 0x3U) << 30U);
}

static inline void flow_bmf_unpack_vertex_10bit(uint32_t packed, uint32_t *qx_out, uint32_t *qy_out,
                                                uint32_t *qz_out, uint32_t *flag_out) {
    if (qx_out)
        *qx_out = packed & FLOW_BMF_QUANT_MAX;
    if (qy_out)
        *qy_out = (packed >> 10U) & FLOW_BMF_QUANT_MAX;
    if (qz_out)
        *qz_out = (packed >> 20U) & FLOW_BMF_QUANT_MAX;
    if (flag_out)
        *flag_out = (packed >> 30U) & 0x3U;
}

static inline uint32_t flow_bmf_pack_tri_6bit(uint8_t i0, uint8_t i1, uint8_t i2) {
    return ((uint32_t)i0 & FLOW_BMF_MICRO_INDEX_MASK) |
           (((uint32_t)i1 & FLOW_BMF_MICRO_INDEX_MASK) << 6U) |
           (((uint32_t)i2 & FLOW_BMF_MICRO_INDEX_MASK) << 12U);
}

static inline void flow_bmf_unpack_tri_6bit(uint32_t packed, uint8_t *i0_out, uint8_t *i1_out,
                                            uint8_t *i2_out) {
    if (i0_out)
        *i0_out = (uint8_t)(packed & FLOW_BMF_MICRO_INDEX_MASK);
    if (i1_out)
        *i1_out = (uint8_t)((packed >> 6U) & FLOW_BMF_MICRO_INDEX_MASK);
    if (i2_out)
        *i2_out = (uint8_t)((packed >> 12U) & FLOW_BMF_MICRO_INDEX_MASK);
}

/*
 * Pack 3 triangles (9 micro-indices x 6 bits = 54 bits) into a single 64-bit register word.
 */
static inline uint64_t flow_bmf_pack_3tris_54bit(uint32_t t0, uint32_t t1, uint32_t t2) {
    return ((uint64_t)(t0 & 0x3FFFFU)) | (((uint64_t)(t1 & 0x3FFFFU)) << 18U) |
           (((uint64_t)(t2 & 0x3FFFFU)) << 36U);
}

static inline void flow_bmf_unpack_3tris_54bit(uint64_t word, uint32_t *t0_out, uint32_t *t1_out,
                                               uint32_t *t2_out) {
    if (t0_out)
        *t0_out = (uint32_t)(word & 0x3FFFFULL);
    if (t1_out)
        *t1_out = (uint32_t)((word >> 18U) & 0x3FFFFULL);
    if (t2_out)
        *t2_out = (uint32_t)((word >> 36U) & 0x3FFFFULL);
}

typedef struct __attribute__((aligned(64))) {
    /* Global integer snap-lattice origin & quantum step (guarantees watertight seams) */
    int32_t lattice_min[3];
    uint32_t lattice_step; /* Quantum step in fixed-point units (> 0) */
    float world_scale_inv; /* Scale factor from global integer lattice to world float */

    /* 2-Jet Contact Geometry Derived AABB & Normal Cone Culling Bounds */
    float aabb_min[3];
    float aabb_max[3];
    float cone_apex[3];
    float cone_axis[3]; /* Unit normal cone axis derived from 1-Jet tangent cross product */
    float cone_cutoff;  /* -sin(half_angle) derived from 2-Jet Hessian curvature bound */

    double jet2_max_lipschitz_d1;
    double jet2_max_curvature_d2;

    uint16_t vertex_count;   /* <= FLOW_BMF_MESHLET_MAX_VERTS (64) */
    uint16_t triangle_count; /* <= FLOW_BMF_MESHLET_MAX_PRIMS (124) */
    uint32_t parent_node_idx;
    uint64_t topos_sieve_mask;

    /* Bit-Manifold Compact Payload (0 pointers, directly mmap / GPU-tile decodable) */
    uint32_t packed_vertices[FLOW_BMF_MESHLET_MAX_VERTS];
    uint32_t packed_triangles[FLOW_BMF_MESHLET_MAX_PRIMS];
    FlowBmfTernary64 normal_oct_x;
    FlowBmfTernary64 normal_oct_y;
    FlowBmfTernary64 normal_oct_z;
} FlowBmfMeshlet;

/*
 * Encodes a raw mesh cluster into a GPU-Native BMF Meshlet, snapping the cluster AABB
 * to the global integer lattice so shared boundary vertices across adjacent meshlets
 * produce bit-identical decoded coordinates (zero T-junctions / cracks).
 */
int flow_gpu_meshlet_encode(FlowBmfMeshlet *out_meshlet, const float (*positions)[3],
                            const uint8_t *vertex_flags, size_t vertex_count,
                            const uint8_t (*triangles)[3], size_t triangle_count,
                            float global_quantum, uint32_t parent_node_idx);

/*
 * Decodes vertex `v_idx` into both exact global lattice coordinates `out_lattice[3]`
 * and IEEE-754 world coordinates `out_pos[3]` without branching or overflow.
 */
int flow_gpu_meshlet_decode_vertex(const FlowBmfMeshlet *meshlet, uint32_t v_idx, float out_pos[3],
                                   int64_t out_lattice[3], uint32_t *out_flag);

/*
 * Derives 2-Jet contact geometry bounds (AABB inflation + Normal Cone axis & cutoff)
 * from 2-Jet surface patch fibers J^2(R^2, R^3) and updates the meshlet's Topos Sieve.
 */
int flow_gpu_meshlet_derive_jet2_cone(FlowBmfMeshlet *meshlet, const FlowJetScalar2 jet_patch_u[3],
                                      const FlowJetScalar2 jet_patch_v[3], double param_step_du,
                                      double param_step_dv);

/*
 * Verifies all formal meshlet invariants (6-bit micro-index bounds, 10-bit decode
 * overflow-freedom, finite AABB/cone, and canonical 1.58-bit ternary normals).
 */
FlowSMTResult flow_gpu_meshlet_verify_smt(const FlowBmfMeshlet *meshlet,
                                          FlowSMTProofAttestation *proof_out);

/*
 * Verifies the Watertight Topological Fidelity Theorem (No T-Junctions) between two
 * adjacent meshlets: every shared seam vertex (FLOW_BMF_VERT_SEAM_LOCKED) with matching
 * reference position decodes to the exact same integer lattice point and float position.
 */
int flow_gpu_verify_watertight_seam(const FlowBmfMeshlet *meshlet_a,
                                    const FlowBmfMeshlet *meshlet_b, size_t *out_shared_count);

/* ============================================================================
 * 5. 64-Lane Subgroup Ballot & 1-Cycle POPCNT/CTZ Stream Compaction
 * ============================================================================
 * Folds visibility of 64 meshlets into a single `uint64_t` register (equivalent to
 * Metal `simd_ballot` / CUDA `__ballot_sync`), then computes compacted output slots
 * in 1 cycle via hardware `__builtin_popcountll` and `__builtin_ctzll`.
 */
typedef struct {
    float view_pos[3];
    float view_dir[3]; /* Unit view direction vector (camera -> scene) */
    float frustum_min[3];
    float frustum_max[3];
    uint64_t required_sieve_mask;
} FlowGpuCullCamera;

/*
 * Evaluates a single meshlet against the camera frustum, 2-Jet normal cone, and Topos sieve.
 */
uint64_t flow_gpu_evaluate_meshlet_sieve(const FlowBmfMeshlet *meshlet,
                                         const FlowGpuCullCamera *camera);

/*
 * Evaluates up to 64 meshlets in a single wave and folds their visibility into a 64-bit ballot
 * mask.
 */
uint64_t flow_gpu_ballot_cull_wave64(const FlowBmfMeshlet *meshlets, size_t count,
                                     const FlowGpuCullCamera *camera,
                                     uint64_t out_lane_sieves[FLOW_BMF_WAVE_SIZE]);

/*
 * 1-cycle subgroup prefix-sum: returns the compacted write offset for `lane_id` in [0, 63]
 * using hardware POPCNT on lower lane bits.
 */
static inline uint32_t flow_gpu_ballot_lane_offset(uint64_t ballot_mask, uint32_t lane_id) {
    if (lane_id == 0U)
        return 0U;
    uint64_t lower_mask = (lane_id >= 64U) ? UINT64_MAX : ((1ULL << lane_id) - 1ULL);
    return (uint32_t)__builtin_popcountll(ballot_mask & lower_mask);
}

/*
 * Total number of visible meshlets in the 64-lane wave via single hardware POPCNT.
 */
static inline uint32_t flow_gpu_ballot_visible_count(uint64_t ballot_mask) {
    return (uint32_t)__builtin_popcountll(ballot_mask);
}

/*
 * Branchless stream compaction using hardware trailing-zeros count (__builtin_ctzll):
 * writes the compact list of visible meshlet handles to `out_visible_handles` and returns
 * the number of visible meshlets written.
 */
uint32_t flow_gpu_ballot_compact_wave64(uint64_t ballot_mask, uint32_t base_meshlet_handle,
                                        uint32_t out_visible_handles[FLOW_BMF_WAVE_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_GPU_H */
