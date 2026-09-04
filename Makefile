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

.PHONY: all clean test demos demo benchmark chaos-benchmark gateway-benchmark frontier-benchmark autopoiesis-check acceptance install uninstall fuzz test-build test-run test-e2e sync-book level5-contest audit-mechanisms fvec-flowc-apply-test reload-stress-nightly plugins flowy libflow

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

chaos-benchmark: tests/chaos-fp-vs-int-benchmark.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $(BUILD_DIR)/chaos-fp-vs-int-benchmark -lm
	$(BUILD_DIR)/chaos-fp-vs-int-benchmark

gateway-benchmark: tests/gateway-autonomous-benchmark.c $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $(BUILD_DIR)/gateway-autonomous-benchmark $(LDLIBS)
	$(BUILD_DIR)/gateway-autonomous-benchmark

frontier-benchmark: tests/frontier-4pillars-benchmark.c $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $(BUILD_DIR)/frontier-4pillars-benchmark $(LDLIBS)
	$(BUILD_DIR)/frontier-4pillars-benchmark

# ==============================================================================
# Two-Stage Native Makefile Test Pipeline (Native Hermetic Barrier)
# ==============================================================================

TEST_BINARIES := \
	$(BUILD_DIR)/security-test \
	$(BUILD_DIR)/reload-test \
	$(BUILD_DIR)/live-reload-test \
	$(BUILD_DIR)/backend-reload-test \
	$(BUILD_DIR)/generated-reload-test \
	$(BUILD_DIR)/adaptive-test \
	$(BUILD_DIR)/bitspace-test \
	$(BUILD_DIR)/plugin-test \
	$(BUILD_DIR)/project-test \
	$(BUILD_DIR)/abi-test \
	$(BUILD_DIR)/vertical-slice-test \
	$(BUILD_DIR)/fuzz-test \
	$(BUILD_DIR)/ensemble-test \
	$(BUILD_DIR)/smt-test \
	$(BUILD_DIR)/mlir-llvm-test \
	$(BUILD_DIR)/topology-test \
	$(BUILD_DIR)/ebpf-pmu-test \
	$(BUILD_DIR)/jit-migration-test \
	$(BUILD_DIR)/bootstrap-sandbox-test \
	$(BUILD_DIR)/quantum-dimension-test \
	$(BUILD_DIR)/two-tier-chaos-test \
	$(BUILD_DIR)/zero-tlb-shootdown-test \
	$(BUILD_DIR)/epigenetic-mask-test \
	$(BUILD_DIR)/dynamic-mask-superposition-test \
	$(BUILD_DIR)/mtd-defense-test \
	$(BUILD_DIR)/swarm-federation-test \
	$(BUILD_DIR)/genetic-programming-test \
	$(BUILD_DIR)/dynamic-env-morph-test \
	$(BUILD_DIR)/qsbr-unified-test \
	$(BUILD_DIR)/bitset-genome-test \
	$(BUILD_DIR)/async-jit-worker-test \
	$(BUILD_DIR)/orchestrator-test \
	$(BUILD_DIR)/enterprise-production-test \
	$(BUILD_DIR)/embodied-physics-test \
	$(BUILD_DIR)/flowy-test \
	$(BUILD_DIR)/mechanism-audit-test \
	$(BUILD_DIR)/decision-explain-test \
	$(BUILD_DIR)/hardened-production-test \
	$(BUILD_DIR)/decoupling-test \
	$(BUILD_DIR)/polytope-projection-test \
	$(BUILD_DIR)/plugin-abi-v2-test \
	$(BUILD_DIR)/autonomous-orchestration-test \
	$(BUILD_DIR)/flowy-level5-crucible \
	$(BUILD_DIR)/flowy-i18n-test \
	$(BUILD_DIR)/reload-stress-test \
	$(BUILD_DIR)/vault-test \
	$(BUILD_DIR)/serverless-coldstart-test \
	$(BUILD_DIR)/fleet-immune-test \
	$(BUILD_DIR)/semantic-rag-test \
	$(BUILD_DIR)/tidal-morph-test \
	$(BUILD_DIR)/cross-hardware-transfer-test \
	$(BUILD_DIR)/predictive-jit-test \
	$(BUILD_DIR)/generative-architecture-test \
	$(BUILD_DIR)/fvec-format-test \
	$(BUILD_DIR)/fvec-curator-test \
	$(BUILD_DIR)/immune-promotion-test \
	$(BUILD_DIR)/primitive-driver-test \
	$(BUILD_DIR)/protocol-primitive-test \
	$(BUILD_DIR)/fvec-hub-test \
	$(BUILD_DIR)/hetero-swarm-mesh-test \
	$(BUILD_DIR)/snapshot-replay-test \
	$(BUILD_DIR)/gateway-autonomous-test \
	$(BUILD_DIR)/edge-gateway-test \
	$(BUILD_DIR)/fleet-swarm-test \
	$(BUILD_DIR)/financial-matching-test \
	$(BUILD_DIR)/cxl-llm-fabric-test \
	$(BUILD_DIR)/isomorphic-primitives-test

# Specific build prerequisites for targets with inter-module dependencies
$(BUILD_DIR)/plugin-test: plugins
$(BUILD_DIR)/plugin-abi-v2-test: plugins
$(BUILD_DIR)/decoupling-test: $(FLOWC) $(FLOWY) plugins
$(BUILD_DIR)/flowy-test: $(FLOWY)
$(BUILD_DIR)/flowy-level5-crucible: $(FLOWY)
$(BUILD_DIR)/flowy-i18n-test: $(FLOWY)
$(BUILD_DIR)/autonomous-orchestration-test: $(FLOWY)

$(BUILD_DIR)/generated-reload-test: tests/generated-reload-test.c $(FLOWC) $(LIBFLOW_A) | $(BUILD_DIR)
	$(FLOWC) examples/rank.flow -o /tmp/flow-rank-reload.c --search --iterations 50 --seed 42 --reload-adapter
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc tests/generated-reload-test.c /tmp/flow-rank-reload.c $(LIBFLOW_A) -o $@ $(LDLIBS)
	$(FLOWC) examples/small.flow -o /tmp/flow-small-reload.c --reload-adapter
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c /tmp/flow-small-reload.c -o /tmp/flow-small-reload.o

$(BUILD_DIR)/flowy-level5-crucible: tests/flowy-level5-crucible.c $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

# Pattern rule: compiles any tests/%-test.c directly against libflow.a
$(BUILD_DIR)/%-test: tests/%-test.c $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

# Backward-compatible convenience shortcuts to build & execute any single test
TEST_NAMES := $(patsubst $(BUILD_DIR)/%,%,$(TEST_BINARIES))
.PHONY: $(TEST_NAMES)
$(TEST_NAMES): %: $(BUILD_DIR)/%
	@$<

level5-contest: flowy-level5-crucible

audit-mechanisms: $(FLOWY)
	$(FLOWY) audit-mechanisms

fuzz: | $(BUILD_DIR)
	clang -std=c17 -O2 -fsanitize=fuzzer,address,undefined -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -Isrc $(SRC_LIB) tests/fuzz-test.c -o $(BUILD_DIR)/fuzzer-engine -lm
	@echo "Running LLVM libFuzzer for 5 seconds..."
	$(BUILD_DIR)/fuzzer-engine -max_total_time=5

reload-stress-nightly: $(BUILD_DIR)/reload-stress-test
	FLOW_STRESS_THREADS=32 FLOW_STRESS_CALLS=312500 FLOW_STRESS_PUBLISHES=1000 $<

autopoiesis-check: $(FLOWY)
	$(FLOWY) absorb examples/compiler.flow
	$(FLOWY) anneal examples/compiler.flow examples/project.flow

acceptance: test benchmark autopoiesis-check security-test

fvec-flowc-apply-test: $(FLOWC) $(FLOWY) | $(BUILD_DIR)
	$(FLOWY) fvec seed .flow/vecs
	$(FLOWC) examples/bounded_queue.flow -o generated/fvec-applied.c --apply-fvec .flow/vecs/hft_ultra_low_latency.fvec
	grep -q 'flow: bounded_queue' generated/fvec-applied.c

# Phase 1: Pure Compilation Barrier (Max CPU parallelism with make -j)
test-build: $(FLOWC) $(FLOWY) plugins $(TEST_BINARIES)

# Phase 2: Pure Isolated Execution (Hardware Cache & SMT Soundness Protected)
test-run: $(TEST_BINARIES) fvec-flowc-apply-test
	@echo "=== [Phase 1/5] Running Core Units & Formal SMT Proofs ==="
	@$(BUILD_DIR)/bitspace-test
	@$(BUILD_DIR)/smt-test
	@$(BUILD_DIR)/polytope-projection-test
	@$(BUILD_DIR)/topology-test
	@$(BUILD_DIR)/abi-test
	@$(BUILD_DIR)/project-test
	@$(BUILD_DIR)/fuzz-test
	@$(BUILD_DIR)/ensemble-test
	@$(BUILD_DIR)/mlir-llvm-test
	@$(BUILD_DIR)/vertical-slice-test
	@$(BUILD_DIR)/security-test
	@echo "=== [Phase 2/5] Running Concurrency, QSBR & State Morphing ==="
	@$(BUILD_DIR)/reload-test
	@$(BUILD_DIR)/live-reload-test
	@$(BUILD_DIR)/backend-reload-test
	@$(BUILD_DIR)/generated-reload-test
	@$(BUILD_DIR)/adaptive-test
	@$(BUILD_DIR)/dynamic-env-morph-test
	@$(BUILD_DIR)/zero-tlb-shootdown-test
	@$(BUILD_DIR)/epigenetic-mask-test
	@$(BUILD_DIR)/dynamic-mask-superposition-test
	@$(BUILD_DIR)/mtd-defense-test
	@$(BUILD_DIR)/swarm-federation-test
	@$(BUILD_DIR)/genetic-programming-test
	@$(BUILD_DIR)/qsbr-unified-test
	@$(BUILD_DIR)/bitset-genome-test
	@$(BUILD_DIR)/async-jit-worker-test
	@$(BUILD_DIR)/orchestrator-test
	@$(BUILD_DIR)/jit-migration-test
	@$(BUILD_DIR)/bootstrap-sandbox-test
	@$(BUILD_DIR)/quantum-dimension-test
	@$(BUILD_DIR)/two-tier-chaos-test
	@$(BUILD_DIR)/ebpf-pmu-test
	@echo "=== [Phase 3/5] Running Hardware Primitive Drivers & Embodied Gates ==="
	@$(BUILD_DIR)/embodied-physics-test
	@$(BUILD_DIR)/fleet-swarm-test
	@$(BUILD_DIR)/plugin-test
	@$(BUILD_DIR)/plugin-abi-v2-test
	@$(BUILD_DIR)/primitive-driver-test
	@$(BUILD_DIR)/protocol-primitive-test
	@$(BUILD_DIR)/gateway-autonomous-test
	@$(BUILD_DIR)/edge-gateway-test
	@$(BUILD_DIR)/financial-matching-test
	@$(BUILD_DIR)/cxl-llm-fabric-test
	@$(BUILD_DIR)/isomorphic-primitives-test
	@$(BUILD_DIR)/enterprise-production-test
	@$(BUILD_DIR)/hardened-production-test
	@$(BUILD_DIR)/decoupling-test
	@echo "=== [Phase 4/5] Running Architecture Memory (.fvec) & Herd Immunity ==="
	@$(BUILD_DIR)/vault-test
	@$(BUILD_DIR)/serverless-coldstart-test
	@$(BUILD_DIR)/fleet-immune-test
	@$(BUILD_DIR)/semantic-rag-test
	@$(BUILD_DIR)/tidal-morph-test
	@$(BUILD_DIR)/cross-hardware-transfer-test
	@$(BUILD_DIR)/predictive-jit-test
	@$(BUILD_DIR)/generative-architecture-test
	@$(BUILD_DIR)/fvec-format-test
	@$(BUILD_DIR)/fvec-curator-test
	@$(BUILD_DIR)/immune-promotion-test
	@$(BUILD_DIR)/fvec-hub-test
	@$(BUILD_DIR)/hetero-swarm-mesh-test
	@$(BUILD_DIR)/snapshot-replay-test
	@echo "=== [Phase 5/5] Running Autopilot, Introspection & Hardware Stress Crucible ==="
	@$(BUILD_DIR)/flowy-test
	@$(BUILD_DIR)/mechanism-audit-test
	@$(BUILD_DIR)/decision-explain-test
	@$(BUILD_DIR)/autonomous-orchestration-test
	@$(BUILD_DIR)/flowy-i18n-test
	@$(BUILD_DIR)/reload-stress-test
	@$(BUILD_DIR)/flowy-level5-crucible

# Phase 3: End-to-End Compiler CLI & Invariant Smoke Tests
test-e2e: $(FLOWC) $(FLOWY) plugins
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

test: test-build
	@$(MAKE) --no-print-directory test-run
	@$(MAKE) --no-print-directory test-e2e
	@echo "================================================================================"
	@echo "          ALL 68 TEST SUITES & E2E VERIFICATIONS 100% SOUND & PASSED!           "
	@echo "================================================================================"

clean:
	rm -rf $(BUILD_DIR) generated/*.c generated/*.h generated/*.rs generated/*.py generated/*.profile generated/*.lock generated/*.dot generated/*.json /tmp/flow-* /tmp/d_* /tmp/test_*
