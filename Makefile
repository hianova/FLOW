export PATH := $(HOME)/.elan/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC ?= clang
LEAN ?= lean
CLANG_FORMAT ?= clang-format
PREFIX ?= /usr/local

BUILD_DIR := build
FLOW_VERSION := $(shell tr -d '[:space:]' < VERSION)

CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2
override CFLAGS += -DFLOW_VERSION_STRING=\"$(FLOW_VERSION)\"

C_SOURCES := flow.h flow.c

.DEFAULT_GOAL := all
.PHONY: all fmt fmt-check install uninstall clean

all: $(BUILD_DIR)/Flow.olean $(BUILD_DIR)/flow

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

fmt:
	$(CLANG_FORMAT) -i $(C_SOURCES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)

$(BUILD_DIR)/Flow.olean: Flow.lean | $(BUILD_DIR)
	$(LEAN) -DwarningAsError=true -o $@ Flow.lean

$(BUILD_DIR)/flow: flow.c flow.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) flow.c -o $@
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof)(@|$$)' || \
		(echo "ERROR: Heap or float symbol leaked into universal flow engine!" && nm -u $@ && exit 1)

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
