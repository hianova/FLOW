#ifndef FLOW_LSP_H
#define FLOW_LSP_H

#include "flow.h"
#include "registry.h"
#include "bitspace.h"
#include "smt.h"
#include "topology.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

typedef struct FlowLSPServer FlowLSPServer;

typedef struct {
    int verbose;
    int emit_pareto_stream;
    int emit_topology_stream;
    int emit_smt_stream;
    size_t search_batch_iterations;
} FlowLSPConfig;

FlowLSPServer *flow_lsp_create(const FlowLSPConfig *config, FILE *in, FILE *out);
void flow_lsp_destroy(FlowLSPServer *server);

/* Process single incoming JSON-RPC message from stream */
int flow_lsp_process_message(FlowLSPServer *server, const char *json_payload, size_t length);

/* Run standard stdio event loop (blocks until exit) */
int flow_lsp_run_loop(FlowLSPServer *server);

/* Instant Diagnostic Check on document buffer (< 1ms) */
int flow_lsp_validate_document(FlowLSPServer *server, const char *uri, const char *text,
                               char *response_buf, size_t response_capacity);

/* Progressive Pareto & Search Step */
int flow_lsp_step_search(FlowLSPServer *server, const char *uri,
                         char *pareto_update_buf, size_t update_capacity);

#endif
