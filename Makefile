export PATH := /opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2 -Isrc -Iplugins -Itests
LDLIBS ?= -lm
THREAD_FLAGS ?= -pthread

BUILD_DIR := build
FLOWC := $(BUILD_DIR)/flowc
FLOWY := $(BUILD_DIR)/flowy
LIBFLOW_A := $(BUILD_DIR)/libflow.a

SRC_LIB_ROOT    := $(filter-out src/flowc.c src/flowy_main.c,$(wildcard src/*.c))
SRC_LIB_PLUGINS := $(wildcard plugins/*.c)
SRC_LIB := $(SRC_LIB_ROOT) $(SRC_LIB_PLUGINS)

LIB_OBJS := \
  $(patsubst src/%.c,              $(BUILD_DIR)/obj/%.o,          $(SRC_LIB_ROOT)) \
  $(patsubst plugins/%.c,          $(BUILD_DIR)/obj/plugin_%.o,   $(SRC_LIB_PLUGINS))

AR ?= ar
RANLIB ?= ranlib

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include/flow

PLUGINS_SO := $(BUILD_DIR)/libflow_embodied.so

.PHONY: all clean test test-cli security-test install uninstall fuzz test-build test-run test-asan test-tsan plugins flowy libflow

all: $(LIBFLOW_A) $(FLOWC) $(FLOWY) plugins

install: $(FLOWC) $(FLOWY) $(LIBFLOW_A)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(FLOWC) $(DESTDIR)$(BINDIR)/flowc
	install -m 755 $(FLOWY) $(DESTDIR)$(BINDIR)/flowy
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(LIBFLOW_A) $(DESTDIR)$(PREFIX)/lib/libflow.a
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	install -m 644 src/*.h plugins/*.h $(DESTDIR)$(INCLUDEDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/flowc $(DESTDIR)$(BINDIR)/flowy $(DESTDIR)$(PREFIX)/lib/libflow.a
	rm -rf $(DESTDIR)$(INCLUDEDIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)


$(BUILD_DIR)/obj/%.o: src/%.c $(wildcard src/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(BUILD_DIR)/obj/plugin_%.o: plugins/%.c $(wildcard src/*.h) $(wildcard plugins/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -Iplugins -c $< -o $@


$(LIBFLOW_A): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

libflow: $(LIBFLOW_A)

$(FLOWC): src/flowc.c $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/flowc.c $(LIBFLOW_A) -o $@ $(LDLIBS) $(THREAD_FLAGS)

$(FLOWY): src/flowy_main.c $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/flowy_main.c $(LIBFLOW_A) -o $@ $(LDLIBS) $(THREAD_FLAGS)

flowy: $(FLOWY)

plugins: $(PLUGINS_SO)

$(BUILD_DIR)/libflow_embodied.so: plugins/embodied.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc -Iplugins plugins/embodied.c -o $@ $(LDLIBS)
	@if [ ! -f target/debug/libflow_plugin_example.dylib ] && [ ! -f target/debug/libflow_plugin_example.so ] && command -v cargo >/dev/null 2>&1; then cargo build --workspace --quiet && rm -f Cargo.lock; fi

# ==============================================================================
# Two-Stage Native Makefile Test Pipeline (Native Hermetic Barrier)
# ==============================================================================

# Tests locate sibling binaries (flowc/flowy) via FLOW_BUILD_DIR so that
# sanitizer builds (BUILD_DIR=build/asan, build/tsan) are fully self-contained.
TEST_DEFS := -DFLOW_BUILD_DIR=\"$(BUILD_DIR)\"
TEST_DEPS := tests/flow_test_kit.h $(FLOWC) $(FLOWY) $(PLUGINS_SO) $(LIBFLOW_A)

# 8 Consolidated Domain Test Suites + Fuzz Harness
TEST_BINARIES := \
	$(BUILD_DIR)/test-brain \
	$(BUILD_DIR)/test-body \
	$(BUILD_DIR)/test-concurrency \
	$(BUILD_DIR)/test-fvec-swarm \
	$(BUILD_DIR)/test-system \
	$(BUILD_DIR)/test-refactored-core \
	$(BUILD_DIR)/test-geometric-axiom \
	$(BUILD_DIR)/test-cubical-hott \
	$(BUILD_DIR)/fuzz-test


$(BUILD_DIR)/test-brain: tests/test_brain.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-body: tests/test_body.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-concurrency: tests/test_concurrency.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-fvec-swarm: tests/test_fvec_swarm.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-system: tests/test_system.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-refactored-core: tests/test_refactored_core.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-geometric-axiom: tests/test_geometric_axiom.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-cubical-hott: tests/test_cubical_hott.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/fuzz-test: tests/fuzz-test.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)


# Backward-compatible convenience shortcuts to build & execute any domain test
TEST_NAMES := $(patsubst $(BUILD_DIR)/%,%,$(TEST_BINARIES))
.PHONY: $(TEST_NAMES)
$(TEST_NAMES): %: $(BUILD_DIR)/%
	@$<

FUZZ_SECONDS ?= 60
fuzz: | $(BUILD_DIR)
	clang -std=c17 -g -O1 -fsanitize=fuzzer,address,undefined -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -Isrc -Iplugins $(SRC_LIB) tests/fuzz-test.c -o $(BUILD_DIR)/fuzzer-engine -lm -pthread
	@echo "Running LLVM libFuzzer for $(FUZZ_SECONDS) seconds..."
	$(BUILD_DIR)/fuzzer-engine -max_total_time=$(FUZZ_SECONDS)

# Sanitizer test pipelines (self-contained build trees; tests must be green here too).
SAN_COMMON := -std=c17 -Wall -Wextra -Wpedantic -g -O1 -fno-omit-frame-pointer -Isrc -Iplugins -Itests
test-asan:
	@$(MAKE) --no-print-directory BUILD_DIR=build/asan CFLAGS="$(SAN_COMMON) -fsanitize=address,undefined -fno-sanitize-recover=undefined" LDLIBS="-lm -fsanitize=address,undefined" test-build
	@ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 $(MAKE) --no-print-directory BUILD_DIR=build/asan test-run

test-tsan:
	@$(MAKE) --no-print-directory BUILD_DIR=build/tsan CFLAGS="$(SAN_COMMON) -fsanitize=thread" LDLIBS="-lm -fsanitize=thread" test-build
	@TSAN_OPTIONS=halt_on_error=1 $(MAKE) --no-print-directory BUILD_DIR=build/tsan test-run

security-test: $(BUILD_DIR)/test-concurrency
	@$(BUILD_DIR)/test-concurrency

# Phase 1: Pure Compilation Barrier (Max CPU parallelism with make -j)
test-build: $(FLOWC) $(FLOWY) plugins $(TEST_BINARIES)

# Phase 2: Pure Isolated Execution (Hardware Cache & SMT Soundness Protected)
test-run: $(TEST_BINARIES)
	@rm -f $(BUILD_DIR)/.failed-suites
	@echo "=== [1/8] Brain: Bit-Manifold Form, SMT Supreme Court & Algebraic Geometry ==="
	@$(BUILD_DIR)/test-brain || echo test-brain >> $(BUILD_DIR)/.failed-suites
	@echo "=== [2/8] Body: Physical Substrate, Telemetry, Drivers & Hardware Control ==="
	@$(BUILD_DIR)/test-body || echo test-body >> $(BUILD_DIR)/.failed-suites
	@echo "=== [3/8] Concurrency: QSBR RCU, Live Reload, Zero-TLB & Moving Target Defense ==="
	@$(BUILD_DIR)/test-concurrency || echo test-concurrency >> $(BUILD_DIR)/.failed-suites
	@echo "=== [4/8] Fvec & Swarm: Architectural Memory, Swarm Federation & Speculative Pre-Play ==="
	@$(BUILD_DIR)/test-fvec-swarm || echo test-fvec-swarm >> $(BUILD_DIR)/.failed-suites
	@echo "=== [5/8] System: Compiler Pipeline, Pure .lean Fusion, Plugin ABI & Polyglot Emitter ==="
	@$(BUILD_DIR)/test-system || echo test-system >> $(BUILD_DIR)/.failed-suites
	@echo "=== [6/8] Refactored Core: Thermal Contact-Jet, Quadratic Polyhedral & 64-Node Mesh ==="
	@$(BUILD_DIR)/test-refactored-core || echo test-refactored-core >> $(BUILD_DIR)/.failed-suites
	@echo "=== [7/8] Geometric Axiom: Unified Fiber Bundle Section (SMT + Polyhedral + BMF + Jet) ==="
	@$(BUILD_DIR)/test-geometric-axiom || echo test-geometric-axiom >> $(BUILD_DIR)/.failed-suites
	@echo "=== [8/8] Cubical HoTT: Discrete Cubical Sets, Kan Fillers & Topos Subobject Classifier ==="
	@$(BUILD_DIR)/test-cubical-hott || echo test-cubical-hott >> $(BUILD_DIR)/.failed-suites
	@echo "=== [Fuzz] Standalone Corpus & 1,000 BMF Mutation Fuzz Harness ==="
	@$(BUILD_DIR)/fuzz-test || echo fuzz-test >> $(BUILD_DIR)/.failed-suites
	@if [ -s $(BUILD_DIR)/.failed-suites ]; then echo "❌ FAILED SUITES:"; cat $(BUILD_DIR)/.failed-suites; exit 1; fi

test-cli: $(FLOWC) $(FLOWY)
	@echo "=== [CLI] Testing flowc and flowy CLI Interfaces ==="
	@$(FLOWC) -h >/dev/null
	@$(FLOWC) --help >/dev/null
	@$(FLOWC) --version >/dev/null
	@! $(FLOWC) >/dev/null 2>&1
	@! $(FLOWC) /nonexistent/file.lean >/dev/null 2>&1
	@$(FLOWY) -h >/dev/null
	@$(FLOWY) --version >/dev/null
	@$(FLOWY) audit >/dev/null
	@$(FLOWY) topos >/dev/null
	@$(FLOWY) inspect scope formal_sieve >/dev/null
	@$(FLOWY) inspect scope rust_sdk >/dev/null
	@if command -v cargo >/dev/null 2>&1; then cargo test --workspace --quiet && rm -f Cargo.lock && echo "  ✓ Rust Plugin SDK workspace unit tests passed (0 external crates)."; fi
	@echo "  ✓ flowc & flowy CLI smoke tests passed."

test: test-build
	@$(MAKE) --no-print-directory test-run
	@$(MAKE) --no-print-directory test-cli
	@echo "================================================================================"
	@echo "      ALL 8 DOMAIN TEST SUITES + CLI SMOKE TESTS 100% SOUND & PASSED!           "
	@echo "================================================================================"

help:
	@echo "FLOW Native C17 & Rust Build Pipeline (Makefile)"
	@echo "  make -j4 all        Build libflow.a, flowc, flowy, and libflow_embodied.so"
	@echo "  make -j4 test       Run all 8 domain test suites, fuzz harness, and CLI smoke tests"
	@echo "  make test-asan      Run all suites under AddressSanitizer + UBSan"
	@echo "  make test-tsan      Run all suites under ThreadSanitizer"
	@echo "  make fuzz           Run LLVM libFuzzer (FUZZ_SECONDS=60)"
	@echo "  make install        Install flowc, flowy, libflow.a, and headers to PREFIX=$(PREFIX)"
	@echo "  make clean          Remove build artifacts"

clean:
	rm -rf $(BUILD_DIR) build/asan build/tsan target Cargo.lock /tmp/flow_* /tmp/libflow_*
