export PATH := /opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2
LDLIBS ?= -lm
THREAD_FLAGS ?= -pthread

BUILD_DIR := build
FLOWC := $(BUILD_DIR)/flowc

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include/flow

.PHONY: all clean test demos demo benchmark security-test reload-test live-reload-test backend-reload-test generated-reload-test adaptive-test plugin-test reload-stress-test reload-stress-nightly autopoiesis-check acceptance install uninstall fuzz-test fuzz ensemble-test smt-test mlir-llvm-test topology-test ebpf-pmu-test jit-migration-test bootstrap-sandbox-test quantum-dimension-test two-tier-chaos-test zero-tlb-shootdown-test epigenetic-mask-test dynamic-mask-superposition-test mtd-defense-test swarm-federation-test genetic-programming-test dynamic-env-morph-test qsbr-unified-test bitset-genome-test async-jit-worker-test orchestrator-test enterprise-production-test embodied-physics-test flowy-test mechanism-audit-test audit-mechanisms

all: $(FLOWC)

install: $(FLOWC)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(FLOWC) $(DESTDIR)$(BINDIR)/flowc
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	install -m 644 src/*.h $(DESTDIR)$(INCLUDEDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/flowc
	rm -rf $(DESTDIR)$(INCLUDEDIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(FLOWC): $(wildcard src/*.c) $(wildcard src/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(wildcard src/*.c) -o $@ $(LDLIBS) $(THREAD_FLAGS)

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

security-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/security-test.c -o $(BUILD_DIR)/security-test -lm
	$(BUILD_DIR)/security-test

reload-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/reload-test.c -o $(BUILD_DIR)/reload-test -lm
	$(BUILD_DIR)/reload-test

live-reload-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/live-reload-test.c -o $(BUILD_DIR)/live-reload-test -lm
	$(BUILD_DIR)/live-reload-test

backend-reload-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/backend-reload-test.c -o $(BUILD_DIR)/backend-reload-test -lm
	$(BUILD_DIR)/backend-reload-test

generated-reload-test: $(FLOWC) | $(BUILD_DIR)
	$(FLOWC) examples/rank.flow -o /tmp/flow-rank-reload.c --search --iterations 50 --seed 42 --reload-adapter
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/generated-reload-test.c /tmp/flow-rank-reload.c -o $(BUILD_DIR)/generated-reload-test -lm
	$(BUILD_DIR)/generated-reload-test
	$(FLOWC) examples/small.flow -o /tmp/flow-small-reload.c --reload-adapter
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c /tmp/flow-small-reload.c -o /tmp/flow-small-reload.o

adaptive-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/adaptive-test.c -o $(BUILD_DIR)/adaptive-test -lm
	$(BUILD_DIR)/adaptive-test

bitspace-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/bitspace-test.c -o $(BUILD_DIR)/bitspace-test -lm
	$(BUILD_DIR)/bitspace-test

plugin-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/plugin-test.c -o $(BUILD_DIR)/plugin-test -lm
	$(BUILD_DIR)/plugin-test

project-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/project-test.c -o $(BUILD_DIR)/project-test -lm
	$(BUILD_DIR)/project-test

abi-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/abi-test.c -o $(BUILD_DIR)/abi-test -lm
	$(BUILD_DIR)/abi-test

vertical-slice-test: $(FLOWC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/vertical-slice-test.c -o $(BUILD_DIR)/vertical-slice-test -lm
	$(BUILD_DIR)/vertical-slice-test

fuzz-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/fuzz-test.c -o $(BUILD_DIR)/fuzz-test -lm
	$(BUILD_DIR)/fuzz-test

ensemble-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/ensemble-test.c -o $(BUILD_DIR)/ensemble-test -lm
	$(BUILD_DIR)/ensemble-test

smt-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/smt-test.c -o $(BUILD_DIR)/smt-test -lm
	$(BUILD_DIR)/smt-test

mlir-llvm-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/mlir-llvm-test.c -o $(BUILD_DIR)/mlir-llvm-test -lm
	$(BUILD_DIR)/mlir-llvm-test

topology-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/topology-test.c -o $(BUILD_DIR)/topology-test -lm
	$(BUILD_DIR)/topology-test

ebpf-pmu-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/ebpf-pmu-test.c -o $(BUILD_DIR)/ebpf-pmu-test -lm
	$(BUILD_DIR)/ebpf-pmu-test

jit-migration-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/jit-migration-test.c -o $(BUILD_DIR)/jit-migration-test -lm
	$(BUILD_DIR)/jit-migration-test

bootstrap-sandbox-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/bootstrap-sandbox-test.c -o $(BUILD_DIR)/bootstrap-sandbox-test -lm
	$(BUILD_DIR)/bootstrap-sandbox-test

quantum-dimension-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/quantum-dimension-test.c -o $(BUILD_DIR)/quantum-dimension-test -lm
	$(BUILD_DIR)/quantum-dimension-test

two-tier-chaos-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/two-tier-chaos-test.c -o $(BUILD_DIR)/two-tier-chaos-test -lm
	$(BUILD_DIR)/two-tier-chaos-test

zero-tlb-shootdown-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/zero-tlb-shootdown-test.c -o $(BUILD_DIR)/zero-tlb-shootdown-test -lm
	$(BUILD_DIR)/zero-tlb-shootdown-test

epigenetic-mask-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/epigenetic-mask-test.c -o $(BUILD_DIR)/epigenetic-mask-test -lm
	$(BUILD_DIR)/epigenetic-mask-test

dynamic-mask-superposition-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/dynamic-mask-superposition-test.c -o $(BUILD_DIR)/dynamic-mask-superposition-test -lm
	$(BUILD_DIR)/dynamic-mask-superposition-test

mtd-defense-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/mtd-defense-test.c -o $(BUILD_DIR)/mtd-defense-test -lm
	$(BUILD_DIR)/mtd-defense-test

swarm-federation-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/swarm-federation-test.c -o $(BUILD_DIR)/swarm-federation-test -lm
	$(BUILD_DIR)/swarm-federation-test

genetic-programming-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/genetic-programming-test.c -o $(BUILD_DIR)/genetic-programming-test -lm
	$(BUILD_DIR)/genetic-programming-test

dynamic-env-morph-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/dynamic-env-morph-test.c -o $(BUILD_DIR)/dynamic-env-morph-test -lm
	$(BUILD_DIR)/dynamic-env-morph-test

qsbr-unified-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/qsbr-unified-test.c -o $(BUILD_DIR)/qsbr-unified-test -lm
	$(BUILD_DIR)/qsbr-unified-test

bitset-genome-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/bitset-genome-test.c -o $(BUILD_DIR)/bitset-genome-test -lm
	$(BUILD_DIR)/bitset-genome-test

async-jit-worker-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/async-jit-worker-test.c -o $(BUILD_DIR)/async-jit-worker-test -lm
	$(BUILD_DIR)/async-jit-worker-test

orchestrator-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/orchestrator-test.c -o $(BUILD_DIR)/orchestrator-test -lm
	$(BUILD_DIR)/orchestrator-test

enterprise-production-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/enterprise-production-test.c -o $(BUILD_DIR)/enterprise-production-test -lm
	$(BUILD_DIR)/enterprise-production-test

embodied-physics-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/embodied-physics-test.c -o $(BUILD_DIR)/embodied-physics-test -lm
	$(BUILD_DIR)/embodied-physics-test

flowy-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/flowy-test.c -o $(BUILD_DIR)/flowy-test -lm
	$(BUILD_DIR)/flowy-test

mechanism-audit-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/mechanism-audit-test.c -o $(BUILD_DIR)/mechanism-audit-test -lm
	$(BUILD_DIR)/mechanism-audit-test

audit-mechanisms: $(FLOWC)
	$(FLOWC) audit-mechanisms

fuzz: | $(BUILD_DIR)
	clang -std=c17 -O2 -fsanitize=fuzzer,address,undefined -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/fuzz-test.c -o $(BUILD_DIR)/fuzzer-engine -lm
	@echo "Running LLVM libFuzzer for 5 seconds..."
	$(BUILD_DIR)/fuzzer-engine -max_total_time=5

reload-stress-test: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $(filter-out src/flowc.c,$(wildcard src/*.c)) tests/reload-stress-test.c -o $(BUILD_DIR)/reload-stress-test -lm
	$(BUILD_DIR)/reload-stress-test

reload-stress-nightly: reload-stress-test
	FLOW_STRESS_THREADS=32 FLOW_STRESS_CALLS=312500 FLOW_STRESS_PUBLISHES=1000 $(BUILD_DIR)/reload-stress-test

autopoiesis-check: $(FLOWC)
	$(FLOWC) absorb examples/compiler.flow
	$(FLOWC) anneal examples/compiler.flow examples/project.flow

acceptance: test benchmark autopoiesis-check security-test

test: $(FLOWC) reload-test live-reload-test backend-reload-test generated-reload-test adaptive-test bitspace-test plugin-test project-test abi-test vertical-slice-test reload-stress-test fuzz-test ensemble-test smt-test mlir-llvm-test topology-test ebpf-pmu-test jit-migration-test bootstrap-sandbox-test quantum-dimension-test two-tier-chaos-test zero-tlb-shootdown-test epigenetic-mask-test dynamic-mask-superposition-test mtd-defense-test swarm-federation-test genetic-programming-test dynamic-env-morph-test qsbr-unified-test bitset-genome-test async-jit-worker-test orchestrator-test enterprise-production-test embodied-physics-test flowy-test mechanism-audit-test
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
	rm -rf $(BUILD_DIR) generated/*.c
