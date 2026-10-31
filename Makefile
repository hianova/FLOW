export PATH := /opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
FLOW_VERSION := $(shell tr -d '[:space:]' < VERSION 2>/dev/null || echo "2.6.0")
GIT_BRANCH   := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "detached")
GIT_COMMIT   := $(shell git rev-parse --short=8 HEAD 2>/dev/null || echo "unknown")
GIT_DIRTY    := $(shell git status --porcelain -uno 2>/dev/null | grep -q . && echo "dirty" || echo "clean")
PROVENANCE_DEFS := -DFLOW_VERSION_STRING=\"$(FLOW_VERSION)\" \
                   -DFLOW_GIT_BRANCH=\"$(GIT_BRANCH)\" \
                   -DFLOW_GIT_COMMIT=\"$(GIT_COMMIT)\" \
                   -DFLOW_GIT_DIRTY=\"$(GIT_DIRTY)\"
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2 -Isrc -Iplugins -Itests
CFLAGS += $(PROVENANCE_DEFS)
LDLIBS ?= -lm
THREAD_FLAGS ?= -pthread

BUILD_DIR := build
FLOWC := $(BUILD_DIR)/flowc
FLOWY := $(BUILD_DIR)/flowy
LIBFLOW_A := $(BUILD_DIR)/libflow.a
LIBFLOW_RT_A := $(BUILD_DIR)/libflow_rt.a
PLUGINS_SO := $(BUILD_DIR)/libflow_embodied.so $(BUILD_DIR)/libflow_cryptanalysis.so

SRC_LIB_ROOT := $(filter-out src/flowc.c src/flowy_main.c src/flowy.c,$(wildcard src/*.c))
LIB_OBJS := $(patsubst src/%.c, $(BUILD_DIR)/obj/%.o, $(SRC_LIB_ROOT))

RT_OBJS := \
  $(BUILD_DIR)/obj/flow_jet.o \
  $(BUILD_DIR)/obj/reload.o \
  $(BUILD_DIR)/obj/cubical_hott.o

AR ?= ar
RANLIB ?= ranlib

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include/flow

.PHONY: all clean test test-cli install uninstall plugins flowy libflow libflow_rt

all: $(LIBFLOW_A) $(LIBFLOW_RT_A) $(FLOWC) $(FLOWY) plugins

install: $(FLOWC) $(FLOWY) $(LIBFLOW_A) $(LIBFLOW_RT_A) plugins
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(FLOWC) $(DESTDIR)$(BINDIR)/flowc
	install -m 755 $(FLOWY) $(DESTDIR)$(BINDIR)/flowy
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(LIBFLOW_A) $(DESTDIR)$(PREFIX)/lib/libflow.a
	install -m 644 $(LIBFLOW_RT_A) $(DESTDIR)$(PREFIX)/lib/libflow_rt.a
	mkdir -p $(DESTDIR)$(PREFIX)/lib/flow
	install -m 755 $(PLUGINS_SO) $(DESTDIR)$(PREFIX)/lib/flow/
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	install -m 644 src/*.h plugins/*.h $(DESTDIR)$(INCLUDEDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/flowc $(DESTDIR)$(BINDIR)/flowy $(DESTDIR)$(PREFIX)/lib/libflow.a $(DESTDIR)$(PREFIX)/lib/libflow_rt.a
	rm -rf $(DESTDIR)$(PREFIX)/lib/flow
	rm -rf $(DESTDIR)$(INCLUDEDIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/obj/%.o: src/%.c $(wildcard src/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(LIBFLOW_A): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(LIBFLOW_RT_A): $(RT_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

libflow: $(LIBFLOW_A)
libflow_rt: $(LIBFLOW_RT_A)

$(FLOWC): src/flowc.c $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS) $(THREAD_FLAGS)

$(FLOWY): src/flowy_main.c src/flowy.c $(LIBFLOW_A) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc src/flowy_main.c src/flowy.c $(LIBFLOW_A) -o $@ $(LDLIBS) $(THREAD_FLAGS)

flowy: $(FLOWY)

plugins: $(PLUGINS_SO)

$(BUILD_DIR)/libflow_embodied.so: plugins/embodied.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc -Iplugins $< -o $@ $(LDLIBS)

$(BUILD_DIR)/libflow_cryptanalysis.so: plugins/cryptanalysis.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -fPIC -DFLOW_PLUGIN_DSO -undefined dynamic_lookup $(THREAD_FLAGS) -Isrc -Iplugins $< -o $@ $(LDLIBS)

# ==============================================================================
# Domain Test Pipeline
# ==============================================================================
TEST_DEFS := -DFLOW_BUILD_DIR=\"$(BUILD_DIR)\"
TEST_DEPS := tests/hott_test_kit.h $(FLOWC) $(FLOWY) $(PLUGINS_SO) $(LIBFLOW_A) $(LIBFLOW_RT_A)

TEST_BINARIES := \
	$(BUILD_DIR)/test-cubical-hott \
	$(BUILD_DIR)/test-concurrency \
	$(BUILD_DIR)/test-system \
	$(BUILD_DIR)/fuzz-test

$(BUILD_DIR)/test-cubical-hott: tests/test_cubical_hott.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-concurrency: tests/test_concurrency.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-system: tests/test_system.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/fuzz-test: tests/fuzz-test.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DEFS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

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
	@$(FLOWY) induce --benchmark >/dev/null
	@echo "  ✓ flowc & flowy CLI smoke tests passed."

test: $(TEST_BINARIES) test-cli
	@echo "=== [1/4] Cubical HoTT: Discrete Cubical Sets, Kan Fillers, Topos Subobject Classifier & U_0 ==="
	@$(BUILD_DIR)/test-cubical-hott
	@echo "=== [2/4] Concurrency: QSBR RCU, Zero-Downtime Hot Reload & Zero-TLB Dual-Mapped Pool ==="
	@$(BUILD_DIR)/test-concurrency
	@echo "=== [3/4] System: Pure Lean 4 Compilation, Jet Bundles, Lossless Fixed-Point & RT Runtime ==="
	@$(BUILD_DIR)/test-system
	@echo "=== [4/4] Fuzz: Parser Boundary Corpus & Pseudo-Random BMF Mutation Engine ==="
	@$(BUILD_DIR)/fuzz-test
	@echo "================================================================================"
	@echo "        ALL TEST SUITES + CLI SMOKE TESTS 100% SOUND & PASSED!           "
	@echo "================================================================================"

clean:
	rm -rf $(BUILD_DIR) /tmp/flow_* /tmp/libflow_*
