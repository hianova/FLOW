#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "================================================================================"
echo "          FLOW LEVEL-5 AUTONOMOUS AUDIT & CRUCIBLE CONTEST RUNNER               "
echo "================================================================================"
echo "Inspecting and executing contest specifications from Level5-contest/flowy.rtf..."
echo ""

# Build and execute Crucible Verification via native Makefile target
make flowy-level5-crucible

echo ""
echo "CONTEST VERDICT: Level-5 Autonomous Self-Awareness & Double-Bind Survival AUDITED AND VERIFIED."
