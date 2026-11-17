export PATH := $(HOME)/.elan/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
CXX ?= clang++
LEAN ?= lean
CLANG_FORMAT ?= clang-format
PREFIX ?= /usr/local

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

C_SOURCES := flow_opt_params.h flow.h searu.h flow_ir.h flow.c

FLOW_SUB_NAMES := Core BitAlgebra Bundle Interval Convex Schema Morphism
FLOW_SUB_LEANS := $(patsubst %,Flow/%.lean,$(FLOW_SUB_NAMES))
FLOW_SUB_OLEANS := $(patsubst %,$(OLEAN_DIR)/Flow/%.olean,$(FLOW_SUB_NAMES))

.DEFAULT_GOAL := all
.PHONY: all ring0 self-opt test fmt fmt-check install uninstall clean

all: $(OLEAN_DIR)/Flow.olean $(BUILD_DIR)/libsearu.a $(BUILD_DIR)/flow

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(OLEAN_DIR)/Flow:
	@mkdir -p $(OLEAN_DIR)/Flow

fmt:
	$(CLANG_FORMAT) -i $(C_SOURCES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)

$(OLEAN_DIR)/Flow/Core.olean: Flow/Core.lean | $(OLEAN_DIR)/Flow
	$(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(OLEAN_DIR)/Flow/BitAlgebra.olean: Flow/BitAlgebra.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(OLEAN_DIR)/Flow/Bundle.olean: Flow/Bundle.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(OLEAN_DIR)/Flow/Interval.olean: Flow/Interval.lean $(OLEAN_DIR)/Flow/Bundle.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(OLEAN_DIR)/Flow/Convex.olean: Flow/Convex.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(OLEAN_DIR)/Flow/Schema.olean: Flow/Schema.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(OLEAN_DIR)/Flow/Morphism.olean: Flow/Morphism.lean $(OLEAN_DIR)/Flow/Core.olean | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(OLEAN_DIR)/Flow.olean: Flow.lean $(FLOW_SUB_OLEANS) | $(OLEAN_DIR)/Flow
	LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --root=. -o $@ Flow.lean

$(BUILD_DIR)/flow_ring0.o: flow.h flow_opt_params.h | $(BUILD_DIR)
	printf '#include "flow.h"\n' | $(CC) $(FREESTANDING_CFLAGS) -x c -I. -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding Ring-0 object:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](b\.(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)|cbn?z|tbn?z|j(e|ne|z|nz|g|ge|l|le|a|ae|b|be|s|ns|o|no))[[:space:]]' || \
		(echo "ERROR: Conditional branch instruction emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)

$(BUILD_DIR)/searu_ring0.o: searu.h flow.h flow_opt_params.h | $(BUILD_DIR)
	printf '#include "searu.h"\n' | $(CC) $(FREESTANDING_CFLAGS) -UFLOW_EXPORT_KERNEL -DSEARU_EXPORT_RING0=1 -x c -I. -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in freestanding SEARU Ring-0 object:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](b\.(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)|cbn?z|tbn?z|j(e|ne|z|nz|g|ge|l|le|a|ae|b|be|s|ns|o|no))[[:space:]]' || \
		(echo "ERROR: Conditional branch instruction emitted in SEARU Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in SEARU Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in SEARU Ring-0 kernel!" && objdump -d $@ && exit 1)

$(BUILD_DIR)/searu_solvers.o: searu.h flow.h flow_opt_params.h | $(BUILD_DIR)
	printf '#include "searu.h"\n' | $(CC) $(SEARU_LIB_CFLAGS) -DSEARU_EXPORT_SOLVERS=1 -x c -I. -c - -o $@

$(BUILD_DIR)/libsearu.a: $(BUILD_DIR)/searu_ring0.o $(BUILD_DIR)/searu_solvers.o
	rm -f $@
	ar rcs $@ $(BUILD_DIR)/searu_ring0.o $(BUILD_DIR)/searu_solvers.o
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof|__u?divti3|__u?modti3)(@|$$)' || \
		(echo "ERROR: Forbidden runtime/heap/float symbol in libsearu.a!" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in libsearu.a!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in libsearu.a!" && objdump -d $@ && exit 1)

ring0: $(BUILD_DIR)/flow_ring0.o $(BUILD_DIR)/searu_ring0.o $(BUILD_DIR)/libsearu.a
	@rm -rf $(BUILD_DIR)/no_opt_probe && mkdir -p $(BUILD_DIR)/no_opt_probe && cp flow.h $(BUILD_DIR)/no_opt_probe/
	@if (cd $(BUILD_DIR)/no_opt_probe && printf '#include "flow.h"\n' | $(CC) -std=c17 -I. -x c -fsyntax-only - 2>/dev/null); then rm -rf $(BUILD_DIR)/no_opt_probe; echo "ERROR: flow.h silently compiled without flow_opt_params.h!" >&2; exit 1; fi
	@rm -rf $(BUILD_DIR)/no_opt_probe
	@echo "=== [Gate 1] Freestanding Pure-GPR Ring-0 & Binary ISA Gate (0 undef, 0 branches, 0 udiv, 0 BLR, 0 FPU/SIMD) ==="

$(BUILD_DIR)/flow: flow.c flow.h searu.h flow_opt_params.h $(BUILD_DIR)/libsearu.a | $(BUILD_DIR)
	$(CC) $(CFLAGS) -mgeneral-regs-only -fsanitize=signed-integer-overflow,shift,bounds,null,return,unreachable -fsanitize-trap=all -DSEARU_EXPORT_RING0=1 -DSEARU_EXPORT_SOLVERS=1 flow.c $(BUILD_DIR)/libsearu.a -o $@
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof)(@|$$)' || \
		(echo "ERROR: Heap or float symbol leaked into universal flow engine!" && nm -u $@ && exit 1)

self-opt: $(BUILD_DIR)/flow
	./$(BUILD_DIR)/flow self-opt flow_opt_params.h
	$(CLANG_FORMAT) -i flow_opt_params.h
	$(MAKE) test

test: $(OLEAN_DIR)/Flow.olean ring0 $(BUILD_DIR)/flow
	@echo "=== [Gate 2] Lean 4 .flow Atlas Emission & Universal CLI (@swap + KanLock + Schmitt) ==="
	@mkdir -p $(BUILD_DIR)/capsules
	@printf 'import Flow\ndef c0 : Flow.Rule := { b0 := 0, b1 := Flow.q16One, b2 := 0, bExit := -100 * Flow.q16One, bEnter := 10 * Flow.q16One, dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0 }\ndef c1 : Flow.Rule := { b0 := 0, b1 := 0, b2 := 2 * Flow.q16One, bExit := 2 * Flow.q16One, bEnter := 50 * Flow.q16One, dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0x0001 }\ndef cHarbor : Flow.Rule := { b0 := 0, b1 := 2 * Flow.q16One, b2 := 0, bExit := -50 * Flow.q16One, bEnter := 50 * Flow.q16One, dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 1, kanLock := 0x0003 }\ndef cForeign : Flow.Rule := { b0 := 0, b1 := Flow.q16One, b2 := 0, bExit := -10 * Flow.q16One, bEnter := 10 * Flow.q16One, dtMax := 10 * Flow.q16One, uGain := Flow.q16One, nextLo := 0, nextHi := 7, kanLock := 0 }\n#eval do\n  let s0 := Flow.mintState 1 0 (initChart := 0) (initMode := 0x0001)\n  Flow.emitBundle "%s/surge.flow" 1 s0 [c0, c1] c0 2\n  Flow.emitCell "%s/harbor.cell" (Flow.mintCell cHarbor 5)\n  Flow.emitCell "%s/foreign.cell" (Flow.mintCell cForeign 5)\n' "$(BUILD_DIR)/capsules" "$(BUILD_DIR)/capsules" "$(BUILD_DIR)/capsules" | \
		LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --stdin
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
	@echo "=== [Gate 4] Lean 4 Formal Proof of 2-Jet x SEARU Symbiotic Duality ==="
	@$(LEAN) -DwarningAsError=true --run docs/SymbioticDuality.lean
	@echo "=== [Gate 5] Pure Flow/* 0-sorryAx & Domain-Purity Audit, 512b Schema ABI/WGSL & FLOW_IR (.fir) Gate ==="
	@awk 'index($$0,"sorry") && !index($$0,"sorryAx") {print "ERROR: sorry found in " FILENAME " at line " NR > "/dev/stderr"; bad=1} END {exit bad}' Flow.lean Flow/*.lean
	@! grep -Eiq '\b(Silicon|Quantum|Spatial|Market|TAIFEX|Sky130|Euv7|Sic800v|Flexbox|BeerLambert|Bloom)\b' Flow.lean Flow/*.lean flow.h searu.h flow_ir.h flow.c || \
		(echo "ERROR: Application/domain identifier leaked into FLOW pure core!" >&2 && exit 1)
	@mkdir -p $(BUILD_DIR)/ir_gate
	@python3 -c 'import glob, re; thms=[]; [ (ns:=[], [ (ns.append(m.group(1).split(".")) if (m:=re.match(r"^\s*namespace\s+(\S+)",l)) else (ns.pop() if (me:=re.match(r"^\s*end\s+(\S+)",l)) and ns and me.group(1)==".".join(ns[-1]) else (thms.append(".".join([x for xs in ns for x in xs]+[m2.group(1)])) if (m2:=re.match(r"^\s*theorem\s+(\S+)",l)) else None))) for l in open(p) ]) for p in sorted(glob.glob("Flow/*.lean")) ]; assert len(thms) >= 90, f"Expected >=90 theorems, got {len(thms)}"; open("$(BUILD_DIR)/ir_gate/AxiomAuditFlow.lean","w").write("import Flow\n" + "".join(f"#print axioms {t}\n" for t in thms)); print(f"  [AUDIT] Collected {len(thms)} modular Flow theorems for axiom verification")'
	@ax_f="$$(LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true $(BUILD_DIR)/ir_gate/AxiomAuditFlow.lean)" && \
		! printf '%s\n' "$$ax_f" | grep -q 'sorryAx' || (echo "ERROR: sorryAx detected in Flow modules!" >&2 && exit 1)
	@printf 'import Flow\n\ndef synthPktPacked : Flow.Schema.CellLayout64 :=\n  Flow.Schema.CellLayout64.packed64 "pkt_p" "synth_pkt_p_t" "pkt_p" [\n    ("tag_ch", .cchar, 4),\n    ("vals_i16", .i16, 4),\n    ("v_u32", .u32, 4),\n    ("pad_u32", .u32, 1),\n    ("acc_u64", .u64, 4)\n  ]\n\ndef synthArena : Flow.Schema.ArenaSpec where\n  cPrefix         := "synth_arena"\n  headerGuard     := "SYNTH_ARENA_H"\n  totalCells      := 16\n  cCellUnionName  := "synth_cell64_t"\n  cArenaMapName   := "synth_arena_map_t"\n  cArenaUnionName := "synth_arena_t"\n  regions := [\n    { name := "hdr",  baseSlot := 0, count := 1,  cSlotMacro := "SYNTH_SLOT_HDR",  mapDecl := "synth_pkt_p_t hdr;" },\n    { name := "ctrl", baseSlot := 1, count := 3,  cSlotMacro := "SYNTH_SLOT_CTRL", cCountMacro := "SYNTH_MAX_CTRL", mapDecl := "synth_pkt_p_t ctrl[SYNTH_MAX_CTRL];" },\n    { name := "data", baseSlot := 4, count := 12, cSlotMacro := "SYNTH_SLOT_DATA", mapDecl := "uint64_t data[12][8];" }\n  ]\n  layouts := [\n    synthPktPacked,\n    { name := "pkt", cTypeName := "synth_pkt_t", unionMember := "pkt", aligned64 := true, fields := [\n      { name := "tag_u8",  bitOffset := 0,   ty := .u8 },\n      { name := "pad_u8",  bitOffset := 8,   ty := .u8 },\n      { name := "flag_u16", bitOffset := 16, ty := .u16 },\n      { name := "len_u32", bitOffset := 32,  ty := .u32 },\n      { name := "val_i32", bitOffset := 64,  ty := .i32 },\n      { name := "pad_u32", bitOffset := 96,  ty := .u32 },\n      { name := "seq_u64", bitOffset := 128, ty := .u64, count := 4 },\n      { name := "acc_i64", bitOffset := 384, ty := .i64, count := 2 }\n    ] }\n  ]\n\ndef synthBundleSpec : Flow.Schema.Spec where\n  cPrefix := "synth_bundle_abi"\n  headerGuard := "SYNTH_BUNDLE_ABI_H"\n  kExp := 2\n  tagChart := 1\n  fields := [\n    { name := "alpha", chartSlot := 1, lane := 0, minVal := 1, maxVal := 10000 },\n    { name := "beta",  chartSlot := 2, lane := 1, minVal := -5000, maxVal := 5000 },\n    { name := "gamma", chartSlot := 3, lane := 2, minVal := 0, maxVal := 65535 }\n  ]\n\ndef synthLinGraph : Flow.Morphism.Graph :=\n  let b0 := Flow.Morphism.Builder.init "synth_lin" .linear true\n  let (b1, a) := b0.input "a" 8\n  let (b2, x) := b1.input "x" 8\n  let (b3, sel) := b2.input "sel" 1\n  let (b4, invA) := b3.gf2mInv "inv_a" 8 a 0x11B\n  let (b5, sq) := b4.sqrFxp "sq" 8 invA 4 4 4\n  let (b6, sum) := b5.add "sum" 8 sq x\n  let (b7, ez) := b6.eqZero "ez" a\n  let (b8, y0) := b7.ctrlSel "y0" 8 sel invA sum\n  let (b9, y) := b8.ctrlSel "y" 8 ez y0 x\n  b9.build [y]\n\ndef synthCartGraph : Flow.Morphism.Graph :=\n  let b0 := Flow.Morphism.Builder.init "synth_cart" .cartesian false\n  let (b1, s0) := b0.input "state_in" 512\n  let (b2, p0) := b1.stagePipe "pipe0" 64 [s0] 1 2 3 0\n  b2.build [p0]\n\n#eval do\n  let ok1 ← synthArena.emitC17HeaderChecked "%s/synth_arena_abi.h"\n  let ok2 ← synthArena.emitWgslModuleChecked "%s/synth_arena.wgsl"\n  let ok3 ← synthBundleSpec.emitAbiHeaderChecked "%s/synth_bundle_abi.h"\n  let fb : Flow.Rule := { b1 := Flow.q16One, dtMax := 10 * Flow.q16One, bExit := -100 * Flow.q16One, bEnter := 100 * Flow.q16One, uGain := Flow.q16One }\n  let st0 := Flow.mintState 2 0\n  match synthBundleSpec.packBundle st0 [fb] fb [("alpha", 42), ("beta", -123), ("gamma", 777)] 1 with\n  | none => IO.Process.exit 1\n  | some bytes => IO.FS.writeBinFile "%s/synth_bundle.flow" bytes\n  let ok4 ← Flow.Morphism.emitFirChecked "%s/synth_lin.fir" synthLinGraph\n  let ok5 ← Flow.Morphism.emitFirChecked "%s/synth_cart.fir" synthCartGraph\n  if !(ok1 && ok2 && ok3 && ok4 && ok5) then IO.Process.exit 1\n' "$(BUILD_DIR)/ir_gate" "$(BUILD_DIR)/ir_gate" "$(BUILD_DIR)/ir_gate" "$(BUILD_DIR)/ir_gate" "$(BUILD_DIR)/ir_gate" "$(BUILD_DIR)/ir_gate" | \
		LEAN_PATH=$(OLEAN_DIR) $(LEAN) -DwarningAsError=true --stdin
	@! grep -Eq '\b(u64|i64)\b' $(BUILD_DIR)/ir_gate/synth_arena.wgsl || \
		(echo "ERROR: Forbidden 64-bit WGSL type emitted by Flow.Schema!" >&2 && exit 1)
	@if command -v naga >/dev/null 2>&1; then \
		naga $(BUILD_DIR)/ir_gate/synth_arena.wgsl $(BUILD_DIR)/ir_gate/synth_arena.spv && rm -f $(BUILD_DIR)/ir_gate/synth_arena.spv; \
	fi
	@./$(BUILD_DIR)/flow verify $(BUILD_DIR)/ir_gate/synth_bundle.flow > /dev/null
	@printf '#include "flow_ir.h"\n#include "synth_arena_abi.h"\n#include "synth_bundle_abi.h"\nint cpp20_probe(void) { return (int)sizeof(synth_pkt_t) + (int)sizeof(synth_arena_t) + (int)sizeof(synth_bundle_abi_t) + (int)sizeof(flow_ir_prog_t); }\n' | \
		$(CXX) -x c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -iquote . -I$(BUILD_DIR)/ir_gate -c - -o $(BUILD_DIR)/ir_gate/cpp_probe.o
	@printf '%s\n' \
		'#include "flow_ir.h"' \
		'#include "synth_arena_abi.h"' \
		'#include "synth_bundle_abi.h"' \
		'#include <stdio.h>' \
		'static flow_ir_prog_t g_p;' \
		'static uint8_t g_buf[FLOW_IR_MAX_FILE_BYTES];' \
		'static uint8_t g_raw[SYNTH_BUNDLE_ABI_BUNDLE_BYTES];' \
		'static flow_cell g_arena[SYNTH_BUNDLE_ABI_CHART_COUNT];' \
		'int main(void) {' \
		'  synth_pkt_t pkt_in = {0xABU, 0x01U, 0x1234U, 0xCAFEBABEU, -424242, 0U, {UINT64_C(0x0123456789ABCDEF), 2ULL, 3ULL, 4ULL}, {-9876543210LL, -1LL}};' \
		'  uint8_t wire[64];' \
		'  synth_arena_pkt_pack_bytes(wire, &pkt_in);' \
		'  synth_pkt_t pkt_out = {0};' \
		'  synth_arena_pkt_unpack_bytes(wire, &pkt_out);' \
		'  if (pkt_out.tag_u8 != pkt_in.tag_u8 || pkt_out.flag_u16 != pkt_in.flag_u16 ||' \
		'      pkt_out.len_u32 != pkt_in.len_u32 || pkt_out.val_i32 != pkt_in.val_i32 ||' \
		'      pkt_out.seq_u64[0] != pkt_in.seq_u64[0] || pkt_out.acc_i64[1] != pkt_in.acc_i64[1]) return 1;' \
		'  synth_pkt_p_t pp_in = {{65, 66, 67, 68}, {-10, 20, -30, 40}, {1U, 2U, 3U, 4U}, 0U, {10ULL, 20ULL, 30ULL, 40ULL}};' \
		'  synth_arena_pkt_p_pack_bytes(wire, &pp_in);' \
		'  synth_pkt_p_t pp_out = {0};' \
		'  synth_arena_pkt_p_unpack_bytes(wire, &pp_out);' \
		'  if (pp_out.vals_i16[2] != -30 || pp_out.acc_u64[3] != 40ULL) return 12;' \
		'  FILE *fp = fopen("$(BUILD_DIR)/ir_gate/synth_bundle.flow", "rb");' \
		'  if (!fp || fread(g_raw, 1, sizeof(g_raw), fp) != sizeof(g_raw)) return 2;' \
		'  fclose(fp);' \
		'  for (size_t i = 0; i < SYNTH_BUNDLE_ABI_CHART_COUNT; ++i) flow_cell_from_bytes(g_arena[i], g_raw + (i + 1) * 64U);' \
		'  synth_bundle_abi_t b_out;' \
		'  if (!synth_bundle_abi_unpack(g_arena, SYNTH_BUNDLE_ABI_CHART_COUNT, &b_out)) return 3;' \
		'  if (b_out.alpha != 42 || b_out.beta != -123 || b_out.gamma != 777) return 4;' \
		'  g_arena[SYNTH_BUNDLE_ABI_TAG_CHART][7] ^= 1ULL;' \
		'  if (synth_bundle_abi_unpack(g_arena, SYNTH_BUNDLE_ABI_CHART_COUNT, &b_out)) return 5;' \
		'  if (!flow_ir_load_file(&g_p, "$(BUILD_DIR)/ir_gate/synth_cart.fir", g_buf, sizeof(g_buf))) return 6;' \
		'  if (g_p.mult != FLOW_IR_MULT_CARTESIAN || flow_ir_admits_unitary(&g_p) || g_p.num_ops != 1U) return 7;' \
		'  if (!flow_ir_load_file(&g_p, "$(BUILD_DIR)/ir_gate/synth_lin.fir", g_buf, sizeof(g_buf))) return 8;' \
		'  if (!flow_ir_admits_unitary(&g_p)) return 9;' \
		'  uint64_t inv_in[3] = {0x53ULL, 0x05ULL, 0ULL}, inv_out[1] = {0};' \
		'  if (!flow_ir_eval_u64(&g_p, inv_in, inv_out) || flow_ir__gf2m_mul(inv_out[0], 0x53ULL, 8, 0x11B) != 1ULL) return 10;' \
		'  uint64_t zero_in[3] = {0ULL, 0x05ULL, 0ULL}, zero_out[1] = {0};' \
		'  if (!flow_ir_eval_u64(&g_p, zero_in, zero_out) || zero_out[0] != 0x05ULL) return 11;' \
		'  return 0;' \
		'}' > $(BUILD_DIR)/ir_gate/ir_test.c
	@$(CC) $(SEARU_LIB_CFLAGS) -fno-jump-tables -mstack-probe-size=65536 -I. -I$(BUILD_DIR)/ir_gate -c $(BUILD_DIR)/ir_gate/ir_test.c -o $(BUILD_DIR)/ir_gate/ir_test.o
	@! objdump -d $(BUILD_DIR)/ir_gate/ir_test.o | grep -Eq '[[:space:]](blr|br|udiv|sdiv)[[:space:]]' || (echo "ERROR: Forbidden br/blr/div in flow_ir!" >&2 && exit 1)
	@$(CC) $(SEARU_LIB_CFLAGS) $(BUILD_DIR)/ir_gate/ir_test.o -o $(BUILD_DIR)/ir_gate/ir_test
	@$(BUILD_DIR)/ir_gate/ir_test
	@echo "================================================================================"
	@echo "   ALL FLOW v$(FLOW_VERSION) + SEARU VERIFICATION GATES PASSED"
	@echo "================================================================================"

install: $(OLEAN_DIR)/Flow.olean $(BUILD_DIR)/libsearu.a $(BUILD_DIR)/flow
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/include/flow $(DESTDIR)$(PREFIX)/lib/flow/Flow
	install -m 755 $(BUILD_DIR)/flow $(DESTDIR)$(PREFIX)/bin/flow
	install -m 644 flow_opt_params.h $(DESTDIR)$(PREFIX)/include/flow/flow_opt_params.h
	install -m 644 flow.h $(DESTDIR)$(PREFIX)/include/flow/flow.h
	install -m 644 searu.h $(DESTDIR)$(PREFIX)/include/flow/searu.h
	install -m 644 flow_ir.h $(DESTDIR)$(PREFIX)/include/flow/flow_ir.h
	install -m 644 $(BUILD_DIR)/libsearu.a $(DESTDIR)$(PREFIX)/lib/flow/libsearu.a
	install -m 644 Flow.lean $(DESTDIR)$(PREFIX)/lib/flow/Flow.lean
	install -m 644 $(OLEAN_DIR)/Flow.olean $(DESTDIR)$(PREFIX)/lib/flow/Flow.olean
	install -m 644 Flow/*.lean $(DESTDIR)$(PREFIX)/lib/flow/Flow/
	install -m 644 $(OLEAN_DIR)/Flow/*.olean $(DESTDIR)$(PREFIX)/lib/flow/Flow/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/flow
	rm -rf $(DESTDIR)$(PREFIX)/include/flow $(DESTDIR)$(PREFIX)/lib/flow

clean:
	rm -rf $(BUILD_DIR)
