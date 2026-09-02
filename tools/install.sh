#!/bin/sh
set -eu

# FLOW One-Line Installer (Zero-dependency pure C build)
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/hianova/FLOW/main/tools/install.sh | sh
# Or locally:
#   ./tools/install.sh

PREFIX="${PREFIX:-$HOME/.local}"
BINDIR="$PREFIX/bin"
INCLUDEDIR="$PREFIX/include/flow"

CC="${CC:-clang}"
if ! command -v "$CC" >/dev/null 2>&1; then
    CC="gcc"
fi

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "Error: C compiler (clang or gcc) is required to build FLOW." >&2
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

echo "Building FLOW compiler (flowc) using $CC..."
mkdir -p "$TMPDIR/build"
CORE_SRCS=$(find "$SRCDIR/src" -maxdepth 1 -name "*.c" ! -name "flowc.c" ! -name "flowy_main.c")
"$CC" -std=c17 -Wall -Wextra -Wpedantic -O3 -pthread \
    $CORE_SRCS "$SRCDIR/src/flowc.c" -o "$TMPDIR/build/flowc" -lm

echo "Building FLOW introspective assistant (flowy) using $CC..."
"$CC" -std=c17 -Wall -Wextra -Wpedantic -O3 -pthread \
    $CORE_SRCS "$SRCDIR/src/flowy_main.c" -o "$TMPDIR/build/flowy" -lm

echo "Installing flowc and flowy to $BINDIR..."
mkdir -p "$BINDIR"
install -m 755 "$TMPDIR/build/flowc" "$BINDIR/flowc"
install -m 755 "$TMPDIR/build/flowy" "$BINDIR/flowy"

mkdir -p "$INCLUDEDIR"
install -m 644 "$SRCDIR"/src/*.h "$INCLUDEDIR/"

echo ""
echo "=========================================================="
echo "  FLOW Toolchain (flowc + flowy) successfully installed!"
echo "  Binaries: $BINDIR/flowc, $BINDIR/flowy"
echo "  Headers:  $INCLUDEDIR"
echo "=========================================================="
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
