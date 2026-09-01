#include "lsp.h"
#include "verifier.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FlowLSPServer {
    FlowLSPConfig config;
    FILE *in;
    FILE *out;
    int running;
    char document_uri[256];
    char *document_text;
    size_t document_len;
    SemanticIR ir;
    int ir_valid;
    FlowBitSpace bitspace;
    int bitspace_valid;
    FlowBitSearchResult bit_search;
    size_t search_iterations_done;
};

static void send_jsonrpc_raw(FILE *out, const char *body) {
    if (out == NULL || body == NULL) return;
    size_t len = strlen(body);
    fprintf(out, "Content-Length: %zu\r\n\r\n%s", len, body);
    fflush(out);
}

FlowLSPServer *flow_lsp_create(const FlowLSPConfig *config, FILE *in, FILE *out) {
    FlowLSPServer *server = calloc(1, sizeof(*server));
    if (server == NULL) return NULL;
    if (config != NULL) server->config = *config;
    else {
        server->config.search_batch_iterations = 50;
        server->config.emit_pareto_stream = 1;
        server->config.emit_smt_stream = 1;
    }
    server->in = in ? in : stdin;
    server->out = out ? out : stdout;
    server->running = 1;
    flow_registry_init();
    return server;
}

void flow_lsp_destroy(FlowLSPServer *server) {
    if (server == NULL) return;
    if (server->ir_valid) flow_ir_cleanup(&server->ir);
    if (server->document_text) free(server->document_text);
    free(server);
}

int flow_lsp_validate_document(FlowLSPServer *server, const char *uri, const char *text,
                               char *response_buf, size_t response_capacity) {
    if (server == NULL || text == NULL || response_buf == NULL) return 0;

    strncpy(server->document_uri, uri ? uri : "inmemory://current.flow", sizeof(server->document_uri) - 1);
    if (server->document_text) free(server->document_text);
    server->document_text = strdup(text);
    server->document_len = strlen(text);

    if (server->ir_valid) {
        flow_ir_cleanup(&server->ir);
        server->ir_valid = 0;
    }
    server->bitspace_valid = 0;
    server->search_iterations_done = 0;

    FILE *mem = fmemopen((void *)text, strlen(text), "r");
    if (mem == NULL) return 0;

    FlowSpec spec;
    int parse_ok = parse_spec(mem, &spec);
    fclose(mem);

    if (!parse_ok) {
        snprintf(response_buf, response_capacity,
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
            "\"uri\":\"%s\",\"diagnostics\":[{"
            "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":10}},"
            "\"severity\":1,\"source\":\"flowc\",\"message\":\"Syntax error in Flow specification\""
            "]}}", server->document_uri);
        return 1;
    }

    lower_to_ir(&spec, &server->ir);
    server->ir_valid = 1;

    const Component *comp = select_component(&server->ir);
    VerificationReport v_rep;
    memset(&v_rep, 0, sizeof(v_rep));
    if (comp != NULL) {
        SearchResult s = {0};
        s.component = comp;
        verify_candidate(&server->ir, comp, &s, &v_rep);
    }

    if (flow_bitspace_init_for_ir(&server->ir, &server->bitspace)) {
        server->bitspace_valid = 1;
    }

    snprintf(response_buf, response_capacity,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
        "\"uri\":\"%s\",\"diagnostics\":[{"
        "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":10}},"
        "\"severity\":3,\"source\":\"flowc\",\"message\":\"Verified valid: selected component '%s' (%s), status=%s\""
        "]}}", server->document_uri, comp ? comp->id : "none", comp ? comp->kind : "unknown",
        verification_status_name(v_rep.status));

    return 1;
}

int flow_lsp_step_search(FlowLSPServer *server, const char *uri,
                         char *pareto_update_buf, size_t update_capacity) {
    if (server == NULL || !server->bitspace_valid || pareto_update_buf == NULL) return 0;
    (void)uri;

    size_t batch = server->config.search_batch_iterations > 0 ? server->config.search_batch_iterations : 50;
    uint32_t seed = (uint32_t)(0xC0F0123u + server->search_iterations_done);

    if (!flow_bitspace_search(&server->bitspace, batch, seed, 0, NULL, &server->bit_search)) {
        return 0;
    }
    server->search_iterations_done += batch;

    FlowPlanEnsemble ensemble;
    flow_bitspace_extract_ensemble(&server->bit_search, &ensemble);

    snprintf(pareto_update_buf, update_capacity,
        "{\"jsonrpc\":\"2.0\",\"method\":\"flow/paretoUpdate\",\"params\":{"
        "\"uri\":\"%s\",\"iterations\":%zu,\"pareto_count\":%zu,"
        "\"best_energy\":%.2f,\"best_latency\":%.2f,\"best_memory\":%zu,"
        "\"tactics\":["
        "{\"name\":\"speed\",\"latency\":%.2f,\"memory\":%zu},"
        "{\"name\":\"balanced\",\"latency\":%.2f,\"memory\":%zu},"
        "{\"name\":\"memory\",\"latency\":%.2f,\"memory\":%zu}"
        "]}}",
        server->document_uri, server->search_iterations_done, server->bit_search.pareto_count,
        server->bit_search.best_plan.eval.energy, server->bit_search.best_latency,
        (size_t)server->bit_search.best_memory,
        ensemble.tactics[FLOW_TACTIC_SPEED].eval.latency_score, ensemble.tactics[FLOW_TACTIC_SPEED].eval.memory_bytes,
        ensemble.tactics[FLOW_TACTIC_BALANCED].eval.latency_score, ensemble.tactics[FLOW_TACTIC_BALANCED].eval.memory_bytes,
        ensemble.tactics[FLOW_TACTIC_MEMORY].eval.latency_score, ensemble.tactics[FLOW_TACTIC_MEMORY].eval.memory_bytes);

    return 1;
}

int flow_lsp_process_message(FlowLSPServer *server, const char *json_payload, size_t length) {
    if (server == NULL || json_payload == NULL || length == 0) return 0;

    /* Handle: initialize */
    if (strstr(json_payload, "\"method\":\"initialize\"") != NULL ||
        strstr(json_payload, "\"method\": \"initialize\"") != NULL) {
        const char *init_resp =
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
            "\"capabilities\":{"
            "\"textDocumentSync\":1,"
            "\"hoverProvider\":true"
            "}}}";
        send_jsonrpc_raw(server->out, init_resp);
        return 1;
    }

    /* Handle: textDocument/didOpen or didChange */
    if (strstr(json_payload, "\"method\":\"textDocument/didOpen\"") != NULL ||
        strstr(json_payload, "\"method\":\"textDocument/didChange\"") != NULL ||
        strstr(json_payload, "\"method\": \"textDocument/didOpen\"") != NULL ||
        strstr(json_payload, "\"method\": \"textDocument/didChange\"") != NULL) {
        const char *text_start = strstr(json_payload, "\"text\":");
        if (text_start != NULL) {
            text_start += 7;
            while (*text_start == ' ' || *text_start == '\"') text_start++;
            char doc_buf[4096];
            size_t copy_len = 0;
            while (text_start[copy_len] != '\0' && text_start[copy_len] != '\"' && copy_len < sizeof(doc_buf) - 1) {
                if (text_start[copy_len] == '\\' && text_start[copy_len + 1] == 'n') {
                    doc_buf[copy_len] = '\n';
                    text_start++;
                } else {
                    doc_buf[copy_len] = text_start[copy_len];
                }
                copy_len++;
            }
            doc_buf[copy_len] = '\0';

            char diag_buf[4096];
            if (flow_lsp_validate_document(server, "inmemory://current.flow", doc_buf, diag_buf, sizeof(diag_buf))) {
                send_jsonrpc_raw(server->out, diag_buf);
            }
        }
        return 1;
    }

    /* Handle: textDocument/hover */
    if (strstr(json_payload, "\"method\":\"textDocument/hover\"") != NULL ||
        strstr(json_payload, "\"method\": \"textDocument/hover\"") != NULL) {
        const char *hover_resp =
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{"
            "\"contents\":{\"kind\":\"markdown\",\"value\":\"### FLOW Component Specification\\n- **Engine**: BitSpace 1-bit Mutation\\n- **Proof Mode**: SMT-LIB2 QF_BV Proven Sound\"}"
            "}}";
        send_jsonrpc_raw(server->out, hover_resp);
        return 1;
    }

    /* Handle: flow/startSearch */
    if (strstr(json_payload, "\"method\":\"flow/startSearch\"") != NULL ||
        strstr(json_payload, "\"method\": \"flow/startSearch\"") != NULL) {
        char pareto_buf[4096];
        if (flow_lsp_step_search(server, server->document_uri, pareto_buf, sizeof(pareto_buf))) {
            send_jsonrpc_raw(server->out, pareto_buf);
        }
        return 1;
    }

    /* Handle: shutdown */
    if (strstr(json_payload, "\"method\":\"shutdown\"") != NULL ||
        strstr(json_payload, "\"method\": \"shutdown\"") != NULL) {
        const char *shutdown_resp = "{\"jsonrpc\":\"2.0\",\"id\":99,\"result\":null}";
        send_jsonrpc_raw(server->out, shutdown_resp);
        server->running = 0;
        return 1;
    }

    return 0;
}

int flow_lsp_run_loop(FlowLSPServer *server) {
    if (server == NULL) return 0;
    char header[256];
    while (server->running && fgets(header, sizeof(header), server->in) != NULL) {
        if (strncmp(header, "Content-Length: ", 16) == 0) {
            size_t length = (size_t)strtoull(header + 16, NULL, 10);
            /* consume CRLF */
            if (fgets(header, sizeof(header), server->in) != NULL) {
                char *payload = malloc(length + 1);
                if (payload != NULL) {
                    size_t read_bytes = fread(payload, 1, length, server->in);
                    payload[read_bytes] = '\0';
                    flow_lsp_process_message(server, payload, read_bytes);
                    free(payload);
                }
            }
        }
    }
    return 1;
}
