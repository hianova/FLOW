export PATH := $(HOME)/.elan/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
LEAN ?= lean
CLANG_FORMAT ?= clang-format
PREFIX ?= /usr/local

BUILD_DIR := build
CAPSULE_DIR := $(BUILD_DIR)/capsules
FLOW_VERSION := $(shell tr -d '[:space:]' < VERSION)

CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Wno-psabi -Werror -O2
override CFLAGS += -DFLOW_VERSION_STRING=\"$(FLOW_VERSION)\"
THREAD_FLAGS ?= -pthread

FREESTANDING_CFLAGS := -std=c17 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
                       -DFLOW_RING0_GATE=1 -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes \
                       -Wno-psabi -Werror -O2

.DEFAULT_GOAL := all
.PHONY: all capsules ring0 test fmt fmt-check install uninstall clean

all: capsules ring0 $(BUILD_DIR)/test-flow

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

fmt:
	$(CLANG_FORMAT) -i flow.h test_flow.c

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror flow.h test_flow.c

capsules: Flow.lean | $(BUILD_DIR)
	@echo "=== [Gate 1] Lean 4 Formal Proof Check (0 sorryAx, 0 lean -c) & 64B .flow Minting ==="
	$(LEAN) -DwarningAsError=true Flow.lean
	$(LEAN) --run Flow.lean $(CAPSULE_DIR)

$(BUILD_DIR)/flow_ring0.o: test_flow.c flow.h | $(BUILD_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c test_flow.c -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding Ring-0 object:" && nm -u $@ && exit 1)

ring0: $(BUILD_DIR)/flow_ring0.o
	@echo "=== [Gate 2] Freestanding Ring-0 Gate (-ffreestanding -nostdlib, nm -u == empty) ==="
	@echo "  [PASS] $(BUILD_DIR)/flow_ring0.o (0 undefined runtime symbols)"

$(BUILD_DIR)/test-flow: test_flow.c flow.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) test_flow.c -o $@

test: fmt-check capsules ring0 $(BUILD_DIR)/test-flow
	@echo "=== [Gate 3] Object-Free HoTT x 2-Jet Register Calculus Verification ==="
	@./$(BUILD_DIR)/test-flow $(CAPSULE_DIR)
	@echo "================================================================================"
	@echo "   ALL FLOW VERIFICATION GATES PASSED"
	@echo "================================================================================"

install:
	mkdir -p $(DESTDIR)$(PREFIX)/include/flow
	install -m 644 flow.h $(DESTDIR)$(PREFIX)/include/flow/flow.h
	install -m 644 Flow.lean $(DESTDIR)$(PREFIX)/include/flow/Flow.lean

uninstall:
	rm -rf $(DESTDIR)$(PREFIX)/include/flow

clean:
	rm -rf $(BUILD_DIR)
