#include "flowy_fvec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("==================================================================================\n");
    printf("  🧪 Running .fvec Binary Format, 1024-B Header & CRC32 Integrity Tests\n");
    printf("==================================================================================\n");

    const char *test_path = "/tmp/test_model.fvec";

    /* 1. Construct Header & Payload */
    FlowVecHeader hdr_in;
    memset(&hdr_in, 0, sizeof(hdr_in));
    strncpy(hdr_in.magic, FLOW_FVEC_MAGIC, sizeof(hdr_in.magic) - 1);
    strncpy(hdr_in.id, "vec_hft_ultra_low_latency", sizeof(hdr_in.id) - 1);
    strncpy(hdr_in.name, "HFT Ultra Low-Latency Lock-Free Queue", sizeof(hdr_in.name) - 1);
    strncpy(hdr_in.origin_hardware, "x86_avx2, L1=64K, Cores=64", sizeof(hdr_in.origin_hardware) - 1);
    strncpy(hdr_in.trigger_intent, "HFT_TRADING", sizeof(hdr_in.trigger_intent) - 1);
    strncpy(hdr_in.category, "HFT", sizeof(hdr_in.category) - 1);
    strncpy(hdr_in.component_id, "bounded_queue", sizeof(hdr_in.component_id) - 1);
    strncpy(hdr_in.description, "Sub-15ns lock-free ring buffer for algorithmic trading.", sizeof(hdr_in.description) - 1);
    strncpy(hdr_in.smt_signature, "BUFFER_UNSAT:MEM_UNSAT:SHARD_UNSAT:DET_UNSAT", sizeof(hdr_in.smt_signature) - 1);
    hdr_in.energy_score = 12.50;
    hdr_in.created_at_unix = 1772590000;
    hdr_in.vector_dim = FLOW_VAULT_DIM;
    hdr_in.payload_size = sizeof(FlowVecPayload);

    FlowVecPayload payload_in;
    memset(&payload_in, 0, sizeof(payload_in));
    for (int i = 0; i < FLOW_VAULT_DIM; ++i) {
        payload_in.features[i] = (double)(i + 1) * 0.05;
    }
    payload_in.pure_genome = 0x000000b01a627c6bULL;
    payload_in.hard_composite_mask = 0x000000000000ffffULL;
    payload_in.soft_composite_bias = 0x0000000000000001ULL;
    payload_in.proof.buffer_bounds_safety = FLOW_SMT_PROVEN_UNSAT;
    payload_in.proof.memory_quota_bound = FLOW_SMT_PROVEN_UNSAT;
    payload_in.proof.shard_non_aliasing = FLOW_SMT_PROVEN_UNSAT;
    payload_in.proof.determinism_invariant = FLOW_SMT_PROVEN_UNSAT;

    /* 2. Test Header Serialization and Deserialization */
    char header_buf[FLOW_FVEC_HEADER_SIZE];
    assert(flow_fvec_header_serialize(&hdr_in, header_buf, sizeof(header_buf)));

    FlowVecHeader hdr_deser;
    assert(flow_fvec_header_deserialize(header_buf, sizeof(header_buf), &hdr_deser));
    assert(strcmp(hdr_deser.id, hdr_in.id) == 0);
    assert(strcmp(hdr_deser.trigger_intent, hdr_in.trigger_intent) == 0);
    assert(strcmp(hdr_deser.smt_signature, hdr_in.smt_signature) == 0);
    assert(hdr_deser.energy_score == hdr_in.energy_score);
    printf("  ✓ Header 1024-byte exact padded block serialization & deserialization sound.\n");

    /* 3. Test File Write and Read Roundtrip */
    assert(flow_fvec_write_file(test_path, &hdr_in, &payload_in));

    FlowVecHeader hdr_out;
    FlowVecPayload payload_out;
    assert(flow_fvec_read_file(test_path, &hdr_out, &payload_out));

    assert(strcmp(hdr_out.id, hdr_in.id) == 0);
    assert(strcmp(hdr_out.component_id, "bounded_queue") == 0);
    assert(payload_out.pure_genome == payload_in.pure_genome);
    assert(payload_out.hard_composite_mask == payload_in.hard_composite_mask);
    assert(payload_out.proof.buffer_bounds_safety == FLOW_SMT_PROVEN_UNSAT);
    printf("  ✓ File write and read roundtrip verified intact.\n");

    /* 4. Test CRC32 Corruption Detection */
    FILE *f = fopen(test_path, "r+b");
    assert(f != NULL);
    /* Seek to byte 1050 (inside binary payload) and flip bits */
    fseek(f, 1050, SEEK_SET);
    int c = fgetc(f);
    fseek(f, 1050, SEEK_SET);
    fputc(c ^ 0xFF, f);
    fclose(f);

    FlowVecHeader hdr_corrupt;
    FlowVecPayload payload_corrupt;
    int read_res = flow_fvec_read_file(test_path, &hdr_corrupt, &payload_corrupt);
    assert(read_res == 0); /* Must fail due to CRC32 mismatch! */
    printf("  ✓ Bit-tampering rejected via payload CRC32 checksum guard.\n");

    remove(test_path);
    printf("==================================================================================\n");
    printf("FVEC_FORMAT_TEST=passed header_1024b=verified binary_payload=sound crc32=enforced\n");
    return 0;
}
