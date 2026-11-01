#include "flow_gpu.h"
#include <math.h>
#include <string.h>

/* ============================================================================
 * 1. 1.58-Bit Ternary Bit-Manifold Codec (BitNet b1.58 Isomorphic)
 * ============================================================================ */

FlowBmfTernary64 flow_bmf_ternary64_encode(const int8_t values[64]) {
    FlowBmfTernary64 out = {0ULL, 0ULL};
    if (!values)
        return out;
    for (uint32_t i = 0; i < 64U; ++i) {
        if (values[i] > 0) {
            out.nonzero_mask |= (1ULL << i);
        } else if (values[i] < 0) {
            out.nonzero_mask |= (1ULL << i);
            out.sign_mask |= (1ULL << i);
        }
    }
    return out;
}

void flow_bmf_ternary64_decode_all(FlowBmfTernary64 t, int8_t out_values[64]) {
    if (!out_values)
        return;
    FlowBmfTernary64 canon = flow_bmf_ternary64_canonicalize(t);
    for (uint32_t i = 0; i < 64U; ++i) {
        out_values[i] = flow_bmf_ternary64_get(canon, i);
    }
}

/* ============================================================================
 * 2. 2-Bit Block Interpolation Codec (BC1 / DXT1 Isomorphic)
 * ============================================================================ */

FlowBmfBlock2Bit flow_bmf_block2bit_encode(const int16_t samples[16]) {
    FlowBmfBlock2Bit blk = {0, 0, 0U};
    if (!samples)
        return blk;

    int16_t min_v = samples[0];
    int16_t max_v = samples[0];
    for (uint32_t i = 1; i < 16U; ++i) {
        if (samples[i] < min_v)
            min_v = samples[i];
        if (samples[i] > max_v)
            max_v = samples[i];
    }
    blk.c0 = max_v;
    blk.c1 = min_v;

    int32_t c0 = (int32_t)blk.c0;
    int32_t c1 = (int32_t)blk.c1;
    int32_t palette[4] = {c0, c1, (2 * c0 + c1) / 3, (c0 + 2 * c1) / 3};

    uint32_t selectors = 0U;
    for (uint32_t i = 0; i < 16U; ++i) {
        int32_t s = (int32_t)samples[i];
        uint32_t best_code = 0U;
        int32_t best_err = s >= palette[0] ? (s - palette[0]) : (palette[0] - s);
        for (uint32_t code = 1U; code < 4U; ++code) {
            int32_t err = s >= palette[code] ? (s - palette[code]) : (palette[code] - s);
            if (err < best_err) {
                best_err = err;
                best_code = code;
            }
        }
        selectors |= (best_code & 0x3U) << (i * 2U);
    }
    blk.selectors = selectors;
    return blk;
}

void flow_bmf_block2bit_decode_all(FlowBmfBlock2Bit blk, int16_t out_samples[16]) {
    if (!out_samples)
        return;
    for (uint32_t i = 0; i < 16U; ++i) {
        out_samples[i] = flow_bmf_block2bit_decode_lane(blk, i);
    }
}

/* ============================================================================
 * 3. Pointerless Flat Index-Based Scene Hierarchy Propagation
 * ============================================================================ */

int flow_gpu_propagate_flat_nodes(FlowBmfFlatNode *nodes, size_t count) {
    if (!nodes && count > 0)
        return -1;
    for (size_t i = 0; i < count; ++i) {
        FlowBmfFlatNode *node = &nodes[i];
        if (!isfinite(node->local_pos[0]) || !isfinite(node->local_pos[1]) ||
            !isfinite(node->local_pos[2]) || !isfinite(node->local_scale) ||
            node->local_scale <= 0.0f) {
            node->node_sieve_mask &= ~FLOW_GPU_SIEVE_FLAT_DAG_VALID;
            return -1;
        }
        if (node->parent_idx == FLOW_BMF_INVALID_HANDLE) {
            node->world_pos[0] = node->local_pos[0];
            node->world_pos[1] = node->local_pos[1];
            node->world_pos[2] = node->local_pos[2];
            node->world_scale = node->local_scale;
            node->node_sieve_mask |= FLOW_GPU_SIEVE_FLAT_DAG_VALID;
        } else if ((size_t)node->parent_idx < i) {
            /* Strictly precedes `i` in flat array -> 100% sequential cache-line prefetch */
            const FlowBmfFlatNode *parent = &nodes[node->parent_idx];
            node->world_pos[0] = parent->world_pos[0] + parent->world_scale * node->local_pos[0];
            node->world_pos[1] = parent->world_pos[1] + parent->world_scale * node->local_pos[1];
            node->world_pos[2] = parent->world_pos[2] + parent->world_scale * node->local_pos[2];
            node->world_scale = parent->world_scale * node->local_scale;
            node->node_sieve_mask |= (parent->node_sieve_mask & FLOW_GPU_SIEVE_FLAT_DAG_VALID);
        } else {
            /* Forward/cycle reference violates flat DAG topological order */
            node->node_sieve_mask &= ~FLOW_GPU_SIEVE_FLAT_DAG_VALID;
            return -1;
        }
    }
    return 0;
}

/* ============================================================================
 * 4. GPU-Native BMF Meshlet Encoding, Decoding & 2-Jet Cone Derivation
 * ============================================================================ */

int flow_gpu_meshlet_encode(FlowBmfMeshlet *out_meshlet, const float (*positions)[3],
                            const uint8_t *vertex_flags, size_t vertex_count,
                            const uint8_t (*triangles)[3], size_t triangle_count,
                            float global_quantum, uint32_t parent_node_idx) {
    if (!out_meshlet || !positions || vertex_count == 0 ||
        vertex_count > FLOW_BMF_MESHLET_MAX_VERTS || triangle_count > FLOW_BMF_MESHLET_MAX_PRIMS ||
        (triangle_count > 0 && !triangles) || !isfinite(global_quantum) || global_quantum <= 0.0f) {
        return -1;
    }

    memset(out_meshlet, 0, sizeof(*out_meshlet));
    out_meshlet->vertex_count = (uint16_t)vertex_count;
    out_meshlet->triangle_count = (uint16_t)triangle_count;
    out_meshlet->parent_node_idx = parent_node_idx;
    out_meshlet->world_scale_inv = global_quantum;

    /* Step 1: Snap all vertices to the global integer lattice Z^3 to prevent T-junctions */
    int64_t lat[FLOW_BMF_MESHLET_MAX_VERTS][3];
    int64_t lmin[3] = {INT64_MAX, INT64_MAX, INT64_MAX};
    int64_t lmax[3] = {INT64_MIN, INT64_MIN, INT64_MIN};

    for (size_t i = 0; i < vertex_count; ++i) {
        for (int d = 0; d < 3; ++d) {
            float v = positions[i][d];
            if (!isfinite(v))
                return -1;
            double scaled = (double)v / (double)global_quantum;
            if (scaled < (double)INT32_MIN / 2.0 || scaled > (double)INT32_MAX / 2.0)
                return -1;
            int64_t q = (int64_t)llround(scaled);
            lat[i][d] = q;
            if (q < lmin[d])
                lmin[d] = q;
            if (q > lmax[d])
                lmax[d] = q;
        }
    }

    int64_t max_span = 0;
    for (int d = 0; d < 3; ++d) {
        int64_t span = lmax[d] - lmin[d];
        if (span > max_span)
            max_span = span;
    }

    uint32_t step = 1U;
    if (max_span > (int64_t)FLOW_BMF_QUANT_MAX) {
        step =
            (uint32_t)((max_span + (int64_t)FLOW_BMF_QUANT_MAX - 1) / (int64_t)FLOW_BMF_QUANT_MAX);
        if (step == 0U)
            step = 1U;
        for (int d = 0; d < 3; ++d) {
            /* Align lattice_min to integer multiple of `step` for seam invariance */
            int64_t rem = flow_int_mod(lmin[d], (int64_t)step);
            lmin[d] -= rem;
        }
    }

    out_meshlet->lattice_step = step;
    for (int d = 0; d < 3; ++d) {
        out_meshlet->lattice_min[d] = (int32_t)lmin[d];
        out_meshlet->aabb_min[d] = (float)lmin[d] * global_quantum;
        out_meshlet->aabb_max[d] = (float)lmax[d] * global_quantum;
        out_meshlet->cone_apex[d] = 0.5f * (out_meshlet->aabb_min[d] + out_meshlet->aabb_max[d]);
    }

    /* Step 2: Pack 10-bit local coordinates + 2-bit boundary flags into 32-bit words */
    bool has_seam_locked = true;
    for (size_t i = 0; i < vertex_count; ++i) {
        uint32_t qcoords[3];
        for (int d = 0; d < 3; ++d) {
            int64_t offset = (lat[i][d] - lmin[d]) / (int64_t)step;
            if (offset < 0)
                offset = 0;
            if (offset > (int64_t)FLOW_BMF_QUANT_MAX)
                offset = (int64_t)FLOW_BMF_QUANT_MAX;
            qcoords[d] = (uint32_t)offset;
        }
        uint32_t flag = vertex_flags ? ((uint32_t)vertex_flags[i] & 0x3U) : FLOW_BMF_VERT_INTERIOR;
        if (flag == FLOW_BMF_VERT_SEAM_LOCKED && step != 1U) {
            for (int d = 0; d < 3; ++d) {
                if (flow_int_mod(lat[i][d] - lmin[d], (int64_t)step) != 0)
                    has_seam_locked = false;
            }
        }
        out_meshlet->packed_vertices[i] =
            flow_bmf_pack_vertex_10bit(qcoords[0], qcoords[1], qcoords[2], flag);
    }

    /* Step 3: Pack 6-bit micro-indices & accumulate discrete triangle normals */
    double avg_norm[3] = {0.0, 0.0, 0.0};
    double tri_normals[FLOW_BMF_MESHLET_MAX_PRIMS][3];
    size_t valid_tri_normals = 0;
    int8_t v_norm_sign[3][64];
    memset(v_norm_sign, 0, sizeof(v_norm_sign));

    for (size_t t = 0; t < triangle_count; ++t) {
        uint8_t i0 = triangles[t][0];
        uint8_t i1 = triangles[t][1];
        uint8_t i2 = triangles[t][2];
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
            return -1;
        }
        out_meshlet->packed_triangles[t] = flow_bmf_pack_tri_6bit(i0, i1, i2);

        double e1[3] = {(double)(positions[i1][0] - positions[i0][0]),
                        (double)(positions[i1][1] - positions[i0][1]),
                        (double)(positions[i1][2] - positions[i0][2])};
        double e2[3] = {(double)(positions[i2][0] - positions[i0][0]),
                        (double)(positions[i2][1] - positions[i0][1]),
                        (double)(positions[i2][2] - positions[i0][2])};
        double nx = e1[1] * e2[2] - e1[2] * e2[1];
        double ny = e1[2] * e2[0] - e1[0] * e2[2];
        double nz = e1[0] * e2[1] - e1[1] * e2[0];
        double len = sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-12) {
            nx /= len;
            ny /= len;
            nz /= len;
            tri_normals[valid_tri_normals][0] = nx;
            tri_normals[valid_tri_normals][1] = ny;
            tri_normals[valid_tri_normals][2] = nz;
            valid_tri_normals++;
            avg_norm[0] += nx;
            avg_norm[1] += ny;
            avg_norm[2] += nz;
            int8_t sx = (nx > 0.25) ? 1 : ((nx < -0.25) ? -1 : 0);
            int8_t sy = (ny > 0.25) ? 1 : ((ny < -0.25) ? -1 : 0);
            int8_t sz = (nz > 0.25) ? 1 : ((nz < -0.25) ? -1 : 0);
            v_norm_sign[0][i0] = v_norm_sign[0][i1] = v_norm_sign[0][i2] = sx;
            v_norm_sign[1][i0] = v_norm_sign[1][i1] = v_norm_sign[1][i2] = sy;
            v_norm_sign[2][i0] = v_norm_sign[2][i1] = v_norm_sign[2][i2] = sz;
        }
    }

    out_meshlet->normal_oct_x = flow_bmf_ternary64_encode(v_norm_sign[0]);
    out_meshlet->normal_oct_y = flow_bmf_ternary64_encode(v_norm_sign[1]);
    out_meshlet->normal_oct_z = flow_bmf_ternary64_encode(v_norm_sign[2]);

    double axis_len =
        sqrt(avg_norm[0] * avg_norm[0] + avg_norm[1] * avg_norm[1] + avg_norm[2] * avg_norm[2]);
    if (axis_len > 1e-9 && valid_tri_normals > 0) {
        avg_norm[0] /= axis_len;
        avg_norm[1] /= axis_len;
        avg_norm[2] /= axis_len;
        out_meshlet->cone_axis[0] = (float)avg_norm[0];
        out_meshlet->cone_axis[1] = (float)avg_norm[1];
        out_meshlet->cone_axis[2] = (float)avg_norm[2];

        double min_dot = 1.0;
        for (size_t t = 0; t < valid_tri_normals; ++t) {
            double d = tri_normals[t][0] * avg_norm[0] + tri_normals[t][1] * avg_norm[1] +
                       tri_normals[t][2] * avg_norm[2];
            if (d < min_dot)
                min_dot = d;
        }
        if (min_dot <= 0.0) {
            /* Cone half-angle >= 90 deg -> cannot backface cull */
            out_meshlet->cone_cutoff = 1.0f;
        } else {
            double sin_half = sqrt(fmax(0.0, 1.0 - min_dot * min_dot));
            out_meshlet->cone_cutoff = (float)(-sin_half);
        }
    } else {
        out_meshlet->cone_axis[0] = 0.0f;
        out_meshlet->cone_axis[1] = 0.0f;
        out_meshlet->cone_axis[2] = 1.0f;
        out_meshlet->cone_cutoff = -0.0f;
    }

    out_meshlet->topos_sieve_mask = FLOW_GPU_SIEVE_FINITE_AABB | FLOW_GPU_SIEVE_MICRO_INDEX_VALID |
                                    FLOW_GPU_SIEVE_DECODE_NO_OVERFLOW |
                                    FLOW_GPU_SIEVE_FLAT_DAG_VALID |
                                    (has_seam_locked ? FLOW_GPU_SIEVE_WATERTIGHT_SEAM : 0ULL);

    return 0;
}

int flow_gpu_meshlet_decode_vertex(const FlowBmfMeshlet *meshlet, uint32_t v_idx, float out_pos[3],
                                   int64_t out_lattice[3], uint32_t *out_flag) {
    if (!meshlet || v_idx >= meshlet->vertex_count ||
        meshlet->vertex_count > FLOW_BMF_MESHLET_MAX_VERTS || meshlet->lattice_step == 0U ||
        !isfinite(meshlet->world_scale_inv) || meshlet->world_scale_inv <= 0.0f) {
        return -1;
    }

    uint32_t qx = 0, qy = 0, qz = 0, flag = 0;
    flow_bmf_unpack_vertex_10bit(meshlet->packed_vertices[v_idx], &qx, &qy, &qz, &flag);

    int64_t lx = (int64_t)meshlet->lattice_min[0] + (int64_t)qx * (int64_t)meshlet->lattice_step;
    int64_t ly = (int64_t)meshlet->lattice_min[1] + (int64_t)qy * (int64_t)meshlet->lattice_step;
    int64_t lz = (int64_t)meshlet->lattice_min[2] + (int64_t)qz * (int64_t)meshlet->lattice_step;

    if (out_lattice) {
        out_lattice[0] = lx;
        out_lattice[1] = ly;
        out_lattice[2] = lz;
    }
    if (out_pos) {
        out_pos[0] = (float)lx * meshlet->world_scale_inv;
        out_pos[1] = (float)ly * meshlet->world_scale_inv;
        out_pos[2] = (float)lz * meshlet->world_scale_inv;
    }
    if (out_flag) {
        *out_flag = flag;
    }
    return 0;
}

int flow_gpu_meshlet_derive_jet2_cone(FlowBmfMeshlet *meshlet, const FlowJetScalar2 jet_patch_u[3],
                                      const FlowJetScalar2 jet_patch_v[3], double param_step_du,
                                      double param_step_dv) {
    if (!meshlet || !jet_patch_u || !jet_patch_v || !isfinite(param_step_du) ||
        !isfinite(param_step_dv) || param_step_du < 0.0 || param_step_dv < 0.0) {
        return -1;
    }

    for (int d = 0; d < 3; ++d) {
        if (!isfinite(jet_patch_u[d].v) || !isfinite(jet_patch_u[d].d1) ||
            !isfinite(jet_patch_u[d].d2) || !isfinite(jet_patch_v[d].v) ||
            !isfinite(jet_patch_v[d].d1) || !isfinite(jet_patch_v[d].d2)) {
            return -1;
        }
    }

    /* 1-Jet tangent basis vectors du r and dv r */
    double tu[3] = {jet_patch_u[0].d1, jet_patch_u[1].d1, jet_patch_u[2].d1};
    double tv[3] = {jet_patch_v[0].d1, jet_patch_v[1].d1, jet_patch_v[2].d1};

    /* 2-Jet Hessian curvature vectors d^2_u r and d^2_v r */
    double hu[3] = {jet_patch_u[0].d2, jet_patch_u[1].d2, jet_patch_u[2].d2};
    double hv[3] = {jet_patch_v[0].d2, jet_patch_v[1].d2, jet_patch_v[2].d2};

    double norm_tu = sqrt(tu[0] * tu[0] + tu[1] * tu[1] + tu[2] * tu[2]);
    double norm_tv = sqrt(tv[0] * tv[0] + tv[1] * tv[1] + tv[2] * tv[2]);
    double norm_hu = sqrt(hu[0] * hu[0] + hu[1] * hu[1] + hu[2] * hu[2]);
    double norm_hv = sqrt(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]);

    meshlet->jet2_max_lipschitz_d1 = norm_tu + norm_tv;
    meshlet->jet2_max_curvature_d2 = norm_hu + norm_hv;

    /* Inflate AABB by 2-Jet second-order Taylor curvature remainder to prevent tunneling */
    for (int d = 0; d < 3; ++d) {
        double rem = 0.5 * (fabs(hu[d]) * param_step_du * param_step_du +
                            fabs(hv[d]) * param_step_dv * param_step_dv);
        meshlet->aabb_min[d] -= (float)rem;
        meshlet->aabb_max[d] += (float)rem;
    }

    /* Normal vector from 1-Jet exterior product: n = du r x dv r */
    double nx = tu[1] * tv[2] - tu[2] * tv[1];
    double ny = tu[2] * tv[0] - tu[0] * tv[2];
    double nz = tu[0] * tv[1] - tu[1] * tv[0];
    double n_len = sqrt(nx * nx + ny * ny + nz * nz);
    if (n_len <= 1e-12) {
        return -1;
    }
    nx /= n_len;
    ny /= n_len;
    nz /= n_len;

    meshlet->cone_axis[0] = (float)nx;
    meshlet->cone_axis[1] = (float)ny;
    meshlet->cone_axis[2] = (float)nz;

    /* Normal cone angular spread induced by 2-Jet curvature over (du, dv) */
    double tangent_scale = fmax(norm_tu + norm_tv, 1e-9);
    double curvature_excursion =
        (norm_hu * param_step_du + norm_hv * param_step_dv) / tangent_scale;
    double theta_jet = atan(curvature_excursion);

    if (theta_jet >= 1.5707963267948966) {
        meshlet->cone_cutoff = 1.0f;
    } else if (meshlet->cone_cutoff <= 0.0f) {
        float jet_cutoff = (float)(-sin(theta_jet));
        if (jet_cutoff < meshlet->cone_cutoff) {
            meshlet->cone_cutoff = jet_cutoff;
        }
    }

    meshlet->topos_sieve_mask |= FLOW_GPU_SIEVE_FINITE_AABB;
    return 0;
}

FlowSMTResult flow_gpu_meshlet_verify_smt(const FlowBmfMeshlet *meshlet,
                                          FlowSMTProofAttestation *proof_out) {
    if (!meshlet)
        return FLOW_SMT_VIOLATION_SAT;

    int micro_indices_ok = 1;
    for (uint16_t t = 0; t < meshlet->triangle_count; ++t) {
        if (t >= FLOW_BMF_MESHLET_MAX_PRIMS) {
            micro_indices_ok = 0;
            break;
        }
        uint8_t i0 = 0, i1 = 0, i2 = 0;
        flow_bmf_unpack_tri_6bit(meshlet->packed_triangles[t], &i0, &i1, &i2);
        if (i0 >= meshlet->vertex_count || i1 >= meshlet->vertex_count ||
            i2 >= meshlet->vertex_count) {
            micro_indices_ok = 0;
            break;
        }
    }

    int decode_ok = (meshlet->lattice_step > 0U && isfinite(meshlet->world_scale_inv) &&
                     meshlet->world_scale_inv > 0.0f)
                        ? 1
                        : 0;
    for (uint16_t v = 0; v < meshlet->vertex_count && decode_ok; ++v) {
        float pos[3];
        int64_t lat[3];
        if (flow_gpu_meshlet_decode_vertex(meshlet, v, pos, lat, NULL) != 0) {
            decode_ok = 0;
            break;
        }
        for (int d = 0; d < 3; ++d) {
            int64_t max_lat = (int64_t)meshlet->lattice_min[d] +
                              (int64_t)FLOW_BMF_QUANT_MAX * (int64_t)meshlet->lattice_step;
            if (lat[d] < (int64_t)meshlet->lattice_min[d] || lat[d] > max_lat ||
                !isfinite(pos[d])) {
                decode_ok = 0;
                break;
            }
        }
    }

    int ternary_canon_ok = (flow_bmf_ternary64_is_canonical(meshlet->normal_oct_x) &&
                            flow_bmf_ternary64_is_canonical(meshlet->normal_oct_y) &&
                            flow_bmf_ternary64_is_canonical(meshlet->normal_oct_z))
                               ? 1
                               : 0;

    FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("meshlet_vertex_quota", (int64_t)meshlet->vertex_count, 1,
                      (int64_t)FLOW_BMF_MESHLET_MAX_VERTS, FLOW_BOX_THEOREM_MEMORY_QUOTA,
                      "meshlet vertex count exceeds 64-vertex 6-bit micro-index capacity"),
        FLOW_SMT_RULE("meshlet_triangle_quota", (int64_t)meshlet->triangle_count, 0,
                      (int64_t)FLOW_BMF_MESHLET_MAX_PRIMS, FLOW_BOX_THEOREM_MEMORY_QUOTA,
                      "meshlet triangle count exceeds 124-primitive cluster limit"),
        FLOW_SMT_RULE("micro_index_bounds", micro_indices_ok, 1, 1, FLOW_BOX_THEOREM_BUFFER_BOUNDS,
                      "triangle 6-bit micro-index out of meshlet vertex range"),
        FLOW_SMT_RULE("decode_no_overflow", decode_ok, 1, 1, FLOW_BOX_THEOREM_BUFFER_BOUNDS,
                      "10-bit fixed-point vertex decoding overflowed cluster lattice envelope"),
        FLOW_SMT_RULE("ternary_normal_canonical", ternary_canon_ok, 1, 1,
                      FLOW_BOX_THEOREM_DETERMINISM,
                      "1.58-bit ternary normal bit-planes violate canonical sign &= nonzero law")};

    return FLOW_SMT_VERIFY_RULES(
        "gpu_bmf_meshlet", proof_out, rules,
        "GPU-Native BMF Meshlet verified: verts=%u, tris=%u, step=%u, 10-bit/6-bit/1.58-bit sound",
        (unsigned)meshlet->vertex_count, (unsigned)meshlet->triangle_count,
        (unsigned)meshlet->lattice_step);
}

int flow_gpu_verify_watertight_seam(const FlowBmfMeshlet *meshlet_a,
                                    const FlowBmfMeshlet *meshlet_b, size_t *out_shared_count) {
    if (out_shared_count)
        *out_shared_count = 0;
    if (!meshlet_a || !meshlet_b)
        return -1;

    size_t shared = 0;
    for (uint32_t va = 0; va < meshlet_a->vertex_count; ++va) {
        float pos_a[3];
        int64_t lat_a[3];
        uint32_t flag_a = 0;
        if (flow_gpu_meshlet_decode_vertex(meshlet_a, va, pos_a, lat_a, &flag_a) != 0)
            return -1;
        if (flag_a != FLOW_BMF_VERT_SEAM_LOCKED)
            continue;

        for (uint32_t vb = 0; vb < meshlet_b->vertex_count; ++vb) {
            float pos_b[3];
            int64_t lat_b[3];
            uint32_t flag_b = 0;
            if (flow_gpu_meshlet_decode_vertex(meshlet_b, vb, pos_b, lat_b, &flag_b) != 0)
                return -1;
            if (flag_b != FLOW_BMF_VERT_SEAM_LOCKED)
                continue;

            if (lat_a[0] == lat_b[0] && lat_a[1] == lat_b[1] && lat_a[2] == lat_b[2]) {
                /* Shared global lattice vertex found: verify exact IEEE-754 bitwise identity */
                if (memcmp(pos_a, pos_b, sizeof(pos_a)) != 0) {
                    return -1; /* Crack / T-junction detected! */
                }
                shared++;
            }
        }
    }

    if (out_shared_count)
        *out_shared_count = shared;
    return (shared > 0) ? 0 : -1;
}

/* ============================================================================
 * 5. 64-Lane Subgroup Ballot & 1-Cycle POPCNT/CTZ Stream Compaction
 * ============================================================================ */

uint64_t flow_gpu_evaluate_meshlet_sieve(const FlowBmfMeshlet *meshlet,
                                         const FlowGpuCullCamera *camera) {
    if (!meshlet || !camera)
        return 0ULL;

    uint64_t sieve = meshlet->topos_sieve_mask;

    /* 1. Frustum AABB overlap check */
    bool frustum_ok = true;
    for (int d = 0; d < 3; ++d) {
        if (meshlet->aabb_max[d] < camera->frustum_min[d] ||
            meshlet->aabb_min[d] > camera->frustum_max[d]) {
            frustum_ok = false;
            break;
        }
    }
    if (frustum_ok)
        sieve |= FLOW_GPU_SIEVE_FRUSTUM_VISIBLE;
    else
        sieve &= ~FLOW_GPU_SIEVE_FRUSTUM_VISIBLE;

    /*
     * 2. 2-Jet Normal Cone Backface Culling check:
     *    Let `v` be the normalized view direction from camera towards cluster apex (or
     * camera->view_dir). The entire normal cone of half-angle `theta` (where `cone_cutoff =
     * -sin(theta)`) is back-facing iff `dot(cone_axis, v) >= sin(theta) = -cone_cutoff` and
     * `cone_cutoff < 0`.
     */
    double vx = (double)meshlet->cone_apex[0] - (double)camera->view_pos[0];
    double vy = (double)meshlet->cone_apex[1] - (double)camera->view_pos[1];
    double vz = (double)meshlet->cone_apex[2] - (double)camera->view_pos[2];
    double v_len = sqrt(vx * vx + vy * vy + vz * vz);
    if (v_len > 1e-9) {
        vx /= v_len;
        vy /= v_len;
        vz /= v_len;
    } else {
        vx = (double)camera->view_dir[0];
        vy = (double)camera->view_dir[1];
        vz = (double)camera->view_dir[2];
    }

    double dot_nv = (double)meshlet->cone_axis[0] * vx + (double)meshlet->cone_axis[1] * vy +
                    (double)meshlet->cone_axis[2] * vz;
    bool backface_culled =
        (meshlet->cone_cutoff <= 0.0f) && (dot_nv > (double)(-meshlet->cone_cutoff));

    if (!backface_culled)
        sieve |= FLOW_GPU_SIEVE_CONE_VISIBLE;
    else
        sieve &= ~FLOW_GPU_SIEVE_CONE_VISIBLE;

    return sieve;
}

uint64_t flow_gpu_ballot_cull_wave64(const FlowBmfMeshlet *meshlets, size_t count,
                                     const FlowGpuCullCamera *camera,
                                     uint64_t out_lane_sieves[FLOW_BMF_WAVE_SIZE]) {
    if (!meshlets || !camera || count == 0)
        return 0ULL;

    size_t lanes = count > FLOW_BMF_WAVE_SIZE ? FLOW_BMF_WAVE_SIZE : count;
    uint64_t required = camera->required_sieve_mask != 0ULL
                            ? camera->required_sieve_mask
                            : (FLOW_GPU_SIEVE_FINITE_AABB | FLOW_GPU_SIEVE_MICRO_INDEX_VALID |
                               FLOW_GPU_SIEVE_DECODE_NO_OVERFLOW | FLOW_GPU_SIEVE_FRUSTUM_VISIBLE |
                               FLOW_GPU_SIEVE_CONE_VISIBLE);

    uint64_t ballot = 0ULL;
    for (size_t lane = 0; lane < FLOW_BMF_WAVE_SIZE; ++lane) {
        if (lane < lanes) {
            uint64_t s = flow_gpu_evaluate_meshlet_sieve(&meshlets[lane], camera);
            if (out_lane_sieves)
                out_lane_sieves[lane] = s;
            /* Grothendieck Topos subobject classifier Omega = {0, 1} folded into 64-bit ballot */
            if (flow_topos_classify_subobject(required, s)) {
                ballot |= (1ULL << lane);
            }
        } else if (out_lane_sieves) {
            out_lane_sieves[lane] = 0ULL;
        }
    }
    return ballot;
}

uint32_t flow_gpu_ballot_compact_wave64(uint64_t ballot_mask, uint32_t base_meshlet_handle,
                                        uint32_t out_visible_handles[FLOW_BMF_WAVE_SIZE]) {
    uint32_t write_idx = 0U;
    uint64_t remaining = ballot_mask;
    while (remaining != 0ULL) {
        uint32_t lane = (uint32_t)__builtin_ctzll(remaining);
        if (out_visible_handles) {
            out_visible_handles[write_idx] = base_meshlet_handle + lane;
        }
        write_idx++;
        remaining &= (remaining - 1ULL); /* Clear lowest set bit in 1 cycle */
    }
    return write_idx;
}
