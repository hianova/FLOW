export PATH := /opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
PREFIX ?= /usr/local
FLOW_VERSION := $(shell tr -d '[:space:]' < VERSION 2>/dev/null || echo "5.0.0")
GIT_BRANCH   := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "detached")
GIT_COMMIT   := $(shell git rev-parse --short=8 HEAD 2>/dev/null || echo "unknown")
GIT_DIRTY    := $(shell git status --porcelain -uno 2>/dev/null | grep -q . && echo "dirty" || echo "clean")
PROVENANCE_DEFS := -DFLOW_VERSION_STRING=\"$(FLOW_VERSION)\" \
                   -DFLOW_GIT_BRANCH=\"$(GIT_BRANCH)\" \
                   -DFLOW_GIT_COMMIT=\"$(GIT_COMMIT)\" \
                   -DFLOW_GIT_DIRTY=\"$(GIT_DIRTY)\"
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2 -Isrc -Itests
override CFLAGS += -Wundef -Isrc -Itests $(PROVENANCE_DEFS) -D_GNU_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
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
LIBFLOW_A := $(BUILD_DIR)/libflow.a
LIBFLOW_RT_A := $(BUILD_DIR)/libflow_rt.a

CORE_OBJ := $(BUILD_DIR)/obj/flow_qpoly.o
RT_OBJS := $(BUILD_DIR)/obj/flow_qpoly.o $(BUILD_DIR)/obj/flow_core.o

AR ?= ar
RANLIB ?= ranlib
LEAN ?= lean
LEANC ?= leanc
LEAN_FLAGS ?= -DwarningAsError=true
LEAN_BUILD := $(BUILD_DIR)/lean
SSOT_GEN_H := src/flow_ssot_gen.h

BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include/flow

.DEFAULT_GOAL := all
.PHONY: all clean test install uninstall libflow libflow_rt fmt fmt-check ssot-gen ssot-check

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
	install -m 644 src/*.h $(DESTDIR)$(INCLUDEDIR)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/libflow.a $(DESTDIR)$(PREFIX)/lib/libflow_rt.a
	rm -rf $(DESTDIR)$(INCLUDEDIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/obj/flow_qpoly.o: src/flow_qpoly.c $(wildcard src/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD_DIR)/obj/flow_core.o: src/flow_core.c $(wildcard src/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) $(THREAD_FLAGS) -Isrc -c $< -o $@

$(LIBFLOW_A): $(CORE_OBJ)
	@test -z "$$(nm -u $(CORE_OBJ))" || (echo "ERROR: Undefined symbols in freestanding libflow.a:" && nm -u $(CORE_OBJ) && exit 1)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(LIBFLOW_RT_A): $(RT_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@
	@! nm -u $@ | grep -E '_?(malloc|calloc|realloc|free)$$' >/dev/null || (echo "ERROR: Heap allocation symbols found in libflow_rt.a!" && nm -u $@ && exit 1)

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

FREESTANDING_CFLAGS := -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -DFLOW_FREESTANDING=1 \
                       -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2 -Isrc
LLVM_CLANG := /opt/homebrew/opt/llvm/bin/clang
LLVM_NM    := /opt/homebrew/opt/llvm/bin/llvm-nm

$(BUILD_DIR)/test_freestanding_kernel.o: tests/test_freestanding_kernel.c src/flow_qpoly.c src/flow_asm.h src/flow_hott_jet.h $(SSOT_GEN_H) | $(BUILD_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c $< -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding Ring-0 object:" && nm -u $@ && exit 1)
	$(CC) $(FREESTANDING_CFLAGS) -c src/flow_qpoly.c -o $(BUILD_DIR)/flow_qpoly_freestanding.o
	@test -z "$$(nm -u $(BUILD_DIR)/flow_qpoly_freestanding.o)" || (echo "ERROR: Undefined symbols in freestanding flow_qpoly.o:" && nm -u $(BUILD_DIR)/flow_qpoly_freestanding.o && exit 1)

$(BUILD_DIR)/test_freestanding_mcu32.o: tests/test_freestanding_kernel.c src/flow_qpoly.c src/flow_asm.h src/flow_hott_jet.h $(SSOT_GEN_H) | $(BUILD_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -DFLOW_FORCE_C17_FALLBACK=1 -DFLOW_FORCE_32BIT_FALLBACK=1 -c $< -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in 32-bit MCU freestanding object:" && nm -u $@ && exit 1)
	$(CC) $(FREESTANDING_CFLAGS) -DFLOW_FORCE_C17_FALLBACK=1 -DFLOW_FORCE_32BIT_FALLBACK=1 -c src/flow_qpoly.c -o $(BUILD_DIR)/flow_qpoly_mcu32.o
	@test -z "$$(nm -u $(BUILD_DIR)/flow_qpoly_mcu32.o)" || (echo "ERROR: Undefined symbols in 32-bit MCU flow_qpoly.o:" && nm -u $(BUILD_DIR)/flow_qpoly_mcu32.o && exit 1)
	@$(CC) -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -DFLOW_FREESTANDING=1 -Wall -Wextra -Wpedantic -Wundef -Werror -O2 -Isrc -c $< -o $(BUILD_DIR)/test_standalone_hdr.o
	@test -z "$$(nm -u $(BUILD_DIR)/test_standalone_hdr.o)" || (echo "ERROR: Undefined symbols in standalone -Isrc object:" && nm -u $(BUILD_DIR)/test_standalone_hdr.o && exit 1)
	@if [ -x "$(LLVM_CLANG)" ] && [ -x "$(LLVM_NM)" ]; then \
		$(LLVM_CLANG) --target=riscv32-unknown-none-elf -march=rv32imac -mabi=ilp32 -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -DFLOW_FREESTANDING=1 -Wall -Wextra -Wundef -Werror -O2 -Isrc -c $< -o $(BUILD_DIR)/test_rv32_o2.o && \
		test -z "$$($(LLVM_NM) -u $(BUILD_DIR)/test_rv32_o2.o)" && \
		$(LLVM_CLANG) --target=riscv32-unknown-none-elf -march=rv32imac -mabi=ilp32 -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -DFLOW_FREESTANDING=1 -Wall -Wextra -Wundef -Werror -O2 -Isrc -c src/flow_qpoly.c -o $(BUILD_DIR)/qpoly_rv32_o2.o && \
		test -z "$$($(LLVM_NM) -u $(BUILD_DIR)/qpoly_rv32_o2.o)" && \
		$(LLVM_CLANG) --target=riscv32-unknown-none-elf -march=rv32imac -mabi=ilp32 -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -DFLOW_FREESTANDING=1 -Wall -Wextra -Wundef -Werror -O0 -Isrc -c $< -o $(BUILD_DIR)/test_rv32_o0.o && \
		test -z "$$($(LLVM_NM) -u $(BUILD_DIR)/test_rv32_o0.o)" && \
		$(LLVM_CLANG) --target=riscv32-unknown-none-elf -march=rv32imac -mabi=ilp32 -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -DFLOW_FREESTANDING=1 -Wall -Wextra -Wundef -Werror -O0 -Isrc -c src/flow_qpoly.c -o $(BUILD_DIR)/qpoly_rv32_o0.o && \
		test -z "$$($(LLVM_NM) -u $(BUILD_DIR)/qpoly_rv32_o0.o)" && \
		$(LLVM_CLANG) --target=armv7m-none-eabi -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -DFLOW_FREESTANDING=1 -Wall -Wextra -Wundef -Werror -O2 -Isrc -c $< -o $(BUILD_DIR)/test_armv7m.o && \
		test -z "$$($(LLVM_NM) -u $(BUILD_DIR)/test_armv7m.o)" && \
		$(LLVM_CLANG) --target=armv7m-none-eabi -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -DFLOW_FREESTANDING=1 -Wall -Wextra -Wundef -Werror -O2 -Isrc -c src/flow_qpoly.c -o $(BUILD_DIR)/qpoly_armv7m.o && \
		test -z "$$($(LLVM_NM) -u $(BUILD_DIR)/qpoly_armv7m.o)"; \
	fi

$(LEAN_BUILD)/FlowSpec.olean $(LEAN_BUILD)/FlowSpec.c: FlowSpec.lean | $(BUILD_DIR)
	@mkdir -p $(LEAN_BUILD)
	$(LEAN) $(LEAN_FLAGS) -o $(LEAN_BUILD)/FlowSpec.olean -c $(LEAN_BUILD)/FlowSpec.c FlowSpec.lean

$(LEAN_BUILD)/FlowGen.olean $(LEAN_BUILD)/FlowGen.c: FlowGen.lean $(LEAN_BUILD)/FlowSpec.olean | $(BUILD_DIR)
	@mkdir -p $(LEAN_BUILD)
	LEAN_PATH=$(LEAN_BUILD) $(LEAN) $(LEAN_FLAGS) -o $(LEAN_BUILD)/FlowGen.olean -c $(LEAN_BUILD)/FlowGen.c FlowGen.lean

$(BUILD_DIR)/flow-gen: $(LEAN_BUILD)/FlowSpec.c $(LEAN_BUILD)/FlowGen.c
	$(LEANC) -O3 $(LEAN_BUILD)/FlowSpec.c $(LEAN_BUILD)/FlowGen.c -o $@

$(LEAN_BUILD)/FlowVerify.olean $(LEAN_BUILD)/FlowVerify.c: FlowVerify.lean $(LEAN_BUILD)/FlowSpec.olean | $(BUILD_DIR)
	@mkdir -p $(LEAN_BUILD)
	LEAN_PATH=$(LEAN_BUILD) $(LEAN) $(LEAN_FLAGS) -o $(LEAN_BUILD)/FlowVerify.olean -c $(LEAN_BUILD)/FlowVerify.c FlowVerify.lean

$(BUILD_DIR)/obj/flow_verify_native.o: tests/flow_verify_ffi.c $(wildcard src/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) -DFLOW_FFI_PREFIX=flow_native -Isrc -c $< -o $@

$(BUILD_DIR)/obj/flow_verify_mcu32.o: tests/flow_verify_ffi.c $(wildcard src/*.h) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/obj
	$(CC) $(CFLAGS) -DFLOW_FORCE_C17_FALLBACK=1 -DFLOW_FORCE_32BIT_FALLBACK=1 -DFLOW_FFI_PREFIX=flow_mcu32 -Isrc -c $< -o $@

$(BUILD_DIR)/test-lean-ssot: $(LEAN_BUILD)/FlowSpec.c $(LEAN_BUILD)/FlowVerify.c $(BUILD_DIR)/obj/flow_verify_native.o $(BUILD_DIR)/obj/flow_verify_mcu32.o $(LIBFLOW_RT_A)
	$(LEANC) -O3 $(LEAN_BUILD)/FlowSpec.c $(LEAN_BUILD)/FlowVerify.c $(BUILD_DIR)/obj/flow_verify_native.o $(BUILD_DIR)/obj/flow_verify_mcu32.o $(LIBFLOW_RT_A) -o $@

ssot-gen: $(BUILD_DIR)/flow-gen
	./$(BUILD_DIR)/flow-gen $(SSOT_GEN_H)

ssot-check: $(BUILD_DIR)/flow-gen $(BUILD_DIR)/test-lean-ssot
	@echo "=== [Gate 0] Lean 4 SSOT Formal Kernel, Sealed Header & 3-Way Equivalence ==="
	@./$(BUILD_DIR)/flow-gen --check $(SSOT_GEN_H)
	@./$(BUILD_DIR)/test-lean-ssot

test: $(LIBFLOW_A) ssot-check $(TEST_BINARIES) $(BUILD_DIR)/test_freestanding_kernel.o $(BUILD_DIR)/test_freestanding_mcu32.o
	@echo "=== [0/3] Bare-Metal / Ring-0 Freestanding Verification (-ffreestanding -nostdlib, 0 undefined symbols) ==="
	@echo "  [PASS] test_freestanding_kernel.o & test_freestanding_mcu32.o (__STDC_HOSTED__ == 0, nm -u == empty)"
	@echo "=== [1/3] Total ASM Substrate, Hardware Sieve & 2-Jet Self-Bootstrapping ==="
	@$(BUILD_DIR)/test-asm-sieve --report
	@echo "=== [2/3] Universal External-Constraint ABI (b_ext, u_ext), Hysteresis & Anti-Tunneling ==="
	@$(BUILD_DIR)/test-ext-constraint
	@echo "=== [3/3] Q-Polyhedron Quantization, 2-Jet Forecast & Zero-Fault Hot Path (PoC A-E) ==="
	@$(BUILD_DIR)/test-qpoly-poc --report
	@echo "================================================================================"
	@echo "   ALL FLOW TEST SUITES PASSED!"
	@echo "================================================================================"

clean:
	rm -rf $(BUILD_DIR) /tmp/flow_* /tmp/libflow_*
