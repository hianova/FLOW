export PATH := $(HOME)/.elan/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
LEAN ?= lean
CLANG_FORMAT ?= clang-format
PREFIX ?= /usr/local

BUILD_DIR := build
FLOW_VERSION := $(shell tr -d '[:space:]' < VERSION)

CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2
override CFLAGS += -DFLOW_VERSION_STRING=\"$(FLOW_VERSION)\"

FREESTANDING_CFLAGS := -std=c17 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
                       -mgeneral-regs-only -DFLOW_EXPORT_KERNEL=1 \
                       -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2

C_SOURCES := flow.h flow.c

.DEFAULT_GOAL := all
.PHONY: all ring0 test fmt fmt-check install uninstall clean

all: $(BUILD_DIR)/Flow.olean $(BUILD_DIR)/flow

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

fmt:
	$(CLANG_FORMAT) -i $(C_SOURCES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)

$(BUILD_DIR)/Flow.olean: Flow.lean | $(BUILD_DIR)
	$(LEAN) -DwarningAsError=true -o $@ Flow.lean

$(BUILD_DIR)/flow_ring0.o: flow.h | $(BUILD_DIR)
	printf '#include "flow.h"\n' | $(CC) $(FREESTANDING_CFLAGS) -x c -I. -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding Ring-0 object:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](b\.(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)|cbn?z|tbn?z|j(e|ne|z|nz|g|ge|l|le|a|ae|b|be|s|ns|o|no))[[:space:]]' || \
		(echo "ERROR: Conditional branch instruction emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)

ring0: $(BUILD_DIR)/flow_ring0.o
	@echo "=== [Gate 1] Freestanding Pure-GPR Ring-0 Gate (0 undef, 0 branches, 0 FPU/SIMD) ==="

$(BUILD_DIR)/flow: flow.c flow.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) flow.c -o $@
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof)(@|$$)' || \
		(echo "ERROR: Heap or float symbol leaked into universal flow engine!" && nm -u $@ && exit 1)

test: $(BUILD_DIR)/Flow.olean ring0 $(BUILD_DIR)/flow
	@echo "=== [Gate 2] Lean 4 .flow Atlas Emission & Universal CLI (@swap + KanLock + Schmitt) ==="
	@mkdir -p $(BUILD_DIR)/capsules
	@printf 'import Flow\ndef c0 : Flow.Rule := { b0 := 0, b1 := Flow.q16One, b2 := 0, bExit := -100 * Flow.q16One, bEnter := 10 * Flow.q16One, dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0 }\ndef c1 : Flow.Rule := { b0 := 0, b1 := 0, b2 := 2 * Flow.q16One, bExit := 2 * Flow.q16One, bEnter := 50 * Flow.q16One, dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0x0001 }\ndef cHarbor : Flow.Rule := { b0 := 0, b1 := 2 * Flow.q16One, b2 := 0, bExit := -50 * Flow.q16One, bEnter := 50 * Flow.q16One, dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0x0003 }\ndef cForeign : Flow.Rule := { b0 := 0, b1 := Flow.q16One, b2 := 0, bExit := -10 * Flow.q16One, bEnter := 10 * Flow.q16One, dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 7, kanLock := 0 }\n#eval do\n  let s0 := Flow.mintState 1 0 (initChart := 0) (initMode := 0x0001)\n  Flow.emitBundle "%s/surge.flow" 1 s0 [c0, c1] c0 2\n  Flow.emitCell "%s/harbor.cell" (Flow.mintCell cHarbor 5)\n  Flow.emitCell "%s/foreign.cell" (Flow.mintCell cForeign 5)\n' "$(BUILD_DIR)/capsules" "$(BUILD_DIR)/capsules" "$(BUILD_DIR)/capsules" | \
		LEAN_PATH=$(BUILD_DIR) $(LEAN) -DwarningAsError=true --stdin
	@./$(BUILD_DIR)/flow --version | grep -q 'FLOW $(FLOW_VERSION)'
	@./$(BUILD_DIR)/flow verify $(BUILD_DIR)/capsules/surge.flow > /dev/null
	@long_comment="$$(printf '#%01100d9.0' 0)" && \
		out="$$(printf '%s\n4.0\n12.0\n9.0 0x0000\n1.0\n@swap 1 2 %s/harbor.cell\n@swap 1 3 %s/foreign.cell\n@swap 1 5 %s/harbor.cell\nq16:786432 0x0003\n-0.0000\n' "$$long_comment" "$(BUILD_DIR)/capsules" "$(BUILD_DIR)/capsules" "$(BUILD_DIR)/capsules" | \
		./$(BUILD_DIR)/flow run $(BUILD_DIR)/capsules/surge.flow)" && \
		printf '%s\n' "$$out" | grep -q 'step=1 chart=0 x=4.0000 dt=4.0000 r0=0.0000 mode=0x0001 epoch=1 status=0x01' && \
		printf '%s\n' "$$out" | grep -q 'step=2 chart=1 x=12.0000 dt=8.0000 r0=0.0000 mode=0x0001 epoch=2 status=0x01' && \
		printf '%s\n' "$$out" | grep -q 'step=3 chart=1 x=3.0000 dt=9.0000 r0=0.0000 mode=0x0001 epoch=3 status=0x01' && \
		printf '%s\n' "$$out" | grep -q 'step=4 chart=0 x=1.0000 dt=2.0000 r0=0.0000 mode=0x0001 epoch=4 status=0x01' && \
		printf '%s\n' "$$out" | grep -q '# swap slot=1 epoch=2 rejected' && \
		printf '%s\n' "$$out" | grep -q '# swap slot=1 epoch=3 rejected' && \
		printf '%s\n' "$$out" | grep -q '# swap slot=1 epoch=5 ok' && \
		printf '%s\n' "$$out" | grep -q 'step=5 chart=1 x=11.0000 dt=10.0000 r0=-1.0000 mode=0x0003 epoch=5 status=0x00' && \
		printf '%s\n' "$$out" | grep -q 'step=6 chart=1 x=1.0000 dt=10.0000 r0=2.0000 mode=0x0003 epoch=6 status=0x00'
	@echo "================================================================================"
	@echo "   ALL FLOW v$(FLOW_VERSION) VERIFICATION GATES PASSED"
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
