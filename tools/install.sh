#!/bin/sh
set -eu

# FLOW One-Line Installer
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/hianova/FLOW/main/tools/install.sh | sh
# Or locally:
#   ./tools/install.sh

PREFIX="${PREFIX:-$HOME/.local}"
BINDIR="$PREFIX/bin"
LIBDIR="$PREFIX/lib"
INCLUDEDIR="$PREFIX/include/flow"

CC="${CC:-clang}"
if ! command -v "$CC" >/dev/null 2>&1; then
    CC="gcc"
fi
if ! command -v "$CC" >/dev/null 2>&1; then
    CC="cc"
fi

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "Error: C compiler (clang, gcc, or cc) is required to build FLOW." >&2
    exit 1
fi

TMPDIR=$(mktemp -d 2>/dev/null || mktemp -d -t 'flow-install')
cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

# Check if running from repository root or remote curl
if [ -f "src/flowc.c" ] && [ -f "src/flow.h" ]; then
    SRCDIR="."
else
    echo "Fetching FLOW source from github.com/hianova/FLOW..."
    git clone --depth 1 https://github.com/hianova/FLOW.git "$TMPDIR/flow"
    SRCDIR="$TMPDIR/flow"
fi

echo "=========================================================="
echo "  Installing FLOW to $PREFIX"
echo "  Compiler: $CC ($(command -v "$CC"))"
echo "=========================================================="

if [ "${FLOW_NO_MAKE:-0}" != "1" ] && command -v make >/dev/null 2>&1 && [ -f "$SRCDIR/Makefile" ]; then
    echo "Building and installing via Makefile..."
    make -C "$SRCDIR" CC="$CC" PREFIX="$PREFIX" install
else
    echo "Building FLOW core libraries and binaries using $CC..."
    mkdir -p "$TMPDIR/build/obj"
    mkdir -p "$BINDIR" "$LIBDIR" "$INCLUDEDIR"

    # Core library sources (exclude entrypoints flowc, flowy_main, and flowy)
    CORE_SRCS="src/abi.c src/backend.c src/bitspace.c src/cubical_hott.c src/flow_jet.c src/parser.c src/registry.c src/reload.c"
    
    for src in $CORE_SRCS; do
        obj="$TMPDIR/build/obj/$(basename "$src" .c).o"
        "$CC" -std=c17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$SRCDIR/src" -I"$SRCDIR/plugins" -c "$SRCDIR/$src" -o "$obj"
    done

    # Archive libflow.a
    AR="${AR:-ar}"
    RANLIB="${RANLIB:-ranlib}"
    "$AR" rcs "$LIBDIR/libflow.a" $TMPDIR/build/obj/*.o
    "$RANLIB" "$LIBDIR/libflow.a"

    # Archive libflow_rt.a (flow_jet, reload, cubical_hott)
    "$AR" rcs "$LIBDIR/libflow_rt.a" \
        "$TMPDIR/build/obj/flow_jet.o" \
        "$TMPDIR/build/obj/reload.o" \
        "$TMPDIR/build/obj/cubical_hott.o"
    "$RANLIB" "$LIBDIR/libflow_rt.a"

    # Build flowc
    "$CC" -std=c17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$SRCDIR/src" -I"$SRCDIR/plugins" \
        "$SRCDIR/src/flowc.c" "$LIBDIR/libflow.a" -o "$BINDIR/flowc" -lm

    # Build flowy
    "$CC" -std=c17 -Wall -Wextra -Wpedantic -O2 -pthread -I"$SRCDIR/src" -I"$SRCDIR/plugins" \
        "$SRCDIR/src/flowy_main.c" "$SRCDIR/src/flowy.c" "$LIBDIR/libflow.a" -o "$BINDIR/flowy" -lm

    # Headers
    install -m 644 "$SRCDIR"/src/*.h "$SRCDIR"/plugins/*.h "$INCLUDEDIR/"
fi

echo ""
echo "=========================================================="
echo "  FLOW Toolchain successfully installed!"
echo "  Binaries: $BINDIR/flowc"
echo "            $BINDIR/flowy"
echo "  Libraries: $LIBDIR/libflow.a"
echo "             $LIBDIR/libflow_rt.a"
echo "  Headers:  $INCLUDEDIR"
echo "=========================================================="
echo ""

# Verify installed binaries
if [ -x "$BINDIR/flowc" ]; then
    echo "Verifying flowc binary:"
    "$BINDIR/flowc" --version || true
fi
if [ -x "$BINDIR/flowy" ]; then
    echo "Verifying flowy binary:"
    "$BINDIR/flowy" --version || true
fi
echo ""

# Check if BINDIR is in PATH
case ":$PATH:" in
    *":$BINDIR:"*) ;;
    *)
        echo "Note: $BINDIR is not in your PATH."
        echo "Add it to your shell configuration (.bashrc, .zshrc, etc.):"
        echo "  export PATH=\"$BINDIR:\$PATH\""
        echo ""
        ;;
esac
