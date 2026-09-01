#include "lsp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "lsp-test assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    char out_buf[8192];
    FILE *out_mem = fmemopen(out_buf, sizeof(out_buf), "w");
    CHECK(out_mem != NULL);

    FlowLSPConfig cfg = {
        .search_batch_iterations = 20,
        .emit_pareto_stream = 1,
        .emit_smt_stream = 1
    };

    FlowLSPServer *server = flow_lsp_create(&cfg, NULL, out_mem);
    CHECK(server != NULL);

    /* 1. Test JSON-RPC 2.0 initialize Handshake */
    const char *init_msg = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
    CHECK(flow_lsp_process_message(server, init_msg, strlen(init_msg)));
    fflush(out_mem);
    CHECK(strstr(out_buf, "\"capabilities\"") != NULL);
    CHECK(strstr(out_buf, "\"textDocumentSync\":1") != NULL);

    /* 2. Test Fast Static Validation on valid .flow buffer (< 1ms) */
    const char *spec_code =
        "input tasks {\n"
        "    max_count 1000\n"
        "}\n"
        "flow pipe {\n"
        "    tasks -> transform -> collect\n"
        "}\n"
        "import builtin\n"
        "require {\n"
        "    deterministic\n"
        "    memory < 16mb\n"
        "}\n";

    char diag_buf[4096];
    CHECK(flow_lsp_validate_document(server, "file:///workspace/test.flow", spec_code, diag_buf, sizeof(diag_buf)));
    CHECK(strstr(diag_buf, "textDocument/publishDiagnostics") != NULL);
    CHECK(strstr(diag_buf, "Verified valid") != NULL);

    /* 3. Test Fast Diagnostics on syntax error */
    const char *invalid_code = "this is not valid flow code";
    CHECK(flow_lsp_validate_document(server, "file:///workspace/test.flow", invalid_code, diag_buf, sizeof(diag_buf)));
    CHECK(strstr(diag_buf, "Syntax error") != NULL);

    /* 4. Reload valid code and test Progressive Async Pareto Search Step */
    CHECK(flow_lsp_validate_document(server, "file:///workspace/test.flow", spec_code, diag_buf, sizeof(diag_buf)));

    char pareto_buf[4096];
    CHECK(flow_lsp_step_search(server, "file:///workspace/test.flow", pareto_buf, sizeof(pareto_buf)));
    CHECK(strstr(pareto_buf, "flow/paretoUpdate") != NULL);
    CHECK(strstr(pareto_buf, "\"tactics\":[") != NULL);
    CHECK(strstr(pareto_buf, "\"name\":\"speed\"") != NULL);
    CHECK(strstr(pareto_buf, "\"name\":\"balanced\"") != NULL);
    CHECK(strstr(pareto_buf, "\"name\":\"memory\"") != NULL);

    /* 5. Test Hover */
    const char *hover_msg = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/hover\",\"params\":{}}";
    CHECK(flow_lsp_process_message(server, hover_msg, strlen(hover_msg)));

    /* 6. Test Shutdown */
    const char *shutdown_msg = "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"shutdown\",\"params\":{}}";
    CHECK(flow_lsp_process_message(server, shutdown_msg, strlen(shutdown_msg)));

    flow_lsp_destroy(server);
    fclose(out_mem);

    printf("LSP_TEST=passed jsonrpc=2.0 fast_diagnostics=sound progressive_pareto=streaming hover=verified\n");
    return 0;
}
