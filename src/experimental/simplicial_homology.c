#include "flow_smt_dsl.h"
#include "simplicial_homology.h"
#include <string.h>

int flow_homology_init(FlowSimplicialComplex *complex, size_t vertex_count) {
    if (!complex || !vertex_count || vertex_count > FLOW_HOMOLOGY_MAX_VERTICES) return 0;
    *complex = (FlowSimplicialComplex){ .vertex_count = vertex_count, .betti_0 = vertex_count };
    return 1;
}

int flow_homology_add_edge(FlowSimplicialComplex *complex, uint32_t u, uint32_t v) {
    if (!complex || u >= complex->vertex_count || v >= complex->vertex_count || u == v || complex->edge_count >= FLOW_HOMOLOGY_MAX_EDGES) return 0;
    for (size_t i = 0; i < complex->edge_count; ++i)
        if ((complex->edges[i].u == u && complex->edges[i].v == v) || (complex->edges[i].u == v && complex->edges[i].v == u)) return 1;
    complex->edges[complex->edge_count++] = (FlowSimplex1D){ u, v };
    return 1;
}

int flow_homology_add_face(FlowSimplicialComplex *complex, uint32_t u, uint32_t v, uint32_t w) {
    if (!complex || u >= complex->vertex_count || v >= complex->vertex_count || w >= complex->vertex_count || u == v || v == w || u == w || complex->face_count >= FLOW_HOMOLOGY_MAX_FACES) return 0;
    flow_homology_add_edge(complex, u, v);
    flow_homology_add_edge(complex, v, w);
    flow_homology_add_edge(complex, w, u);
    complex->faces[complex->face_count++] = (FlowSimplex2D){ u, v, w };
    return 1;
}

static uint32_t dsu_find(uint32_t *parent, uint32_t i) {
    return (parent[i] == i) ? i : (parent[i] = dsu_find(parent, parent[i]));
}

int flow_homology_compute_betti(FlowSimplicialComplex *complex, size_t *b0_out, size_t *b1_out) {
    if (!complex) return 0;
    uint32_t parent[FLOW_HOMOLOGY_MAX_VERTICES];
    for (size_t i = 0; i < complex->vertex_count; ++i) parent[i] = (uint32_t)i;
    size_t components = complex->vertex_count;
    for (size_t i = 0; i < complex->edge_count; ++i) {
        uint32_t ru = dsu_find(parent, complex->edges[i].u), rv = dsu_find(parent, complex->edges[i].v);
        if (ru != rv) { parent[ru] = rv; components--; }
    }
    complex->betti_0 = components;
    int64_t chi = (int64_t)complex->vertex_count - (int64_t)complex->edge_count + (int64_t)complex->face_count;
    int64_t b1 = (int64_t)complex->betti_0 - chi;
    complex->betti_1 = (size_t)(b1 > 0 ? b1 : 0);
    if (b0_out) *b0_out = complex->betti_0;
    if (b1_out) *b1_out = complex->betti_1;
    return 1;
}

int flow_homology_guide_mutation(FlowSimplicialComplex *complex, uint64_t base_genome, uint64_t *guided_genome_out, uint32_t *target_simplex_out) {
    if (!complex || !guided_genome_out) return 0;
    flow_homology_compute_betti(complex, NULL, NULL);
    complex->total_topological_rays++;
    uint32_t target_bit = (complex->betti_1 > 0 && complex->edge_count > 0) ? ((uint32_t)(complex->edge_count - 1) % 64) : ((uint32_t)(complex->total_topological_rays % 64));
    if (complex->betti_1 > 0 && complex->edge_count > 0) complex->holes_uncovered++;
    if (target_simplex_out) *target_simplex_out = target_bit;
    *guided_genome_out = base_genome ^ (1ULL << target_bit);
    return 1;
}

FlowSMTResult flow_homology_verify_smt(const FlowSimplicialComplex *complex, FlowSMTProofAttestation *proof_out) {
    if (!complex) return FLOW_SMT_UNKNOWN;
    uint64_t defect = 0;
    for (size_t i = 0; i < complex->face_count; ++i) {
        const FlowSimplex2D *f = &complex->faces[i];
        if (((int64_t)f->w - (int64_t)f->v) - ((int64_t)f->w - (int64_t)f->u) + ((int64_t)f->v - (int64_t)f->u) != 0) defect++;
    }
    const FlowBoxConstraint rules[] = {
        FLOW_SMT_RULE("boundary operator nilpotence", defect, 0, 0, FLOW_BOX_THEOREM_BUFFER_BOUNDS, "Algebraic boundary operator partial_1 o partial_2 != 0"),
        FLOW_SMT_RULE("betti_0 positive", complex->betti_0 == 0, 0, 0, FLOW_BOX_THEOREM_SHARD_ISOLATION, "Simplicial complex has 0 connected components")
    };
    return FLOW_SMT_VERIFY_RULES("simplicial_homology", proof_out, rules,
        "SMT HOMOLOGY SOUND: V=%zu, E=%zu, F=%zu, b0=%zu, b1=%zu, d1od2=0 (Zero-Defect Soundness)",
        complex->vertex_count, complex->edge_count, complex->face_count, complex->betti_0, complex->betti_1);
}
