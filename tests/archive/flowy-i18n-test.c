#include "flowy.h"
#include "topology.h"
#include "generated_book_knowledge.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) if (!(cond)) { fprintf(stderr, "flowy-i18n-test failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); }

int main(void) {
    printf("Starting FLOW Natural Language (I18N) & Data-Template Separation Test...\n");

    FlowTopologyGraph graph;
    flow_topology_build_codebase_graph(&graph);

    /* --------------------------------------------------------------------- */
    /* 1. Language Parser & State Configuration                              */
    /* --------------------------------------------------------------------- */
    CHECK(flowy_parse_language("en") == FLOW_LANG_EN);
    CHECK(flowy_parse_language("en_US") == FLOW_LANG_EN);
    CHECK(flowy_parse_language("english") == FLOW_LANG_EN);
    CHECK(flowy_parse_language("zh") == FLOW_LANG_ZH);
    CHECK(flowy_parse_language("zh_TW") == FLOW_LANG_ZH);
    CHECK(flowy_parse_language("invalid") == FLOW_LANG_ZH);

    flowy_set_language(FLOW_LANG_ZH);
    CHECK(flowy_get_language() == FLOW_LANG_ZH);
    flowy_set_language(FLOW_LANG_EN);
    CHECK(flowy_get_language() == FLOW_LANG_EN);

    /* --------------------------------------------------------------------- */
    /* 2. Language-Agnostic Input Mapping (Alias Dictionary to Node ID)       */
    /* --------------------------------------------------------------------- */
    /* (a) Reload / QSBR module */
    FlowyIntrospectiveAnswer a_en_qsbr, a_zh_qsbr, a_cn_qsbr;
    CHECK(flowy_query_codebase_lang(&graph, "how does lock-free QSBR memory reclamation work?", FLOW_LANG_EN, &a_en_qsbr) == 1);
    CHECK(flowy_query_codebase_lang(&graph, "QSBR 無鎖記憶體回收如何運作？", FLOW_LANG_ZH, &a_zh_qsbr) == 1);
    CHECK(flowy_query_codebase_lang(&graph, "无锁热替换与内存回收机制", FLOW_LANG_ZH, &a_cn_qsbr) == 1);

    CHECK(a_en_qsbr.primary_module != NULL && strcmp(a_en_qsbr.primary_module->module_id, "reload") == 0);
    CHECK(a_zh_qsbr.primary_module != NULL && strcmp(a_zh_qsbr.primary_module->module_id, "reload") == 0);
    CHECK(a_cn_qsbr.primary_module != NULL && strcmp(a_cn_qsbr.primary_module->module_id, "reload") == 0);

    /* (b) BitSpace / BMF module */
    FlowyIntrospectiveAnswer a_en_BMF, a_zh_BMF;
    CHECK(flowy_query_codebase_lang(&graph, "BMF mutation mask canvas", FLOW_LANG_EN, &a_en_BMF) == 1);
    CHECK(flowy_query_codebase_lang(&graph, "BMF 最佳化與遮罩畫布", FLOW_LANG_ZH, &a_zh_BMF) == 1);
    CHECK(strcmp(a_en_BMF.primary_module->module_id, "bitspace") == 0);
    CHECK(strcmp(a_zh_BMF.primary_module->module_id, "bitspace") == 0);

    /* (c) SMT Formal Verification module */
    FlowyIntrospectiveAnswer a_en_smt, a_zh_smt;
    CHECK(flowy_query_codebase_lang(&graph, "SMT formal mathematical proofs", FLOW_LANG_EN, &a_en_smt) == 1);
    CHECK(flowy_query_codebase_lang(&graph, "SMT 形式化驗證最高法院定理", FLOW_LANG_ZH, &a_zh_smt) == 1);
    CHECK(strcmp(a_en_smt.primary_module->module_id, "smt") == 0);
    CHECK(strcmp(a_zh_smt.primary_module->module_id, "smt") == 0);

    /* (d) Embodied Robotics module */
    FlowyIntrospectiveAnswer a_en_robot, a_zh_robot;
    CHECK(flowy_query_codebase_lang(&graph, "robotics joint torque and center of mass ZMP", FLOW_LANG_EN, &a_en_robot) == 1);
    CHECK(flowy_query_codebase_lang(&graph, "具身機器人步態規劃與質心抗震", FLOW_LANG_ZH, &a_zh_robot) == 1);
    CHECK(strcmp(a_en_robot.primary_module->module_id, "embodied") == 0);
    CHECK(strcmp(a_zh_robot.primary_module->module_id, "embodied") == 0);

    /* (e) JIT / High-Watermark Survival Mode */
    FlowyIntrospectiveAnswer a_en_jit, a_zh_jit;
    CHECK(flowy_query_codebase_lang(&graph, "memory high watermark OOM survival shelter", FLOW_LANG_EN, &a_en_jit) == 1);
    CHECK(flowy_query_codebase_lang(&graph, "記憶體高水位與生存模式避難所", FLOW_LANG_ZH, &a_zh_jit) == 1);
    CHECK(strcmp(a_en_jit.primary_module->module_id, "jit") == 0);
    CHECK(strcmp(a_zh_jit.primary_module->module_id, "jit") == 0);

    /* --------------------------------------------------------------------- */
    /* 3. Output Presentation Render Mask Verification                       */
    /* --------------------------------------------------------------------- */
    /* English Render Mask: English headers, English philosophy, English chapter */
    CHECK(strstr(a_en_qsbr.explanation, "=== FLOW INTROSPECTIVE CODEBASE ARCHITECTURE REPORT ===") != NULL);
    CHECK(strstr(a_en_qsbr.explanation, "1. CORE RESPONSIBILITIES:") != NULL);
    CHECK(strstr(a_en_qsbr.explanation, "5. 💡 DESIGN PHILOSOPHY & WHY (From 《The FLOW Book》):") != NULL);
    CHECK(strstr(a_en_qsbr.explanation, "Chapter 7: QSBR Lock-Free Hot-Swap") != NULL);
    CHECK(strstr(a_en_qsbr.explanation, "In servers processing millions of requests/sec") != NULL);

    /* Chinese Render Mask: Chinese headers, Chinese philosophy, Chinese chapter */
    CHECK(strstr(a_zh_qsbr.explanation, "=== FLOW 代碼庫內省式架構推論報告 ===") != NULL);
    CHECK(strstr(a_zh_qsbr.explanation, "1. 核心系統職責 (Core Responsibilities):") != NULL);
    CHECK(strstr(a_zh_qsbr.explanation, "5. 💡 設計哲學與成因 (Why - 摘自《The FLOW Book》):") != NULL);
    CHECK(strstr(a_zh_qsbr.explanation, "第七章：QSBR 零鎖熱替換") != NULL);
    CHECK(strstr(a_zh_qsbr.explanation, "在每秒數千萬次請求的高並發伺服器中") != NULL);

    /* --------------------------------------------------------------------- */
    /* 4. Bilingual Living Documentation & The FLOW Book Viewer              */
    /* --------------------------------------------------------------------- */
    char buf_zh[4096] = {0};
    char buf_en[4096] = {0};

    FILE *mem_zh = fmemopen(buf_zh, sizeof(buf_zh), "w");
    CHECK(mem_zh != NULL);
    CHECK(flowy_show_book_lang("4", FLOW_LANG_ZH, mem_zh) == 1);
    fclose(mem_zh);
    CHECK(strstr(buf_zh, "第四章：BMF 最佳化") != NULL);
    CHECK(strstr(buf_zh, "混沌") != NULL);

    FILE *mem_en = fmemopen(buf_en, sizeof(buf_en), "w");
    CHECK(mem_en != NULL);
    CHECK(flowy_show_book_lang("4", FLOW_LANG_EN, mem_en) == 1);
    fclose(mem_en);
    CHECK(strstr(buf_en, "Chapter 4: 1-Bit Chaotic Annealing") != NULL);
    CHECK(strstr(buf_en, "Chaotic") != NULL);

    /* --------------------------------------------------------------------- */
    /* 5. Bilingual Decision & Bottleneck Explanations                       */
    /* --------------------------------------------------------------------- */
    FlowDecisionEvent ev = {
        .timestamp_ns = 5200000ULL,
        .trigger_type = FLOW_DECISION_TRIGGER_TORQUE_ANOMALY,
        .trigger_source = "left_leg_actuator",
        .observed_metric_value = 85.4,
        .threshold_limit_value = 80.0,
        .metric_unit = "N*m",
        .violated_constraint = "Joint Torque Safe Limit",
        .flipped_genome_bit = 14,
        .pre_topology = "AoS_LinearArray",
        .post_topology = "SoA_Sharded_LoadBalance",
        .causal_rationale = "Load shifted to right leg.",
        .hot_swap_grace_ns = 84
    };

    char dec_zh[2048] = {0}, dec_en[2048] = {0};
    flowy_explain_decision_lang(&ev, FLOW_LANG_ZH, dec_zh, sizeof(dec_zh));
    flowy_explain_decision_lang(&ev, FLOW_LANG_EN, dec_en, sizeof(dec_en));

    CHECK(strstr(dec_zh, "=== FLOW 即時決策與因果解釋") != NULL);
    CHECK(strstr(dec_zh, "第八章：硬體原語驅動") != NULL);

    CHECK(strstr(dec_en, "=== FLOW INTROSPECTIVE REAL-TIME DECISION & CAUSAL EXPLANATION ===") != NULL);
    CHECK(strstr(dec_en, "Chapter 8: Hardware Primitive Drivers") != NULL);

    printf("FLOWY_I18N_TEST=passed language_agnostic_mapping=verified render_mask_separation=verified bilingual_book=verified\n");
    return 0;
}
