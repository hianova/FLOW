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
SRC_LIB_FVEC    := $(wildcard src/fvec/*.c)
SRC_LIB_JET     := $(wildcard src/jet/*.c)
SRC_LIB_TOPO    := $(wildcard src/topo/*.c)
SRC_LIB_INSPECT := $(wildcard src/inspect/*.c)
SRC_LIB_PLUGINS := $(wildcard plugins/*.c)
SRC_LIB := $(SRC_LIB_ROOT) $(SRC_LIB_FVEC) $(SRC_LIB_JET) $(SRC_LIB_TOPO) $(SRC_LIB_INSPECT) $(SRC_LIB_PLUGINS)

LIB_OBJS := \
  $(patsubst src/%.c,              $(BUILD_DIR)/obj/%.o,          $(SRC_LIB_ROOT)) \
  $(patsubst src/fvec/%.c,         $(BUILD_DIR)/obj/fvec_%.o,     $(SRC_LIB_FVEC)) \
  $(patsubst src/jet/%.c,          $(BUILD_DIR)/obj/jet_%.o,      $(SRC_LIB_JET)) \
  $(patsubst src/topo/%.c,         $(BUILD_DIR)/obj/topo_%.o,     $(SRC_LIB_TOPO)) \
  $(patsubst src/inspect/%.c,      $(BUILD_DIR)/obj/inspect_%.o,  $(SRC_LIB_INSPECT)) \
  $(patsubst plugins/%.c,          $(BUILD_DIR)/obj/plugin_%.o,   $(SRC_LIB_PLUGINS))

AR ?= ar
RANLIB ?= ranlib

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include/flow

PLUGINS_SO := $(BUILD_DIR)/libflow_embodied.so $(BUILD_DIR)/libflow_swarm.so

.PHONY: all clean test security-test install uninstall fuzz test-build test-run plugins flowy libflow test-cubical-hott

all: $(LIBFLOW_A) $(FLOWC) $(FLOWY) plugins

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


$(BUILD_DIR)/obj/%.o: src/%.c $(wildcard src/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(BUILD_DIR)/obj/fvec_%.o: src/fvec/%.c $(wildcard src/*.h) $(wildcard src/fvec/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(BUILD_DIR)/obj/jet_%.o: src/jet/%.c $(wildcard src/*.h) $(wildcard src/jet/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(BUILD_DIR)/obj/topo_%.o: src/topo/%.c $(wildcard src/*.h) $(wildcard src/topo/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(BUILD_DIR)/obj/inspect_%.o: src/inspect/%.c $(wildcard src/*.h) $(wildcard src/inspect/*.h) | $(BUILD_DIR)
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

$(BUILD_DIR)/libflow_embodied.so: plugins/embodied.c plugins/flow_embodied_mz.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc -Iplugins plugins/embodied.c plugins/flow_embodied_mz.c -o $@ $(LDLIBS)

$(BUILD_DIR)/libflow_swarm.so: src/swarm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc src/swarm.c -o $@ $(LDLIBS)

# ==============================================================================
# Two-Stage Native Makefile Test Pipeline (Native Hermetic Barrier)
# ==============================================================================

# 5 Consolidated Domain Test Suites (Brain, Body, Concurrency, Fvec-Swarm, System)
TEST_BINARIES := \
	$(BUILD_DIR)/test-brain \
	$(BUILD_DIR)/test-body \
	$(BUILD_DIR)/test-concurrency \
	$(BUILD_DIR)/test-fvec-swarm \
	$(BUILD_DIR)/test-system \
	$(BUILD_DIR)/test-refactored-core \
	$(BUILD_DIR)/test-geometric-axiom \
	$(BUILD_DIR)/test-cubical-hott \


$(BUILD_DIR)/test-brain: tests/test_brain.c $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-body: tests/test_body.c $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-concurrency: tests/test_concurrency.c $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-fvec-swarm: tests/test_fvec_swarm.c $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-system: tests/test_system.c $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-refactored-core: tests/test_refactored_core.c $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-geometric-axiom: tests/test_geometric_axiom.c $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-cubical-hott: tests/test_cubical_hott.c $(FLOWC) $(FLOWY) plugins $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)


# Backward-compatible convenience shortcuts to build & execute any domain test
TEST_NAMES := $(patsubst $(BUILD_DIR)/%,%,$(TEST_BINARIES))
.PHONY: $(TEST_NAMES)
$(TEST_NAMES): %: $(BUILD_DIR)/%
	@$<

fuzz: | $(BUILD_DIR)
	clang -std=c17 -O2 -fsanitize=fuzzer,address,undefined -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -Isrc $(SRC_LIB) tests/fuzz-test.c -o $(BUILD_DIR)/fuzzer-engine -lm
	@echo "Running LLVM libFuzzer for 5 seconds..."
	$(BUILD_DIR)/fuzzer-engine -max_total_time=5

security-test: $(BUILD_DIR)/test-concurrency
	@$(BUILD_DIR)/test-concurrency

# Phase 1: Pure Compilation Barrier (Max CPU parallelism with make -j)
test-build: $(FLOWC) $(FLOWY) plugins $(TEST_BINARIES)

# Phase 2: Pure Isolated Execution (Hardware Cache & SMT Soundness Protected)
test-run: $(TEST_BINARIES)
	@echo "=== [1/8] Brain: Bit-Manifold Form, SMT Supreme Court & Algebraic Geometry ==="
	@$(BUILD_DIR)/test-brain
	@echo "=== [2/8] Body: Physical Substrate, Telemetry, Drivers & Hardware Control ==="
	@$(BUILD_DIR)/test-body
	@echo "=== [3/8] Concurrency: QSBR RCU, Live Reload, Zero-TLB & Moving Target Defense ==="
	@$(BUILD_DIR)/test-concurrency
	@echo "=== [4/8] Fvec & Swarm: Architectural Memory, Swarm Federation, Neuro-Bridge & Pre-Play ==="
	@$(BUILD_DIR)/test-fvec-swarm
	@echo "=== [5/8] System: Compiler Pipeline, Pure .lean Fusion, Plugin ABI & Polyglot Emitter ==="
	@$(BUILD_DIR)/test-system
	@echo "=== [6/8] Refactored Core: Thermal Contact-Jet, Quadratic Polyhedral & 64-Node Mesh ==="
	@$(BUILD_DIR)/test-refactored-core
	@echo "=== [7/8] Geometric Axiom: Unified Fiber Bundle Section (SMT + Polyhedral + BMF + Jet) ==="
	@$(BUILD_DIR)/test-geometric-axiom
	@echo "=== [8/8] Cubical HoTT: Discrete Cubical Sets, Kan Fillers & Topos Subobject Classifier ==="
	@$(BUILD_DIR)/test-cubical-hott

test: test-build
	@$(MAKE) --no-print-directory test-run
	@echo "================================================================================"
	@echo "            ALL 8 DOMAIN TEST SUITES 100% SOUND & PASSED!                       "
	@echo "================================================================================"

clean:
	rm -rf $(BUILD_DIR) /tmp/flow-* /tmp/d_* /tmp/test_*
