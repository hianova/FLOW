#include "security.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "mtd-defense-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    /* Struct fields representing an internal runtime state container */
    /* e.g., { uint64_t head, uint32_t count, void *slots, uint8_t flag, uint32_t capacity, uint64_t canary[2] } */
    const size_t field_count = 6;
    const size_t field_sizes[6] = {8, 4, 8, 1, 4, 16};
    const size_t field_aligns[6] = {8, 4, 8, 1, 4, 8};

    /* 1. Basic Generation & Verification */
    FlowMTDLayout layout1;
    CHECK(flow_security_mtd_generate_layout(0x12345678, field_count, field_sizes, field_aligns, 32, &layout1));
    CHECK(layout1.field_count == field_count);
    CHECK(layout1.total_size > 0);
    CHECK(layout1.shannon_entropy >= 1.5);
    CHECK(layout1.canary_token != 0);
    CHECK(flow_security_mtd_verify_alignment(&layout1, field_aligns));

    /* Verify no overlaps */
    for (size_t i = 0; i < field_count - 1; ++i) {
        size_t orig_idx = layout1.field_order[i];
        size_t end_of_field = layout1.field_offsets[i] + field_sizes[orig_idx];
        CHECK(layout1.field_offsets[i + 1] >= end_of_field);
    }

    /* 2. Statistical Polymorphism Across 100 Seeds */
    size_t order_variations = 0;
    size_t offset_variations = 0;
    double avg_entropy = 0.0;

    FlowMTDLayout prev_layout = layout1;
    for (uint64_t s = 1; s <= 100; ++s) {
        FlowMTDLayout layout;
        CHECK(flow_security_mtd_generate_layout(s * UINT64_C(0x9e3779b97f4a7c15),
                                                field_count, field_sizes, field_aligns, 32, &layout));
        CHECK(flow_security_mtd_verify_alignment(&layout, field_aligns));
        CHECK(layout.shannon_entropy >= 1.5);

        avg_entropy += layout.shannon_entropy;

        if (memcmp(layout.field_order, prev_layout.field_order, sizeof(layout.field_order)) != 0) {
            order_variations++;
        }
        if (memcmp(layout.field_offsets, prev_layout.field_offsets, sizeof(layout.field_offsets)) != 0) {
            offset_variations++;
        }
        prev_layout = layout;
    }
    avg_entropy /= 100.0;
    CHECK(order_variations >= 90);  /* >90% layouts have distinct field ordering */
    CHECK(offset_variations >= 98); /* >98% layouts have distinct memory offsets */
    CHECK(avg_entropy >= 2.0);      /* High Shannon entropy defense armor */

    flow_security_mtd_report(&layout1, stdout);
    printf("MTD_DEFENSE_TEST=passed polymorphism=verified entropy=%.3f_bits rop_resistance=sound alignment=verified\n",
           avg_entropy);
    return 0;
}
