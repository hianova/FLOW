#include "abi.h"
#include "bitspace.h"
#include "flow.h"
#include "registry.h"
#include "reload.h"
#include "search.h"
#include "verifier.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLICE_CHECK(cond) if (!(cond)) { fprintf(stderr, "vertical-slice-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; }

int main(void) {
    if (!flow_registry_init()) return 1;

    /* ===================================================================== */
    /* Step A: Parse .flow manifest and execute 1-bit search for Plan Gen 1  */
    /* ===================================================================== */
    FILE *spec_file = fopen("examples/rank.flow", "r");
    SLICE_CHECK(spec_file != NULL);
    FlowSpec spec;
    SLICE_CHECK(parse_spec(spec_file, &spec));
    fclose(spec_file);

    SemanticIR ir;
    lower_to_ir(&spec, &ir);

    SearchResult search_res_1 = search_best(&ir, 100, 42, 0, NULL);
    SLICE_CHECK(search_res_1.component != NULL);
    SLICE_CHECK(strcmp(search_res_1.component->id, "sharded_hash") == 0);

    FlowPlanArtifact artifact_gen1;
    SLICE_CHECK(flow_search_result_to_artifact(&ir, &search_res_1, &artifact_gen1));
    char val_err[256];
    SLICE_CHECK(flow_artifact_validate(&artifact_gen1, &ir, NULL, val_err, sizeof(val_err)));

    /* ===================================================================== */
    /* Step B: Build Zero-Copy Component ABI & Emit C/Rust/Python Adapters   */
    /* ===================================================================== */
    FlowComponentABI comp_abi;
    SLICE_CHECK(flow_abi_build_for_component(&ir, search_res_1.component, &comp_abi));
    SLICE_CHECK(comp_abi.thread_safe == 1);
    SLICE_CHECK(comp_abi.default_memory_mode == FLOW_MEM_RAW_VIEW);

    /* Emit C header */
    FILE *c_hdr = fopen("generated/rank_slice.h", "w");
    SLICE_CHECK(c_hdr != NULL);
    SLICE_CHECK(flow_abi_emit_c_header(c_hdr, &comp_abi));
    fclose(c_hdr);

    /* Emit Rust adapter */
    FILE *rs_fp = fopen("generated/rank_slice.rs", "w");
    SLICE_CHECK(rs_fp != NULL);
    SLICE_CHECK(flow_abi_emit_rust_adapter(rs_fp, &comp_abi));
    fclose(rs_fp);

    /* Emit Python adapter */
    FILE *py_fp = fopen("generated/rank_slice.py", "w");
    SLICE_CHECK(py_fp != NULL);
    SLICE_CHECK(flow_abi_emit_python_adapter(py_fp, &comp_abi));
    fclose(py_fp);

    /* ===================================================================== */
    /* Step C: Python Buffer (Writable & Readonly) Execution Verification     */
    /* ===================================================================== */
    /* Build shared library for Python ctypes wrapper */
    FILE *c_impl = fopen("generated/rank_slice_impl.c", "w");
    SLICE_CHECK(c_impl != NULL);
    fputs("#include \"rank_slice.h\"\n"
          "#include <stdint.h>\n"
          "#include <stdlib.h>\n"
          "#include <string.h>\n"
          "sharded_hash_Handle flow_sharded_hash_create(void) {\n"
          "    return (sharded_hash_Handle)malloc(64);\n"
          "}\n"
          "int32_t flow_sharded_hash_process(sharded_hash_Handle handle, const uint8_t *input, size_t input_len, uint8_t *output, size_t *output_len) {\n"
          "    (void)handle;\n"
          "    if (!input || input_len == 0) return -1;\n"
          "    if (output && output_len && *output_len >= input_len) {\n"
          "        memcpy(output, input, input_len);\n"
          "        *output_len = input_len;\n"
          "    }\n"
          "    return 0;\n"
          "}\n"
          "void flow_sharded_hash_destroy(sharded_hash_Handle handle) {\n"
          "    free(handle);\n"
          "}\n", c_impl);
    fclose(c_impl);

    int c_build = system("cc -shared -fPIC -Isrc -Igenerated generated/rank_slice_impl.c -o generated/libflow_rank.so");
    SLICE_CHECK(c_build == 0);

    /* Write Python test driver */
    FILE *py_test = fopen("generated/test_slice.py", "w");
    SLICE_CHECK(py_test != NULL);
    fputs("import sys\n"
          "sys.path.insert(0, 'generated')\n"
          "from rank_slice import ShardedHash\n"
          "\n"
          "with ShardedHash('generated/libflow_rank.so') as adapter:\n"
          "    # 1. Test writable bytearray buffer (Zero-Copy)\n"
          "    write_buf = bytearray(b'\\x01\\x02\\x03\\x04\\x05\\x06\\x07\\x08')\n"
          "    out_buf = bytearray(8)\n"
          "    written = adapter.process_view(write_buf, out_buf)\n"
          "    assert written == 8, f'expected written 8, got {written}'\n"
          "    assert out_buf == write_buf, f'expected matching buffer content'\n"
          "\n"
          "    # 2. Test readonly bytes buffer (Safe Fallback via buffer copy)\n"
          "    ro_buf = b'\\xAA\\xBB\\xCC\\xDD'\n"
          "    out_buf2 = bytearray(4)\n"
          "    written2 = adapter.process_view(ro_buf, out_buf2)\n"
          "    assert written2 == 4, f'expected written 4, got {written2}'\n"
          "    assert out_buf2 == ro_buf, f'expected matching buffer content'\n"
          "print('PYTHON_VERTICAL_SLICE=passed')\n", py_test);
    fclose(py_test);

    int py_res = system("python3 generated/test_slice.py");
    SLICE_CHECK(py_res == 0);

    /* ===================================================================== */
    /* Step D: Live Real Plan Activation & Hot Reload State Migration        */
    /* ===================================================================== */
    FlowReloadContext *context = flow_reload_create(NULL);
    SLICE_CHECK(context != NULL);
    FlowReloadReader reader;
    SLICE_CHECK(flow_reload_reader_register(context, &reader) == FLOW_RELOAD_OK);

    /* 1. Activate Plan Gen 1 */
    SLICE_CHECK(flow_reload_plan(context, &artifact_gen1, &ir, FLOW_MIGRATE_AUTO) == FLOW_RELOAD_OK);

    /* 2. Insert items into active generation via flow_reload_call */
    int item1 = 101, item2 = 202, item3 = 303;
    int count_out = 0;
    SLICE_CHECK(flow_reload_call(context, &reader, &item1, &count_out) == FLOW_RELOAD_OK);
    SLICE_CHECK(count_out == 1);
    SLICE_CHECK(flow_reload_call(context, &reader, &item2, &count_out) == FLOW_RELOAD_OK);
    SLICE_CHECK(count_out == 2);
    SLICE_CHECK(flow_reload_call(context, &reader, &item3, &count_out) == FLOW_RELOAD_OK);
    SLICE_CHECK(count_out == 3);

    /* 3. Search and construct Plan Gen 2 (upgraded plan) */
    SearchResult search_res_2 = search_best(&ir, 100, 999, 0, NULL);
    FlowPlanArtifact artifact_gen2;
    SLICE_CHECK(flow_search_result_to_artifact(&ir, &search_res_2, &artifact_gen2));

    /* 4. Live Hot Reload Migration from Gen 1 to Gen 2 */
    SLICE_CHECK(flow_reload_plan(context, &artifact_gen2, &ir, FLOW_MIGRATE_AUTO) == FLOW_RELOAD_OK);

    /* 5. Verify all 3 existing items are preserved across migration! */
    int item4 = 404;
    SLICE_CHECK(flow_reload_call(context, &reader, &item4, &count_out) == FLOW_RELOAD_OK);
    SLICE_CHECK(count_out == 4); /* 3 migrated + 1 newly added = 4 */

    SLICE_CHECK(flow_reload_reclaim(context) >= 1);
    SLICE_CHECK(flow_reload_reader_unregister(&reader) == FLOW_RELOAD_OK);
    SLICE_CHECK(flow_reload_destroy(context) == FLOW_RELOAD_OK);

    /* ===================================================================== */
    /* Step E: Rust Borrowed Slice (&[u8]) Contract Validation              */
    /* ===================================================================== */
    FILE *rs_check = fopen("generated/rank_slice.rs", "r");
    SLICE_CHECK(rs_check != NULL);
    char rs_line[256];
    int found_slice_sig = 0;
    while (fgets(rs_line, sizeof(rs_line), rs_check) != NULL) {
        if (strstr(rs_line, "pub fn process_slice") != NULL) {
            found_slice_sig = 1;
            break;
        }
    }
    fclose(rs_check);
    SLICE_CHECK(found_slice_sig);

    flow_ir_cleanup(&ir);

    printf("VERTICAL_SLICE_TEST=passed pipeline=[python_view,c_core,plan_reload,rust_slice] live_migration=verified items=4\n");
    return 0;
}
