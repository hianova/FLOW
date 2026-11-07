export PATH := /opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
PREFIX ?= /usr/local
FLOW_VERSION := $(shell tr -d '[:space:]' < VERSION 2>/dev/null || echo "4.0.1")
GIT_BRANCH   := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "detached")
GIT_COMMIT   := $(shell git rev-parse --short=8 HEAD 2>/dev/null || echo "unknown")
GIT_DIRTY    := $(shell git status --porcelain -uno 2>/dev/null | grep -q . && echo "dirty" || echo "clean")
PROVENANCE_DEFS := -DFLOW_VERSION_STRING=\"$(FLOW_VERSION)\" \
                   -DFLOW_GIT_BRANCH=\"$(GIT_BRANCH)\" \
                   -DFLOW_GIT_COMMIT=\"$(GIT_COMMIT)\" \
                   -DFLOW_GIT_DIRTY=\"$(GIT_DIRTY)\"
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Wmissing-prototypes -Werror -O2 -Isrc -Itests
override CFLAGS += -Isrc -Itests $(PROVENANCE_DEFS) -D_GNU_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
IS_GCC := $(shell $(CC) -dM -E - </dev/null 2>/dev/null | grep -q "__GNUC__" && ! $(CC) -dM -E - </dev/null 2>/dev/null | grep -q "__clang__" && echo 1 || echo 0)
ifeq ($(IS_GCC),1)
override CFLAGS += -Wno-format-truncation -Wno-stringop-truncation
endif
LDLIBS ?=
THREAD_FLAGS ?= -pthread
UNAME_S := $(shell uname -s 2>/dev/null || echo "Unknown")
ifeq ($(UNAME_S),Darwin)
STACK_LDFLAGS ?= -Wl,-stack_size,0x1000000
else ifeq ($(UNAME_S),Linux)
LDLIBS += -lrt
STACK_LDFLAGS ?=
else ifneq (,$(findstring MINGW,$(UNAME_S))$(findstring MSYS,$(UNAME_S))$(findstring CYGWIN,$(UNAME_S)))
STACK_LDFLAGS ?= -Wl,--stack,67108864
else
STACK_LDFLAGS ?=
endif

BUILD_DIR := build
HW_SPEC_H := $(BUILD_DIR)/flow_hw_spec.h
LIBFLOW_A := $(BUILD_DIR)/libflow.a
LIBFLOW_RT_A := $(BUILD_DIR)/libflow_rt.a

override CFLAGS += -I$(BUILD_DIR)

SRC_FILES := $(wildcard src/*.c)
RT_OBJS := $(patsubst src/%.c, $(BUILD_DIR)/obj/%.o, $(SRC_FILES))

AR ?= ar
RANLIB ?= ranlib

BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include/flow

.DEFAULT_GOAL := all
.PHONY: all clean test install uninstall libflow libflow_rt fmt fmt-check

all: $(LIBFLOW_A) $(LIBFLOW_RT_A)

CLANG_FORMAT ?= clang-format
FMT_FILES := $(wildcard src/*.c src/*.h tests/*.c tests/*.h)

fmt:
	$(CLANG_FORMAT) -i $(FMT_FILES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FMT_FILES)

install: $(LIBFLOW_A) $(LIBFLOW_RT_A)
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(LIBFLOW_A) $(DESTDIR)$(PREFIX)/lib/libflow.a
	install -m 644 $(LIBFLOW_RT_A) $(DESTDIR)$(PREFIX)/lib/libflow_rt.a
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	install -m 644 src/*.h $(HW_SPEC_H) $(DESTDIR)$(INCLUDEDIR)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/libflow.a $(DESTDIR)$(PREFIX)/lib/libflow_rt.a
	rm -rf $(DESTDIR)$(INCLUDEDIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(HW_SPEC_H): tools/gen_hw_spec.sh | $(BUILD_DIR)
	@CC="$(CC)" FLOW_HW_PAGE_BYTES="$(FLOW_HW_PAGE_BYTES)" FLOW_HW_CACHE_LINE_BYTES="$(FLOW_HW_CACHE_LINE_BYTES)" FLOW_HW_L1D_BYTES="$(FLOW_HW_L1D_BYTES)" FLOW_HW_VECTOR_BYTES="$(FLOW_HW_VECTOR_BYTES)" FLOW_HW_WAVE_LANES="$(FLOW_HW_WAVE_LANES)" ./tools/gen_hw_spec.sh > $@

$(RT_OBJS): $(HW_SPEC_H)

$(BUILD_DIR)/obj/%.o: src/%.c $(wildcard src/*.h) $(HW_SPEC_H) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(LIBFLOW_RT_A): $(RT_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(LIBFLOW_A): $(LIBFLOW_RT_A)
	cp $< $@

libflow: $(LIBFLOW_A)
libflow_rt: $(LIBFLOW_RT_A)

TEST_DEPS := tests/hott_test_kit.h $(LIBFLOW_RT_A)

TEST_BINARIES := \
	$(BUILD_DIR)/test-asm-sieve \
	$(BUILD_DIR)/test-ext-constraint \
	$(BUILD_DIR)/test-qpoly-poc

$(BUILD_DIR)/test-asm-sieve: tests/test_asm_sieve.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(STACK_LDFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_RT_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-ext-constraint: tests/test_ext_constraint.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(STACK_LDFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_RT_A) -o $@ $(LDLIBS)

$(BUILD_DIR)/test-qpoly-poc: tests/test_qpoly_poc.c $(TEST_DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(STACK_LDFLAGS) $(THREAD_FLAGS) -Isrc $< $(LIBFLOW_RT_A) -o $@ $(LDLIBS)

FREESTANDING_CFLAGS := -std=c11 -ffreestanding -nostdlib -fno-builtin -DFLOW_FREESTANDING=1 \
                       -Wall -Wextra -Wpedantic -Wmissing-prototypes -Werror -O2 -Isrc -I$(BUILD_DIR)

$(BUILD_DIR)/test_freestanding_kernel.o: tests/test_freestanding_kernel.c src/flow_asm.h src/flow_hott_jet.h $(HW_SPEC_H) | $(BUILD_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c $< -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding Ring-0 object:" && nm -u $@ && exit 1)

$(BUILD_DIR)/test_freestanding_mcu32.o: tests/test_freestanding_kernel.c src/flow_asm.h src/flow_hott_jet.h $(HW_SPEC_H) | $(BUILD_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -DFLOW_FORCE_C17_FALLBACK=1 -DFLOW_FORCE_32BIT_FALLBACK=1 -c $< -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in 32-bit MCU freestanding object:" && nm -u $@ && exit 1)

test: $(TEST_BINARIES) $(BUILD_DIR)/test_freestanding_kernel.o $(BUILD_DIR)/test_freestanding_mcu32.o
	@echo "=== [0/3] Bare-Metal / Ring-0 Freestanding Verification (-ffreestanding -nostdlib, 0 undefined symbols) ==="
	@echo "  [PASS] test_freestanding_kernel.o & test_freestanding_mcu32.o (__STDC_HOSTED__ == 0, nm -u == empty)"
	@echo "=== [1/3] Total ASM Substrate, Hardware Sieve & 2-Jet Self-Bootstrapping ==="
	@$(BUILD_DIR)/test-asm-sieve --report
	@echo "=== [2/3] Universal External-Constraint ABI (b_ext, u_ext), Hysteresis & Anti-Tunneling ==="
	@$(BUILD_DIR)/test-ext-constraint
	@echo "=== [3/3] Q-Polyhedron Quantization, 2-Jet Forecast & Zero-Fault Hot Path (PoC A-E) ==="
	@$(BUILD_DIR)/test-qpoly-poc --report
	@echo "================================================================================"
	@echo "   ALL CLEAN-SLATE V4 TEST SUITES PASSED!"
	@echo "================================================================================"

clean:
	rm -rf $(BUILD_DIR) /tmp/flow_* /tmp/libflow_*
