export PATH := /opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
PREFIX ?= /usr/local
FLOW_VERSION := $(shell tr -d '[:space:]' < VERSION 2>/dev/null || echo "3.0.0")
GIT_BRANCH   := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "detached")
GIT_COMMIT   := $(shell git rev-parse --short=8 HEAD 2>/dev/null || echo "unknown")
GIT_DIRTY    := $(shell git status --porcelain -uno 2>/dev/null | grep -q . && echo "dirty" || echo "clean")
PROVENANCE_DEFS := -DFLOW_VERSION_STRING=\"$(FLOW_VERSION)\" \
                   -DFLOW_GIT_BRANCH=\"$(GIT_BRANCH)\" \
                   -DFLOW_GIT_COMMIT=\"$(GIT_COMMIT)\" \
                   -DFLOW_GIT_DIRTY=\"$(GIT_DIRTY)\"
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Wmissing-prototypes -Werror -O2 -Isrc -Itests
CFLAGS += $(PROVENANCE_DEFS)
IS_GCC := $(shell $(CC) -dM -E - </dev/null 2>/dev/null | grep -q "__GNUC__" && ! $(CC) -dM -E - </dev/null 2>/dev/null | grep -q "__clang__" && echo 1 || echo 0)
ifeq ($(IS_GCC),1)
CFLAGS += -Wno-format-truncation -Wno-stringop-truncation
endif
LDLIBS ?= -lm
THREAD_FLAGS ?= -pthread

BUILD_DIR := build
FLOWC := $(BUILD_DIR)/flowc
LIBFLOW_A := $(BUILD_DIR)/libflow.a
LIBFLOW_RT_A := $(BUILD_DIR)/libflow_rt.a

SRC_LIB_ROOT := $(filter-out src/flowc.c,$(wildcard src/*.c))
LIB_OBJS := $(patsubst src/%.c, $(BUILD_DIR)/obj/%.o, $(SRC_LIB_ROOT))

RT_OBJS := \
  $(BUILD_DIR)/obj/flow_jet.o \
  $(BUILD_DIR)/obj/reload.o \
  $(BUILD_DIR)/obj/cubical_hott.o \
  $(BUILD_DIR)/obj/bitspace.o \
  $(BUILD_DIR)/obj/flow_bridge.o

AR ?= ar
RANLIB ?= ranlib

BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include/flow

.DEFAULT_GOAL := all
.PHONY: all clean test test-cli install uninstall libflow libflow_rt fmt fmt-check

all: $(LIBFLOW_A) $(LIBFLOW_RT_A) $(FLOWC)

CLANG_FORMAT ?= clang-format
FMT_FILES := $(wildcard src/*.c src/*.h tests/*.c tests/*.h)

fmt:
	$(CLANG_FORMAT) -i $(FMT_FILES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FMT_FILES)

install: $(FLOWC) $(LIBFLOW_A) $(LIBFLOW_RT_A)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(FLOWC) $(DESTDIR)$(BINDIR)/flowc
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(LIBFLOW_A) $(DESTDIR)$(PREFIX)/lib/libflow.a
	install -m 644 $(LIBFLOW_RT_A) $(DESTDIR)$(PREFIX)/lib/libflow_rt.a
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	install -m 644 src/*.h $(DESTDIR)$(INCLUDEDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/flowc $(DESTDIR)$(PREFIX)/lib/libflow.a $(DESTDIR)$(PREFIX)/lib/libflow_rt.a
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

TEST_DEPS := tests/hott_test_kit.h $(FLOWC) $(LIBFLOW_A) $(LIBFLOW_RT_A)

TEST_BINARIES := \
	$(BUILD_DIR)/test-hott-jet \
	$(BUILD_DIR)/test-compiler-lifter \
	$(BUILD_DIR)/fuzz-test

$(BUILD_DIR)/test-hott-jet: tests/test_hott_jet.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-compiler-lifter: tests/test_compiler_lifter.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/fuzz-test: tests/fuzz-test.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_A) -o $@ $(LDLIBS)

test-cli: $(FLOWC)
	@echo "=== [CLI] Testing flowc CLI Interface ==="
	@$(FLOWC) -h >/dev/null
	@$(FLOWC) --help >/dev/null
	@$(FLOWC) --version >/dev/null
	@! $(FLOWC) >/dev/null 2>&1
	@! $(FLOWC) /nonexistent/file.lean >/dev/null 2>&1
	@echo "  ✓ flowc CLI smoke tests passed."

test: $(TEST_BINARIES) test-cli
	@echo "=== [1/3] HoTT x 2-Jet x Univalence Mathematical Core ==="
	@$(BUILD_DIR)/test-hott-jet
	@echo "=== [2/3] Pure Lean 4 <-> C17 / LLVM SSA Bidirectional Compiler & Lifter ==="
	@$(BUILD_DIR)/test-compiler-lifter
	@echo "=== [3/3] Fuzz: Parser Boundary Corpus & Pseudo-Random BMF Mutation Engine ==="
	@$(BUILD_DIR)/fuzz-test
	@echo "================================================================================"
	@echo "        ALL TEST SUITES + CLI SMOKE TESTS 100% SOUND & PASSED!           "
	@echo "================================================================================"

clean:
	rm -rf $(BUILD_DIR) /tmp/flow_* /tmp/libflow_*
