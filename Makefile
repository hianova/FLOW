export PATH := $(HOME)/.elan/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin CXX),default)
CXX := clang++
endif
LEAN ?= lean
LEANCHECKER ?= leanchecker
CLANG_FORMAT ?= clang-format
PREFIX ?= /usr/local

CORE_DIR := core
SDK_DIR := sdk
CORE_LEAN_DIR := $(CORE_DIR)/lean
SDK_LEAN_DIR := $(SDK_DIR)/lean

BUILD_DIR := build
OLEAN_DIR := $(BUILD_DIR)/lean
FLOW_VERSION := $(shell tr -d '[:space:]' < VERSION)

CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2
override CFLAGS += -DFLOW_VERSION_STRING=\"$(FLOW_VERSION)\"

FREESTANDING_CFLAGS := -std=c17 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
                       -mgeneral-regs-only -DFLOW_EXPORT_KERNEL=1 \
                       -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2

SEARU_LIB_CFLAGS := -std=c17 -mgeneral-regs-only \
                    -fsanitize=signed-integer-overflow,shift,bounds,null,return,unreachable \
                    -fsanitize-trap=all \
                    -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2

C_SOURCES := $(CORE_DIR)/flow_opt_params.h $(CORE_DIR)/flow.h $(CORE_DIR)/searu.h \
             $(SDK_DIR)/flow_ir_wire.h $(SDK_DIR)/flow_ir.h $(SDK_DIR)/flow_ir_io.h \
             $(SDK_DIR)/searu_mc_gf2.h $(SDK_DIR)/flow_ir_synth.h $(SDK_DIR)/flow_cli.c \
             tests/ir_schema_gate.c

CORE_LEAN_NAMES := Core BitAlgebra Bundle Interval Math
SDK_LEAN_NAMES := Emit Schema Morphism Canonical Sdk

CORE_LEAN_SRCS := $(patsubst %,$(CORE_LEAN_DIR)/Flow/%.lean,$(CORE_LEAN_NAMES))
SDK_LEAN_SRCS := $(patsubst %,$(SDK_LEAN_DIR)/Flow/%.lean,$(SDK_LEAN_NAMES))
ALL_LEAN_SRCS := $(CORE_LEAN_SRCS) $(SDK_LEAN_SRCS) $(SDK_LEAN_DIR)/Flow.lean

CORE_OLEANS := $(patsubst %,$(OLEAN_DIR)/Flow/%.olean,$(CORE_LEAN_NAMES))
SDK_OLEANS := $(patsubst %,$(OLEAN_DIR)/Flow/%.olean,$(SDK_LEAN_NAMES))
FLOW_SUB_OLEANS := $(CORE_OLEANS) $(SDK_OLEANS)

.DEFAULT_GOAL := all
.PHONY: all ring0 self-opt test fmt fmt-check install uninstall clean

all: $(OLEAN_DIR)/Flow.olean $(BUILD_DIR)/libsearu.a $(BUILD_DIR)/libflow_sdk.a $(BUILD_DIR)/flow

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(OLEAN_DIR)/Flow:
	@mkdir -p $(OLEAN_DIR)/Flow

fmt:
	$(CLANG_FORMAT) -i $(C_SOURCES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)

# --- Layer 0 (`core/lean`): Pure Mathematics (zero IO, zero codegen) ---

$(OLEAN_DIR)/Flow/Core.olean: $(CORE_LEAN_DIR)/Flow/Core.lean | $(OLEAN_DIR)/Flow
	$(LEAN) -DwarningAsError=true --root=$(CORE_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow/BitAlgebra.olean: $(CORE_LEAN_DIR)/Flow/BitAlgebra.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(CORE_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow/Bundle.olean: $(CORE_LEAN_DIR)/Flow/Bundle.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(CORE_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow/Interval.olean: $(CORE_LEAN_DIR)/Flow/Interval.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(CORE_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow/Math.olean: $(CORE_LEAN_DIR)/Flow/Math.lean $(OLEAN_DIR)/Flow/Core.olean $(OLEAN_DIR)/Flow/BitAlgebra.olean $(OLEAN_DIR)/Flow/Bundle.olean $(OLEAN_DIR)/Flow/Interval.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(CORE_LEAN_DIR) -o $@ $<

# --- Layer 1 (`sdk/lean`): Engineering Emitters, 512b Schema ABI & Morphism IR ---

$(OLEAN_DIR)/Flow/Emit.olean: $(SDK_LEAN_DIR)/Flow/Emit.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(SDK_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow/Schema.olean: $(SDK_LEAN_DIR)/Flow/Schema.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(SDK_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow/Morphism.olean: $(SDK_LEAN_DIR)/Flow/Morphism.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(SDK_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow/Canonical.olean: $(SDK_LEAN_DIR)/Flow/Canonical.lean $(OLEAN_DIR)/Flow/Morphism.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(SDK_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow/Sdk.olean: $(SDK_LEAN_DIR)/Flow/Sdk.lean $(OLEAN_DIR)/Flow/Math.olean $(OLEAN_DIR)/Flow/Emit.olean $(OLEAN_DIR)/Flow/Schema.olean $(OLEAN_DIR)/Flow/Morphism.olean $(OLEAN_DIR)/Flow/Canonical.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(SDK_LEAN_DIR) -o $@ $<

$(OLEAN_DIR)/Flow.olean: $(SDK_LEAN_DIR)/Flow.lean $(OLEAN_DIR)/Flow/Sdk.olean $(FLOW_SUB_OLEANS) | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=$(SDK_LEAN_DIR) -o $@ $<

# --- Layer 0 C Ring-0 Objects (`-Icore` only; ZERO `-Isdk`) ---

$(BUILD_DIR)/flow_ring0.o: $(CORE_DIR)/flow.h $(CORE_DIR)/flow_opt_params.h | $(BUILD_DIR)
	printf '#include "flow.h"\n' | $(CC) $(FREESTANDING_CFLAGS) -x c -I$(CORE_DIR) -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding Ring-0 object:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](b\.(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)|cbn?z|tbn?z|j(e|ne|z|nz|g|ge|l|le|a|ae|b|be|s|ns|o|no))[[:space:]]' || \
		(echo "ERROR: Conditional branch instruction emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)

$(BUILD_DIR)/searu_ring0.o: $(CORE_DIR)/searu.h $(CORE_DIR)/flow.h $(CORE_DIR)/flow_opt_params.h | $(BUILD_DIR)
	printf '#include "searu.h"\n' | $(CC) $(FREESTANDING_CFLAGS) -UFLOW_EXPORT_KERNEL -DSEARU_EXPORT_RING0=1 -x c -I$(CORE_DIR) -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding SEARU Ring-0 object:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](b\.(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)|cbn?z|tbn?z|j(e|ne|z|nz|g|ge|l|le|a|ae|b|be|s|ns|o|no))[[:space:]]' || \
		(echo "ERROR: Conditional branch instruction emitted in SEARU Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in SEARU Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in SEARU Ring-0 kernel!" && objdump -d $@ && exit 1)

$(BUILD_DIR)/searu_solvers.o: $(CORE_DIR)/searu.h $(CORE_DIR)/flow.h $(CORE_DIR)/flow_opt_params.h | $(BUILD_DIR)
	printf '#include "searu.h"\n' | $(CC) $(SEARU_LIB_CFLAGS) -DSEARU_EXPORT_SOLVERS=1 -x c -I$(CORE_DIR) -c - -o $@

$(BUILD_DIR)/libsearu.a: $(BUILD_DIR)/searu_ring0.o $(BUILD_DIR)/searu_solvers.o
	rm -f $@
	ar rcs $@ $(BUILD_DIR)/searu_ring0.o $(BUILD_DIR)/searu_solvers.o
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof|__u?divti3|__u?modti3)(@|$$)' || \
		(echo "ERROR: Forbidden runtime/heap/float symbol in libsearu.a!" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in libsearu.a!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in libsearu.a!" && objdump -d $@ && exit 1)

# --- Layer 1 SDK Objects (Freestanding `flow_ir_wire.h` + `flow_ir.h` + `searu_mc_gf2.h`) ---

$(BUILD_DIR)/flow_ir_wire_ring0.o: $(SDK_DIR)/flow_ir_wire.h | $(BUILD_DIR)
	printf '#include "flow_ir_wire.h"\nsize_t flow_ir_wire_ring0_probe(uint8_t *buf, const uint32_t ins[3]);\nsize_t flow_ir_wire_ring0_probe(uint8_t *buf, const uint32_t ins[3]) { flow_ir_wire_write_header(buf, FLOW_IR_MULT_LINEAR, FLOW_IR_FLAG_CLEAN, 4U, 2U, 1U, 2U, 0U, "ring0_wire"); flow_ir_wire_write_val(buf + FLOW_IR_HEADER_BYTES, 8U, 0U, "v0"); flow_ir_wire_write_op(buf + FLOW_IR_HEADER_BYTES + FLOW_IR_VAL_WIRE_B, FLOW_IR_OP_XOR_VEC, 2U, 2U, ins, 0U, 0U, 0ULL, 0ULL, 0ULL, 0ULL); return flow_ir_wire_seal_trailer(buf, FLOW_IR_WIRE_BODY_BYTES(4U, 2U, 1U, 2U, 0U)); }\n' | \
		$(CC) $(FREESTANDING_CFLAGS) -UFLOW_EXPORT_KERNEL -fno-jump-tables -I$(SDK_DIR) -x c -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding flow_ir_wire_ring0.o:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in flow_ir_wire_ring0.o!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in flow_ir_wire_ring0.o!" && objdump -d $@ && exit 1)

$(BUILD_DIR)/flow_ir_ring0.o: $(SDK_DIR)/flow_ir.h $(SDK_DIR)/flow_ir_wire.h $(CORE_DIR)/flow.h $(CORE_DIR)/flow_opt_params.h | $(BUILD_DIR)
	printf '#include "flow_ir.h"\nbool flow_ir_ring0_probe(flow_ir_prog_t *p, const uint8_t *buf, size_t len, const uint64_t *inv, uint64_t *outv, flow_ir_dag_stats_t *st);\nbool flow_ir_ring0_probe(flow_ir_prog_t *p, const uint8_t *buf, size_t len, const uint64_t *inv, uint64_t *outv, flow_ir_dag_stats_t *st) { return flow_ir_load_bytes(p, buf, len) && flow_ir_validate(p) && flow_ir_admits_unitary(p) && flow_ir_analyze_dag(p, st) && flow_ir_eval_u64(p, inv, outv); }\n' | \
		$(CC) $(FREESTANDING_CFLAGS) -UFLOW_EXPORT_KERNEL -fno-jump-tables -I$(CORE_DIR) -I$(SDK_DIR) -x c -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding flow_ir_ring0.o:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in flow_ir_ring0.o!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in flow_ir_ring0.o!" && objdump -d $@ && exit 1)

$(BUILD_DIR)/searu_mc_gf2.o: $(SDK_DIR)/searu_mc_gf2.h $(CORE_DIR)/searu.h $(CORE_DIR)/flow.h $(CORE_DIR)/flow_opt_params.h | $(BUILD_DIR)
	printf '#include "searu_mc_gf2.h"\n' | $(CC) $(SEARU_LIB_CFLAGS) -DSEARU_EXPORT_MC_GF2=1 -x c -I$(CORE_DIR) -I$(SDK_DIR) -c - -o $@

$(BUILD_DIR)/libflow_sdk.a: $(BUILD_DIR)/searu_mc_gf2.o
	rm -f $@
	ar rcs $@ $(BUILD_DIR)/searu_mc_gf2.o
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof|__u?divti3|__u?modti3)(@|$$)' || \
		(echo "ERROR: Forbidden runtime/heap/float symbol in libflow_sdk.a!" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in libflow_sdk.a!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in libflow_sdk.a!" && objdump -d $@ && exit 1)

ring0: $(BUILD_DIR)/flow_ring0.o $(BUILD_DIR)/searu_ring0.o $(BUILD_DIR)/libsearu.a $(BUILD_DIR)/flow_ir_wire_ring0.o $(BUILD_DIR)/flow_ir_ring0.o $(BUILD_DIR)/libflow_sdk.a
	@rm -rf $(BUILD_DIR)/no_opt_probe && mkdir -p $(BUILD_DIR)/no_opt_probe && cp $(CORE_DIR)/flow.h $(BUILD_DIR)/no_opt_probe/
	@if (cd $(BUILD_DIR)/no_opt_probe && printf '#include "flow.h"\n' | $(CC) -std=c17 -I. -x c -fsyntax-only - 2>/dev/null); then rm -rf $(BUILD_DIR)/no_opt_probe; echo "ERROR: flow.h silently compiled without flow_opt_params.h!" >&2; exit 1; fi
	@rm -rf $(BUILD_DIR)/no_opt_probe
	@echo "=== [Gate 1] Freestanding Core & IR Pure-GPR Gate (0 undef, 0 branches, 0 udiv, 0 BLR, 0 FPU/SIMD) ==="

$(BUILD_DIR)/flow: $(SDK_DIR)/flow_cli.c $(CORE_DIR)/flow.h $(CORE_DIR)/searu.h $(CORE_DIR)/flow_opt_params.h $(SDK_DIR)/flow_ir_wire.h $(SDK_DIR)/flow_ir.h $(SDK_DIR)/flow_ir_io.h $(SDK_DIR)/searu_mc_gf2.h $(SDK_DIR)/flow_ir_synth.h $(BUILD_DIR)/libsearu.a $(BUILD_DIR)/libflow_sdk.a | $(BUILD_DIR)
	$(CC) $(CFLAGS) -mgeneral-regs-only -fsanitize=signed-integer-overflow,shift,bounds,null,return,unreachable -fsanitize-trap=all -DSEARU_EXPORT_RING0=1 -DSEARU_EXPORT_SOLVERS=1 -I$(CORE_DIR) -I$(SDK_DIR) $(SDK_DIR)/flow_cli.c $(BUILD_DIR)/libsearu.a $(BUILD_DIR)/libflow_sdk.a -o $@
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof)(@|$$)' || \
		(echo "ERROR: Heap or float symbol leaked into universal flow CLI!" && nm -u $@ && exit 1)

self-opt: $(BUILD_DIR)/flow
	./$(BUILD_DIR)/flow self-opt $(CORE_DIR)/flow_opt_params.h
	$(CLANG_FORMAT) -i $(CORE_DIR)/flow_opt_params.h
	$(MAKE) test

test: $(OLEAN_DIR)/Flow.olean ring0 $(BUILD_DIR)/flow
	@echo "=== [Gate 0] Strict Layer Isolation (core vs sdk) & Anti-Domain-Laundering Gate ==="
	@! grep -En 'IO\.|(:|→|->)[[:space:]]*IO\b|System\.FilePath|import Flow\.(Emit|Schema|Morphism|Canonical|Sdk)' $(CORE_LEAN_SRCS) || \
		(echo "ERROR: IO or SDK import leaked into core/lean/!" >&2 && exit 1)
	@! grep -En '<stdio\.h>|#ifdef[[:space:]]+SEARU_H|searu_mc|SEARU_MC' $(CORE_DIR)/*.h || \
		(echo "ERROR: stdio, #ifdef SEARU_H, or MC synthesizer leaked into core/*.h!" >&2 && exit 1)
	@! grep -En '<stdio\.h>|#ifdef[[:space:]]+SEARU_H' $(SDK_DIR)/flow_ir_wire.h $(SDK_DIR)/flow_ir.h || \
		(echo "ERROR: stdio or #ifdef SEARU_H leaked into sdk/flow_ir*.h!" >&2 && exit 1)
	@! grep -En '#ifdef[[:space:]]+SEARU_H' $(SDK_DIR)/flow_ir_synth.h || \
		(echo "ERROR: Order-dependent #ifdef SEARU_H guard leaked into sdk/flow_ir_synth.h!" >&2 && exit 1)
	@! grep -Eiq '\b(Silicon|Quantum|Spatial|Market|TAIFEX|Sky130|Euv7|Sic800v|Flexbox|BeerLambert|Bloom|Hohfeld|Hume|Bilattice|Penumbra|straddle|posLots|netPnl|roundtripFriction|bidQ16|askQ16|maxQuote|kurtosis|contrast_ducking|GuardBatch|admitGuard)\b' $(CORE_LEAN_SRCS) $(SDK_LEAN_SRCS) $(CORE_DIR)/*.h $(SDK_DIR)/*.h $(SDK_DIR)/*.c || \
		(echo "ERROR: Domain-specific or domain-laundered identifier leaked into FLOW core/sdk!" >&2 && exit 1)
	@echo "=== [Gate 2] Lean 4 .flow Atlas Emission & Universal CLI (@swap + KanLock + Schmitt) ==="
	@mkdir -p $(BUILD_DIR)/capsules
	@LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --run tests/atlas_gate.lean $(BUILD_DIR)/capsules
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
	@echo "=== [Gate 3] SEARU x FLOW 2-Jet Symbiotic NP Co-Solver & Ablation Gate ==="
	@./$(BUILD_DIR)/flow searu-verify
	@echo "=== [Gate 4] Symbiotic Duality Executable Witness (Flow.Bundle SSOT) ==="
	@LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --run docs/SymbioticDuality.lean
	@echo "=== [Gate 5] leanchecker + 0-sorryAx Audit, Flow.Math, C++20/Ring-0 Schema & FLOW_IR Gate ==="
	@for m in $(patsubst %,Flow.%,$(CORE_LEAN_NAMES) $(SDK_LEAN_NAMES)); do \
		LEAN_PATH=$(OLEAN_DIR) $(LEANCHECKER) $$m || exit 1; \
	done
	@awk 'index($$0,"sorry") && !index($$0,"sorryAx") {print "ERROR: sorry found in " FILENAME " at line " NR > "/dev/stderr"; bad=1} END {exit bad}' $(ALL_LEAN_SRCS)
	@printf 'import Flow.Math\n#check @Flow.Bundle.symbiotic_duality_complete\n' | \
		LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --stdin > /dev/null
	@mkdir -p $(BUILD_DIR)/ir_gate
	@python3 -c 'import glob, re; thms=[]; files=sorted(glob.glob("$(CORE_LEAN_DIR)/Flow/*.lean")+glob.glob("$(SDK_LEAN_DIR)/Flow/*.lean")); [ (ns:=[], [ (ns.append(m.group(1).split(".")) if (m:=re.match(r"^\s*namespace\s+(\S+)",l)) else (ns.pop() if (me:=re.match(r"^\s*end\s+(\S+)",l)) and ns and me.group(1)==".".join(ns[-1]) else (thms.append(".".join([x for xs in ns for x in xs]+[m2.group(1)])) if (m2:=re.match(r"^\s*theorem\s+(\S+)",l)) else None))) for l in open(p) ]) for p in files ]; assert len(thms) >= 50, f"Expected >=50 theorems, got {len(thms)}"; open("$(BUILD_DIR)/ir_gate/AxiomAuditFlow.lean","w").write("import Flow\n" + "".join(f"#print axioms {t}\n" for t in thms)); print(f"  [AUDIT] Collected {len(thms)} modular Flow core+sdk theorems for axiom verification")'
	@ax_f="$$(LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true $(BUILD_DIR)/ir_gate/AxiomAuditFlow.lean)" && \
		! printf '%s\n' "$$ax_f" | grep -q 'sorryAx' || (echo "ERROR: sorryAx detected in Flow modules!" >&2 && exit 1)
	@LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --run tests/ir_schema_gate.lean $(BUILD_DIR)/ir_gate
	@! grep -Eq '(<string\.h>|memcpy)' $(BUILD_DIR)/ir_gate/synth_arena_abi.h || \
		(echo "ERROR: <string.h> or memcpy leaked into freestanding synth_arena_abi.h!" >&2 && exit 1)
	@printf '#include "synth_arena_abi.h"\nvoid schema_ring0_probe(uint8_t out_pkt[64], const synth_pkt_t *in_pkt, synth_pkt_t *out_s, const synth_pkt_p_t *in_pp, synth_pkt_p_t *out_pp);\nvoid schema_ring0_probe(uint8_t out_pkt[64], const synth_pkt_t *in_pkt, synth_pkt_t *out_s, const synth_pkt_p_t *in_pp, synth_pkt_p_t *out_pp) { uint8_t tmp[64]; synth_arena_pkt_pack_bytes(out_pkt, in_pkt); synth_arena_pkt_unpack_bytes(out_pkt, out_s); synth_arena_pkt_p_pack_bytes(tmp, in_pp); synth_arena_pkt_p_unpack_bytes(tmp, out_pp); }\n' | \
		$(CC) $(FREESTANDING_CFLAGS) -UFLOW_EXPORT_KERNEL -fno-jump-tables -I$(CORE_DIR) -I$(BUILD_DIR)/ir_gate -x c -c - -o $(BUILD_DIR)/ir_gate/synth_schema_ring0.o
	@test -z "$$(nm -u $(BUILD_DIR)/ir_gate/synth_schema_ring0.o)" || \
		(echo "ERROR: Undefined symbols in freestanding synth_schema_ring0.o:" && nm -u $(BUILD_DIR)/ir_gate/synth_schema_ring0.o && exit 1)
	@! objdump -d $(BUILD_DIR)/ir_gate/synth_schema_ring0.o | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in synth_schema_ring0.o!" && objdump -d $(BUILD_DIR)/ir_gate/synth_schema_ring0.o && exit 1)
	@! objdump -d $(BUILD_DIR)/ir_gate/synth_schema_ring0.o | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in synth_schema_ring0.o!" && objdump -d $(BUILD_DIR)/ir_gate/synth_schema_ring0.o && exit 1)
	@! grep -Eq '\b(u64|i64)\b' $(BUILD_DIR)/ir_gate/synth_arena.wgsl || \
		(echo "ERROR: Forbidden 64-bit WGSL type emitted by Flow.Schema!" >&2 && exit 1)
	@if command -v naga >/dev/null 2>&1; then \
		naga $(BUILD_DIR)/ir_gate/synth_arena.wgsl $(BUILD_DIR)/ir_gate/synth_arena.spv && rm -f $(BUILD_DIR)/ir_gate/synth_arena.spv; \
	fi
	@./$(BUILD_DIR)/flow verify $(BUILD_DIR)/ir_gate/synth_bundle.flow > /dev/null
	@./$(BUILD_DIR)/flow fir-synth $(BUILD_DIR)/ir_gate/synth_mul4.fir $(BUILD_DIR)/ir_gate/synth_mul4_opt.fir
	@./$(BUILD_DIR)/flow fir-synth $(BUILD_DIR)/ir_gate/synth_inv4.fir $(BUILD_DIR)/ir_gate/synth_inv4_a5.fir
	@./$(BUILD_DIR)/flow fir-synth $(BUILD_DIR)/ir_gate/synth_inv4.fir $(BUILD_DIR)/ir_gate/synth_inv4_d2.fir --max-depth 2
	@./$(BUILD_DIR)/flow fir-synth $(BUILD_DIR)/ir_gate/synth_lut5.fir $(BUILD_DIR)/ir_gate/synth_lut5_opt.fir
	@./$(BUILD_DIR)/flow fir-synth $(BUILD_DIR)/ir_gate/synth_quad_inv.fir $(BUILD_DIR)/ir_gate/synth_quad_a32.fir --name synth_quad_a32
	@./$(BUILD_DIR)/flow fir-synth $(BUILD_DIR)/ir_gate/synth_quad_inv.fir $(BUILD_DIR)/ir_gate/synth_quad_d4.fir --name synth_quad_d4 --max-depth 2
	@printf '#include "flow.h"\n#include "searu.h"\nint core_cpp20_probe(void) { return (int)sizeof(flow_cell) + (int)sizeof(searu_result); }\n' | \
		$(CXX) -x c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -iquote $(CORE_DIR) -c - -o $(BUILD_DIR)/ir_gate/cpp_core_probe.o
	@printf '#include "flow_ir_wire.h"\nint wire_cpp20_probe(void) { return (int)FLOW_IR_WIRE_BYTES(23U, 5U, 4U, 18U, 0U); }\n' | \
		$(CXX) -x c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -iquote $(SDK_DIR) -c - -o $(BUILD_DIR)/ir_gate/cpp_wire_probe.o
	@printf '#include "flow_ir_synth.h"\n#include "flow_ir_io.h"\n#include "synth_arena_abi.h"\n#include "synth_bundle_abi.h"\nint sdk_cpp20_probe(void) { return (int)sizeof(synth_pkt_t) + (int)sizeof(synth_arena_t) + (int)sizeof(synth_bundle_abi_t) + (int)sizeof(flow_ir_prog_t) + (int)sizeof(flow_ir_synth_ws_t) + (int)sizeof(searu_mc_plan); }\n' | \
		$(CXX) -x c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -iquote $(CORE_DIR) -iquote $(SDK_DIR) -I$(BUILD_DIR)/ir_gate -c - -o $(BUILD_DIR)/ir_gate/cpp_sdk_probe.o
	@$(CC) $(SEARU_LIB_CFLAGS) -fno-jump-tables -mstack-probe-size=65536 -DIR_GATE_DIR=\"$(BUILD_DIR)/ir_gate\" -I$(CORE_DIR) -I$(SDK_DIR) -I$(BUILD_DIR)/ir_gate -c tests/ir_schema_gate.c -o $(BUILD_DIR)/ir_gate/ir_test.o
	@! objdump -d $(BUILD_DIR)/ir_gate/ir_test.o | grep -Eq '[[:space:]](blr|br|udiv|sdiv)[[:space:]]' || (echo "ERROR: Forbidden br/blr/div in flow_ir!" >&2 && exit 1)
	@$(CC) $(SEARU_LIB_CFLAGS) $(BUILD_DIR)/ir_gate/ir_test.o -o $(BUILD_DIR)/ir_gate/ir_test
	@$(BUILD_DIR)/ir_gate/ir_test
	@echo "================================================================================"
	@echo "   ALL FLOW v$(FLOW_VERSION) (CORE + SDK) VERIFICATION GATES PASSED"
	@echo "================================================================================"

install: $(OLEAN_DIR)/Flow.olean $(BUILD_DIR)/libsearu.a $(BUILD_DIR)/libflow_sdk.a $(BUILD_DIR)/flow
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/include/flow/core $(DESTDIR)$(PREFIX)/include/flow/sdk $(DESTDIR)$(PREFIX)/lib/flow/Flow
	install -m 755 $(BUILD_DIR)/flow $(DESTDIR)$(PREFIX)/bin/flow
	install -m 644 $(CORE_DIR)/flow_opt_params.h $(DESTDIR)$(PREFIX)/include/flow/core/flow_opt_params.h
	install -m 644 $(CORE_DIR)/flow.h $(DESTDIR)$(PREFIX)/include/flow/core/flow.h
	install -m 644 $(CORE_DIR)/searu.h $(DESTDIR)$(PREFIX)/include/flow/core/searu.h
	install -m 644 $(SDK_DIR)/flow_ir_wire.h $(DESTDIR)$(PREFIX)/include/flow/sdk/flow_ir_wire.h
	install -m 644 $(SDK_DIR)/flow_ir.h $(DESTDIR)$(PREFIX)/include/flow/sdk/flow_ir.h
	install -m 644 $(SDK_DIR)/flow_ir_io.h $(DESTDIR)$(PREFIX)/include/flow/sdk/flow_ir_io.h
	install -m 644 $(SDK_DIR)/searu_mc_gf2.h $(DESTDIR)$(PREFIX)/include/flow/sdk/searu_mc_gf2.h
	install -m 644 $(SDK_DIR)/flow_ir_synth.h $(DESTDIR)$(PREFIX)/include/flow/sdk/flow_ir_synth.h
	install -m 644 $(BUILD_DIR)/libsearu.a $(DESTDIR)$(PREFIX)/lib/flow/libsearu.a
	install -m 644 $(BUILD_DIR)/libflow_sdk.a $(DESTDIR)$(PREFIX)/lib/flow/libflow_sdk.a
	install -m 644 $(SDK_LEAN_DIR)/Flow.lean $(DESTDIR)$(PREFIX)/lib/flow/Flow.lean
	install -m 644 $(OLEAN_DIR)/Flow.olean $(DESTDIR)$(PREFIX)/lib/flow/Flow.olean
	install -m 644 $(CORE_LEAN_SRCS) $(SDK_LEAN_SRCS) $(DESTDIR)$(PREFIX)/lib/flow/Flow/
	install -m 644 $(OLEAN_DIR)/Flow/*.olean $(DESTDIR)$(PREFIX)/lib/flow/Flow/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/flow
	rm -rf $(DESTDIR)$(PREFIX)/include/flow $(DESTDIR)$(PREFIX)/lib/flow

clean:
	rm -rf $(BUILD_DIR)
