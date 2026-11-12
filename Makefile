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

C_SOURCES := flow.h flow.c test_flow.c

.DEFAULT_GOAL := all
.PHONY: all capsules ring0 test fmt fmt-check install uninstall clean

all: capsules ring0 $(BUILD_DIR)/flow $(BUILD_DIR)/test-flow

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

fmt:
	$(CLANG_FORMAT) -i $(C_SOURCES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)

$(BUILD_DIR)/Flow.olean: Flow.lean | $(BUILD_DIR)
	$(LEAN) -DwarningAsError=true -o $@ Flow.lean

capsules: $(BUILD_DIR)/Flow.olean
	@echo "=== [Gate 1] Lean 4 Formal Proof Check (0 sorryAx, 0 lean -c) & .flow Atlas Minting ==="
	@printf 'import Flow\n#eval Flow.emitCoreTestFixtures "%s"\n' "$(CAPSULE_DIR)" | \
		LEAN_PATH=$(BUILD_DIR) $(LEAN) -DwarningAsError=true --stdin

$(BUILD_DIR)/flow_ring0.o: test_flow.c flow.h | $(BUILD_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c test_flow.c -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding Ring-0 object:" && nm -u $@ && exit 1)

ring0: $(BUILD_DIR)/flow_ring0.o
	@echo "=== [Gate 2] Freestanding Ring-0 Gate (-ffreestanding -nostdlib, nm -u == empty) ==="
	@echo "  [PASS] $(BUILD_DIR)/flow_ring0.o (0 undefined runtime symbols)"

$(BUILD_DIR)/flow: flow.c flow.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) flow.c -o $@
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof)(@|$$)' || \
		(echo "ERROR: Heap or float symbol leaked into universal flow engine!" && nm -u $@ && exit 1)

$(BUILD_DIR)/test-flow: test_flow.c flow.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(THREAD_FLAGS) test_flow.c -o $@

test: capsules ring0 $(BUILD_DIR)/flow $(BUILD_DIR)/test-flow
	@echo "=== [Gate 3] Object-Free HoTT x 2-Jet Manifold Atlas Verification ==="
	@./$(BUILD_DIR)/test-flow $(CAPSULE_DIR)
	@echo "=== [Gate 4] Universal Zero-C Engine CLI (flow verify & flow run) ==="
	@./$(BUILD_DIR)/flow verify $(CAPSULE_DIR)/core_atlas4.flow > /dev/null
	@out="$$(printf '3.0\n12.0\n60.0 0xFF00\n@swap 2 1 %s/swap_harbor.flow\n@swap 2 3 %s/foreign_chart.flow\n@swap 2 5 %s/swap_harbor.flow\n200.0 0xFF03\n' "$(CAPSULE_DIR)" "$(CAPSULE_DIR)" "$(CAPSULE_DIR)" | \
		./$(BUILD_DIR)/flow run $(CAPSULE_DIR)/core_atlas4.flow)" && \
		printf '%s\n' "$$out" | grep -q 'step=1 chart=1 x=3.0000' && \
		printf '%s\n' "$$out" | grep -q 'step=2 chart=2 x=12.0000' && \
		printf '%s\n' "$$out" | grep -q 'step=3 chart=2 x=8.0000 .* mode=0xff03' && \
		printf '%s\n' "$$out" | grep -q '# swap slot=2 epoch=1 rejected' && \
		printf '%s\n' "$$out" | grep -q '# swap slot=2 epoch=3 rejected' && \
		printf '%s\n' "$$out" | grep -q '# swap slot=2 epoch=5 ok' && \
		printf '%s\n' "$$out" | grep -q 'step=4 chart=1 x=1.0000'
	@echo "  [PASS] Universal flow CLI verified (.flow bundle + stream + @swap hot-reload, 0 heap, 0 float)"
	@echo "================================================================================"
	@echo "   ALL FLOW VERIFICATION GATES PASSED"
	@echo "================================================================================"

install: $(BUILD_DIR)/Flow.olean $(BUILD_DIR)/flow
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/include/flow $(DESTDIR)$(PREFIX)/lib/flow
	install -m 755 $(BUILD_DIR)/flow $(DESTDIR)$(PREFIX)/bin/flow
	install -m 644 flow.h $(DESTDIR)$(PREFIX)/include/flow/flow.h
	install -m 644 Flow.lean $(DESTDIR)$(PREFIX)/lib/flow/Flow.lean
	install -m 644 $(BUILD_DIR)/Flow.olean $(DESTDIR)$(PREFIX)/lib/flow/Flow.olean

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/flow
	rm -rf $(DESTDIR)$(PREFIX)/include/flow $(DESTDIR)$(PREFIX)/lib/flow

clean:
	rm -rf $(BUILD_DIR)
