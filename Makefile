export PATH := /opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2
LDLIBS ?= -lm
THREAD_FLAGS ?= -pthread

BUILD_DIR := build
FLOWC := $(BUILD_DIR)/flowc
FLOWY := $(BUILD_DIR)/flowy
LIBFLOW_A := $(BUILD_DIR)/libflow.a

SRC_LIB := $(filter-out src/flowc.c src/flowy_main.c,$(wildcard src/*.c))
LIB_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/obj/%.o,$(SRC_LIB))

AR ?= ar
RANLIB ?= ranlib

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include/flow

PLUGINS_SO := $(BUILD_DIR)/libflow_embodied.so $(BUILD_DIR)/libflow_smt.so $(BUILD_DIR)/libflow_security.so $(BUILD_DIR)/libflow_swarm.so

.PHONY: all clean test demos demo benchmark security-test reload-test live-reload-test backend-reload-test generated-reload-test adaptive-test plugin-test reload-stress-test reload-stress-nightly autopoiesis-check acceptance install uninstall fuzz-test fuzz ensemble-test smt-test mlir-llvm-test topology-test ebpf-pmu-test jit-migration-test bootstrap-sandbox-test quantum-dimension-test two-tier-chaos-test zero-tlb-shootdown-test epigenetic-mask-test dynamic-mask-superposition-test mtd-defense-test swarm-federation-test genetic-programming-test dynamic-env-morph-test qsbr-unified-test bitset-genome-test async-jit-worker-test orchestrator-test enterprise-production-test embodied-physics-test flowy-test mechanism-audit-test audit-mechanisms decision-explain-test hardened-production-test decoupling-test plugins flowy libflow polytope-projection-test plugin-abi-v2-test autonomous-orchestration-test flowy-level5-crucible level5-contest sync-book flowy-i18n-test vault-test serverless-coldstart-test fleet-immune-test semantic-rag-test tidal-morph-test cross-hardware-transfer-test predictive-jit-test generative-architecture-test fvec-format-test fvec-curator-test fvec-flowc-apply-test immune-promotion-test primitive-driver-test fvec-hub-test snapshot-replay-test

all: src/generated_book_knowledge.h $(LIBFLOW_A) $(FLOWC) $(FLOWY) plugins

sync-book: src/generated_book_knowledge.h

src/generated_book_knowledge.h: tools/sync-flow-book.c $(wildcard flow-book/src/*.md) | $(BUILD_DIR)
	$(CC) $(CFLAGS) tools/sync-flow-book.c -o $(BUILD_DIR)/sync-flow-book
	$(BUILD_DIR)/sync-flow-book $@

install: $(FLOWC) $(FLOWY) $(LIBFLOW_A)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(FLOWC) $(DESTDIR)$(BINDIR)/flowc
	install -m 755 $(FLOWY) $(DESTDIR)$(BINDIR)/flowy
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(LIBFLOW_A) $(DESTDIR)$(PREFIX)/lib/libflow.a
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	install -m 644 src/*.h $(DESTDIR)$(INCLUDEDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/flowc $(DESTDIR)$(BINDIR)/flowy $(DESTDIR)$(PREFIX)/lib/libflow.a
	rm -rf $(DESTDIR)$(INCLUDEDIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/obj/%.o: src/%.c src/generated_book_knowledge.h $(wildcard src/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(LIBFLOW_A): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

libflow: $(LIBFLOW_A)

$(FLOWC): src/flowc.c $(LIBFLOW_A) src/generated_book_knowledge.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/flowc.c $(LIBFLOW_A) -o $@ $(LDLIBS) $(THREAD_FLAGS)

$(FLOWY): src/flowy_main.c $(LIBFLOW_A) src/generated_book_knowledge.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/flowy_main.c $(LIBFLOW_A) -o $@ $(LDLIBS) $(THREAD_FLAGS)

flowy: $(FLOWY)

plugins: $(PLUGINS_SO)

$(BUILD_DIR)/libflow_embodied.so: src/embodied.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc src/embodied.c -o $@ $(LDLIBS)

$(BUILD_DIR)/libflow_smt.so: src/smt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc src/smt.c -o $@ $(LDLIBS)

$(BUILD_DIR)/libflow_security.so: src/security.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc src/security.c -o $@ $(LDLIBS)

$(BUILD_DIR)/libflow_swarm.so: src/swarm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc src/swarm.c -o $@ $(LDLIBS)

demo: $(FLOWC)
	$(FLOWC) examples/rank.flow -o generated/rank.c
	$(CC) $(CFLAGS) $(THREAD_FLAGS) generated/rank.c -o $(BUILD_DIR)/rank
	$(BUILD_DIR)/rank

demos: $(FLOWC)
	@for spec in bounded_queue shared_cache parallel_map rank binary_parser state_machine; do \
		$(FLOWC) examples/$$spec.flow -o generated/$$spec-demo.c --search --iterations 100 --seed 42 || exit 1; \
		$(CC) $(CFLAGS) $(THREAD_FLAGS) generated/$$spec-demo.c -o $(BUILD_DIR)/$$spec-demo; \
		$(BUILD_DIR)/$$spec-demo | grep -q "flow: $$spec" || exit 1; \
	done

benchmark: demos
	./tools/benchmark-suite.sh 1000

security-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/security-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/security-test $(LDLIBS)
	$(BUILD_DIR)/security-test

reload-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/reload-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/reload-test $(LDLIBS)
	$(BUILD_DIR)/reload-test

live-reload-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/live-reload-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/live-reload-test $(LDLIBS)
	$(BUILD_DIR)/live-reload-test

backend-reload-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/backend-reload-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/backend-reload-test $(LDLIBS)
	$(BUILD_DIR)/backend-reload-test

generated-reload-test: $(FLOWC) $(LIBFLOW_A) | $(BUILD_DIR)
	$(FLOWC) examples/rank.flow -o /tmp/flow-rank-reload.c --search --iterations 50 --seed 42 --reload-adapter
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/generated-reload-test.c /tmp/flow-rank-reload.c $(LIBFLOW_A) -o $(BUILD_DIR)/generated-reload-test $(LDLIBS)
	$(BUILD_DIR)/generated-reload-test
	$(FLOWC) examples/small.flow -o /tmp/flow-small-reload.c --reload-adapter
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c /tmp/flow-small-reload.c -o /tmp/flow-small-reload.o

adaptive-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/adaptive-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/adaptive-test $(LDLIBS)
	$(BUILD_DIR)/adaptive-test

bitspace-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/bitspace-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/bitspace-test $(LDLIBS)
	$(BUILD_DIR)/bitspace-test

plugin-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/plugin-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/plugin-test $(LDLIBS)
	$(BUILD_DIR)/plugin-test

project-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/project-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/project-test $(LDLIBS)
	$(BUILD_DIR)/project-test

abi-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/abi-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/abi-test $(LDLIBS)
	$(BUILD_DIR)/abi-test

vertical-slice-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/vertical-slice-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/vertical-slice-test $(LDLIBS)
	$(BUILD_DIR)/vertical-slice-test

fuzz-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/fuzz-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/fuzz-test $(LDLIBS)
	$(BUILD_DIR)/fuzz-test

ensemble-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/ensemble-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/ensemble-test $(LDLIBS)
	$(BUILD_DIR)/ensemble-test

smt-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/smt-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/smt-test $(LDLIBS)
	$(BUILD_DIR)/smt-test

mlir-llvm-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/mlir-llvm-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/mlir-llvm-test $(LDLIBS)
	$(BUILD_DIR)/mlir-llvm-test

topology-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/topology-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/topology-test $(LDLIBS)
	$(BUILD_DIR)/topology-test

ebpf-pmu-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/ebpf-pmu-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/ebpf-pmu-test $(LDLIBS)
	$(BUILD_DIR)/ebpf-pmu-test

jit-migration-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/jit-migration-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/jit-migration-test $(LDLIBS)
	$(BUILD_DIR)/jit-migration-test

bootstrap-sandbox-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/bootstrap-sandbox-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/bootstrap-sandbox-test $(LDLIBS)
	$(BUILD_DIR)/bootstrap-sandbox-test

quantum-dimension-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/quantum-dimension-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/quantum-dimension-test $(LDLIBS)
	$(BUILD_DIR)/quantum-dimension-test

two-tier-chaos-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/two-tier-chaos-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/two-tier-chaos-test $(LDLIBS)
	$(BUILD_DIR)/two-tier-chaos-test

zero-tlb-shootdown-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/zero-tlb-shootdown-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/zero-tlb-shootdown-test $(LDLIBS)
	$(BUILD_DIR)/zero-tlb-shootdown-test

epigenetic-mask-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/epigenetic-mask-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/epigenetic-mask-test $(LDLIBS)
	$(BUILD_DIR)/epigenetic-mask-test

dynamic-mask-superposition-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/dynamic-mask-superposition-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/dynamic-mask-superposition-test $(LDLIBS)
	$(BUILD_DIR)/dynamic-mask-superposition-test

mtd-defense-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/mtd-defense-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/mtd-defense-test $(LDLIBS)
	$(BUILD_DIR)/mtd-defense-test

swarm-federation-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/swarm-federation-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/swarm-federation-test $(LDLIBS)
	$(BUILD_DIR)/swarm-federation-test

genetic-programming-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/genetic-programming-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/genetic-programming-test $(LDLIBS)
	$(BUILD_DIR)/genetic-programming-test

dynamic-env-morph-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/dynamic-env-morph-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/dynamic-env-morph-test $(LDLIBS)
	$(BUILD_DIR)/dynamic-env-morph-test

qsbr-unified-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/qsbr-unified-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/qsbr-unified-test $(LDLIBS)
	$(BUILD_DIR)/qsbr-unified-test

bitset-genome-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/bitset-genome-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/bitset-genome-test $(LDLIBS)
	$(BUILD_DIR)/bitset-genome-test

async-jit-worker-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/async-jit-worker-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/async-jit-worker-test $(LDLIBS)
	$(BUILD_DIR)/async-jit-worker-test

orchestrator-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/orchestrator-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/orchestrator-test $(LDLIBS)
	$(BUILD_DIR)/orchestrator-test

enterprise-production-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/enterprise-production-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/enterprise-production-test $(LDLIBS)
	$(BUILD_DIR)/enterprise-production-test

embodied-physics-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/embodied-physics-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/embodied-physics-test $(LDLIBS)
	$(BUILD_DIR)/embodied-physics-test

flowy-test: $(FLOWY) $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/flowy-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/flowy-test $(LDLIBS)
	$(BUILD_DIR)/flowy-test

mechanism-audit-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/mechanism-audit-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/mechanism-audit-test $(LDLIBS)
	$(BUILD_DIR)/mechanism-audit-test

decision-explain-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/decision-explain-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/decision-explain-test $(LDLIBS)
	$(BUILD_DIR)/decision-explain-test

hardened-production-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/hardened-production-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/hardened-production-test $(LDLIBS)
	$(BUILD_DIR)/hardened-production-test

decoupling-test: $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/decoupling-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/decoupling-test $(LDLIBS)
	$(BUILD_DIR)/decoupling-test

polytope-projection-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/polytope-projection-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/polytope-projection-test $(LDLIBS)
	$(BUILD_DIR)/polytope-projection-test

plugin-abi-v2-test: plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/plugin-abi-v2-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/plugin-abi-v2-test $(LDLIBS)
	$(BUILD_DIR)/plugin-abi-v2-test

autonomous-orchestration-test: $(FLOWY) $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/autonomous-orchestration-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/autonomous-orchestration-test $(LDLIBS)
	$(BUILD_DIR)/autonomous-orchestration-test

flowy-level5-crucible: $(FLOWY) $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/flowy-level5-crucible.c $(LIBFLOW_A) -o $(BUILD_DIR)/flowy-level5-crucible $(LDLIBS)
	$(BUILD_DIR)/flowy-level5-crucible

flowy-i18n-test: $(FLOWY) $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/flowy-i18n-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/flowy-i18n-test $(LDLIBS)
	$(BUILD_DIR)/flowy-i18n-test

level5-contest: flowy-level5-crucible

audit-mechanisms: $(FLOWY)
	$(FLOWY) audit-mechanisms

fuzz: | $(BUILD_DIR)
	clang -std=c17 -O2 -fsanitize=fuzzer,address,undefined -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -Isrc $(SRC_LIB) tests/fuzz-test.c -o $(BUILD_DIR)/fuzzer-engine -lm
	@echo "Running LLVM libFuzzer for 5 seconds..."
	$(BUILD_DIR)/fuzzer-engine -max_total_time=5

reload-stress-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/reload-stress-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/reload-stress-test $(LDLIBS)
	$(BUILD_DIR)/reload-stress-test

reload-stress-nightly: reload-stress-test
	FLOW_STRESS_THREADS=32 FLOW_STRESS_CALLS=312500 FLOW_STRESS_PUBLISHES=1000 $(BUILD_DIR)/reload-stress-test

autopoiesis-check: $(FLOWY)
	$(FLOWY) absorb examples/compiler.flow
	$(FLOWY) anneal examples/compiler.flow examples/project.flow

acceptance: test benchmark autopoiesis-check security-test

vault-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/vault-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/vault-test $(LDLIBS)
	$(BUILD_DIR)/vault-test

serverless-coldstart-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/serverless-coldstart-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/serverless-coldstart-test $(LDLIBS)
	$(BUILD_DIR)/serverless-coldstart-test

fleet-immune-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/fleet-immune-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/fleet-immune-test $(LDLIBS)
	$(BUILD_DIR)/fleet-immune-test

semantic-rag-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/semantic-rag-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/semantic-rag-test $(LDLIBS)
	$(BUILD_DIR)/semantic-rag-test

tidal-morph-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/tidal-morph-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/tidal-morph-test $(LDLIBS)
	$(BUILD_DIR)/tidal-morph-test

cross-hardware-transfer-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/cross-hardware-transfer-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/cross-hardware-transfer-test $(LDLIBS)
	$(BUILD_DIR)/cross-hardware-transfer-test

predictive-jit-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/predictive-jit-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/predictive-jit-test $(LDLIBS)
	$(BUILD_DIR)/predictive-jit-test

generative-architecture-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/generative-architecture-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/generative-architecture-test $(LDLIBS)
	$(BUILD_DIR)/generative-architecture-test

fvec-format-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/fvec-format-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/fvec-format-test $(LDLIBS)
	$(BUILD_DIR)/fvec-format-test

fvec-curator-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/fvec-curator-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/fvec-curator-test $(LDLIBS)
	$(BUILD_DIR)/fvec-curator-test

immune-promotion-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/immune-promotion-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/immune-promotion-test $(LDLIBS)
	$(BUILD_DIR)/immune-promotion-test

primitive-driver-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/primitive-driver-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/primitive-driver-test $(LDLIBS)
	$(BUILD_DIR)/primitive-driver-test

fvec-hub-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/fvec-hub-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/fvec-hub-test $(LDLIBS)
	$(BUILD_DIR)/fvec-hub-test

snapshot-replay-test: $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/snapshot-replay-test.c $(LIBFLOW_A) -o $(BUILD_DIR)/snapshot-replay-test $(LDLIBS)
	$(BUILD_DIR)/snapshot-replay-test

fvec-flowc-apply-test: $(FLOWC) $(FLOWY) | $(BUILD_DIR)
	$(FLOWY) fvec seed .flow/vecs
	$(FLOWC) examples/bounded_queue.flow -o generated/fvec-applied.c --apply-fvec .flow/vecs/hft_ultra_low_latency.fvec
	grep -q 'flow: bounded_queue' generated/fvec-applied.c

test: $(FLOWC) $(FLOWY) plugins reload-test live-reload-test backend-reload-test generated-reload-test adaptive-test bitspace-test plugin-test project-test abi-test vertical-slice-test reload-stress-test fuzz-test ensemble-test smt-test mlir-llvm-test topology-test ebpf-pmu-test jit-migration-test bootstrap-sandbox-test quantum-dimension-test two-tier-chaos-test zero-tlb-shootdown-test epigenetic-mask-test dynamic-mask-superposition-test mtd-defense-test swarm-federation-test genetic-programming-test dynamic-env-morph-test qsbr-unified-test bitset-genome-test async-jit-worker-test orchestrator-test enterprise-production-test embodied-physics-test flowy-test mechanism-audit-test decision-explain-test hardened-production-test decoupling-test polytope-projection-test plugin-abi-v2-test autonomous-orchestration-test flowy-level5-crucible flowy-i18n-test vault-test serverless-coldstart-test fleet-immune-test semantic-rag-test tidal-morph-test cross-hardware-transfer-test predictive-jit-test generative-architecture-test fvec-format-test fvec-curator-test fvec-flowc-apply-test immune-promotion-test primitive-driver-test fvec-hub-test snapshot-replay-test
	! grep -E -q 'heavy-tail|flip_bit_block|Black Swan' src/search.c README.md ACCEPTANCE.md
	grep -E -q 'one chaotic 1-bit mutation' src/search.c
	$(FLOWC) examples/rank.flow -o generated/rank.c
	$(CC) $(CFLAGS) $(THREAD_FLAGS) generated/rank.c -o $(BUILD_DIR)/rank
	$(BUILD_DIR)/rank | grep -q 'component: sharded_hash'
	$(BUILD_DIR)/rank | grep -q 'top 3'
	grep -q 'flow_slot slots' generated/rank.c
	grep -q 'Verification: status=proven' generated/rank.c
	! grep -q 'static int flow_verify_input' generated/rank.c
	$(FLOWC) examples/small.flow -o generated/small.c
	$(CC) $(CFLAGS) $(THREAD_FLAGS) generated/small.c -o $(BUILD_DIR)/small
	$(BUILD_DIR)/small | grep -q 'component: linear_array'
	grep -q 'flow_item items\[FLOW_CAPACITY\]' generated/small.c
	$(FLOWC) examples/custom_input.flow -o generated/custom-input.c
	$(CC) $(CFLAGS) generated/custom-input.c -o $(BUILD_DIR)/custom-input
	$(BUILD_DIR)/custom-input | grep -q 'user 11 score 99'
	grep -q 'input_samples=3' generated/custom-input.c
	$(FLOWC) examples/ordered.flow -o generated/ordered.c
	$(CC) $(CFLAGS) $(THREAD_FLAGS) generated/ordered.c -o $(BUILD_DIR)/ordered
	$(BUILD_DIR)/ordered | grep -q 'component: ordered_tree'
	grep -q 'flow_node nodes\[FLOW_CAPACITY\]' generated/ordered.c
	$(FLOWC) examples/ordered.flow -o generated/ordered-hash.c --component sharded_hash
	$(CC) $(CFLAGS) $(THREAD_FLAGS) generated/ordered-hash.c -o $(BUILD_DIR)/ordered-hash
	$(BUILD_DIR)/ordered-hash | grep -q 'component: sharded_hash'
	! $(FLOWC) examples/ordered.flow -o /tmp/flow-invalid-component.c --component parallel_map
	$(FLOWC) examples/rank.flow -o generated/rank-search.c --search --iterations 500 --seed 42
	$(CC) $(CFLAGS) $(THREAD_FLAGS) generated/rank-search.c -o $(BUILD_DIR)/rank-search
	$(BUILD_DIR)/rank-search | grep -q 'component: sharded_hash'
	@online_threads=$$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1); \
	if [ $$online_threads -lt 1 ]; then online_threads=1; fi; \
	if [ $$online_threads -gt 64 ]; then online_threads=64; fi; \
	$(BUILD_DIR)/rank-search | grep -q "configuration: capacity 4096 threads $$online_threads shards 16"
	grep -q 'C search: mode=model iterations=500 seed=42 genome=' generated/rank-search.c
	grep -q 'Verification: status=runtime_check' generated/rank-search.c
	$(FLOWC) examples/rank.flow -o generated/rank-benchmark.c --benchmark --iterations 100 --seed 42
	$(CC) $(CFLAGS) generated/rank-benchmark.c -o $(BUILD_DIR)/rank-benchmark
	$(BUILD_DIR)/rank-benchmark | grep -q 'component: sharded_hash'
	grep -q 'C search: mode=benchmark' generated/rank-benchmark.c
	grep -q 'static int flow_verify_input' generated/rank-benchmark.c
	$(FLOWC) examples/vocabulary.flow -o generated/vocabulary.c
	$(CC) $(CFLAGS) generated/vocabulary.c -o $(BUILD_DIR)/vocabulary
	$(BUILD_DIR)/vocabulary | grep -q 'flow: vocabulary'
	$(BUILD_DIR)/vocabulary | grep -q 'component: parallel_map'
	$(BUILD_DIR)/vocabulary | grep -q 'user 3 score 198'
	grep -q 'map_worker' generated/vocabulary.c
	grep -q 'output=scores' generated/vocabulary.c
	grep -q 'resource=cpu' generated/vocabulary.c
	grep -q 'capability=pthread' generated/vocabulary.c
	grep -q 'domain=ranking' generated/vocabulary.c
	$(FLOWC) examples/bounded_queue.flow -o /tmp/flow-bounded-search.c --search --iterations 1 --seed 42 > /tmp/flow-bounded-search.log
	grep -q 'selected: bounded_queue' /tmp/flow-bounded-search.log
	$(FLOWC) examples/ambiguous.flow -o generated/ambiguous.c
	grep -q 'holes=1' generated/ambiguous.c
	grep -q 'constraints=1' generated/ambiguous.c
	grep -q 'inferred_facts=' generated/ambiguous.c
	$(FLOWC) examples/compiler.flow -o generated/compiler.c
	$(CC) $(CFLAGS) generated/compiler.c -o $(BUILD_DIR)/compiler
	$(BUILD_DIR)/compiler examples/small.flow -o /tmp/flow-make-self.c
	$(CC) $(CFLAGS) /tmp/flow-make-self.c -o $(BUILD_DIR)/make-self
	$(BUILD_DIR)/make-self | grep -q 'flow: scan'
	grep -q 'self_host_graph_nodes=11' generated/compiler.c
	$(FLOWC) examples/rank.flow -o generated/rank-profile.c --benchmark --iterations 50 --seed 42 --profile-out generated/rank.profile
	$(FLOWC) examples/rank.flow -o generated/rank-profile-reuse.c --benchmark --iterations 10 --seed 7 --profile generated/rank.profile > /tmp/flow-profile-reuse.log
	grep -q 'profile: loaded' /tmp/flow-profile-reuse.log
	$(FLOWC) examples/project.flow -o generated/project.c --search --iterations 50 --seed 42 --lock generated/flowplan.lock
	grep -q 'Project: browser_runtime' generated/project.c
	grep -q 'plan_schema_hash=' generated/flowplan.lock
	$(CC) $(CFLAGS) $(THREAD_FLAGS) generated/project.c -o $(BUILD_DIR)/project
	$(BUILD_DIR)/project | grep -q 'flow: browser_pipeline'
	$(FLOWC) examples/project.flow -o generated/project-locked.c --profile generated/flowplan.lock
	grep -q 'Project: browser_runtime' generated/project-locked.c
	$(FLOWC) examples/rank.flow -o generated/rank-cross.c --target-c-header generated/rank-cross.h --target-rust generated/rank-cross.rs --target-python generated/rank-cross.py
	grep -q 'flow_sharded_hash_create' generated/rank-cross.h
	grep -q 'pub struct ShardedHash' generated/rank-cross.rs
	grep -q 'class ShardedHash:' generated/rank-cross.py
	! $(FLOWC) examples/invalid_capacity.flow -o /tmp/flow-invalid.c
	! $(FLOWC) examples/invalid_syntax.flow -o /tmp/flow-invalid-syntax.c
	! $(FLOWC) examples/invalid_unclosed.flow -o /tmp/flow-invalid-unclosed.c
	! $(FLOWC) examples/invalid_value.flow -o /tmp/flow-invalid-value.c
	! $(FLOWC) examples/invalid_capability.flow -o /tmp/flow-invalid-capability.c
	! $(FLOWC) examples/invalid_import.flow -o /tmp/flow-invalid-import.c
	! $(FLOWC) examples/invalid_unbounded.flow -o /tmp/flow-invalid-unbounded.c
	! $(FLOWC) examples/invalid_no_input.flow -o /tmp/flow-invalid-no-input.c

clean:
	rm -rf $(BUILD_DIR) generated/*.c generated/*.h generated/*.rs generated/*.py generated/*.profile generated/*.lock generated/*.dot generated/*.json /tmp/flow-* /tmp/d_* /tmp/test_*
