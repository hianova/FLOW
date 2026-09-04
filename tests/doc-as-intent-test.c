/*
 * tests/doc-as-intent-test.c
 *
 * Formal Verification for Phase 2: Doc-as-Intent / Literate Living Specifications
 * Validates that .md documentation files containing ```flow code blocks are
 * first-class executable specifications compilable directly by FLOW.
 */

#include "flow.h"
#include "flow_test_kit.h"
#include "smt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FLOW_TEST_SUITE_BEGIN("Doc-as-Intent / Literate Living Specifications (Suite #74)");

    FLOW_STAGE_BEGIN(1, "Markdown with Prose, Headings & Code Fences");
    {
        const char *markdown_source =
            "# Specification Document: Microservice Pipeline\n"
            "\n"
            "> This is living prose documentation that acts as a formal contract.\n"
            "\n"
            "## 1. Background & Rationale\n"
            "Traditional dead documentation rots over time. FLOW eliminates this by\n"
            "compiling the documentation directly into a verified binary.\n"
            "\n"
            "```flow\n"
            "// Formal flow intent block\n"
            "project microservice_gateway\n"
            "\n"
            "input event_stream {\n"
            "    max_count 5000\n"
            "}\n"
            "\n"
            "flow event_processor {\n"
            "    event_stream -> filter -> dispatch\n"
            "}\n"
            "\n"
            "require {\n"
            "    deterministic\n"
            "    memory < 32mb\n"
            "}\n"
            "\n"
            "prefer {\n"
            "    latency\n"
            "}\n"
            "```\n"
            "\n"
            "## 2. Accompanying C Code Example\n"
            "```c\n"
            "void dummy_example(void) { /* Should be ignored by flow parser */ }\n"
            "```\n"
            "\n"
            "## 3. Epilogue & Conclusion\n"
            "All assertions hold.\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        if (mem) {
            fputs(markdown_source, mem);
            rewind(mem);

            FlowSpec spec;
            memset(&spec, 0, sizeof(spec));
            int ok = parse_spec(mem, &spec);
            fclose(mem);

            FLOW_ASSERT_TRUE(ok == 1);
            FLOW_ASSERT_STR_EQ(spec.project_name, "microservice_gateway");
            FLOW_ASSERT_STR_EQ(spec.input_name, "event_stream");
            FLOW_ASSERT_STR_EQ(spec.flow_name, "event_processor");
            FLOW_ASSERT_EQ(spec.max_count, 5000);
            FLOW_ASSERT_EQ(spec.memory_mb, 32);
            FLOW_ASSERT_EQ(spec.prefer_latency, 1);
            FLOW_ASSERT_EQ(spec.deterministic, 1);
            FLOW_ASSERT_EQ((int)spec.flow_node_count, 3);

            SemanticIR ir;
            memset(&ir, 0, sizeof(ir));
            lower_to_ir(&spec, &ir);
            FLOW_ASSERT_STR_EQ(ir.flow_name, "event_processor");
            FLOW_ASSERT_EQ(ir.memory_limit_mb, 32);
            FLOW_ASSERT_EQ(ir.prefer_latency, 1);
            FLOW_ASSERT_EQ(ir.fact_deterministic, 1);
        }
    }

    FLOW_STAGE_BEGIN(2, "Tilde Fences (~~~flow) and Comment Syntax");
    {
        const char *tilde_markdown =
            "# Alternative Fence Syntax Test\n"
            "\n"
            "~~~flow\n"
            "# Hash comment inside flow\n"
            "// C++ style comment inside flow\n"
            "input telemetry {\n"
            "    max_count 2048\n"
            "}\n"
            "flow telemetry_pipeline {\n"
            "    telemetry -> ingest -> publish\n"
            "}\n"
            "require {\n"
            "    deterministic\n"
            "}\n"
            "~~~\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        if (mem) {
            fputs(tilde_markdown, mem);
            rewind(mem);

            FlowSpec spec;
            memset(&spec, 0, sizeof(spec));
            int ok = parse_spec(mem, &spec);
            fclose(mem);

            FLOW_ASSERT_TRUE(ok == 1);
            FLOW_ASSERT_STR_EQ(spec.input_name, "telemetry");
            FLOW_ASSERT_STR_EQ(spec.flow_name, "telemetry_pipeline");
            FLOW_ASSERT_EQ(spec.max_count, 2048);
            FLOW_ASSERT_EQ(spec.deterministic, 1);
        }
    }

    FLOW_STAGE_BEGIN(3, "Backward Compatibility with Pure .flow Files");
    {
        const char *pure_flow =
            "// Pure FLOW source without markdown fences\n"
            "input sensor_batch {\n"
            "    max_count 128\n"
            "}\n"
            "flow sensor_fusion {\n"
            "    sensor_batch -> filter -> aggregate\n"
            "}\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        if (mem) {
            fputs(pure_flow, mem);
            rewind(mem);

            FlowSpec spec;
            memset(&spec, 0, sizeof(spec));
            int ok = parse_spec(mem, &spec);
            fclose(mem);

            FLOW_ASSERT_TRUE(ok == 1);
            FLOW_ASSERT_STR_EQ(spec.input_name, "sensor_batch");
            FLOW_ASSERT_STR_EQ(spec.flow_name, "sensor_fusion");
            FLOW_ASSERT_EQ(spec.max_count, 128);
        }
    }

    FLOW_STAGE_BEGIN(4, "Negative Tests: Markdown without Flow Block");
    {
        const char *pure_prose_md =
            "# Only Markdown Prose\n"
            "\n"
            "There are no flow blocks here.\n"
            "```python\n"
            "print('Hello world')\n"
            "```\n";

        FILE *mem = tmpfile();
        FLOW_ASSERT_TRUE(mem != NULL);
        if (mem) {
            fputs(pure_prose_md, mem);
            rewind(mem);

            FlowSpec spec;
            memset(&spec, 0, sizeof(spec));
            int ok = parse_spec(mem, &spec);
            fclose(mem);

            /* Must fail gracefully because no flow was found */
            FLOW_ASSERT_TRUE(ok == 0);
        }
    }

    FLOW_TEST_SUITE_END();
    return 0;
}
