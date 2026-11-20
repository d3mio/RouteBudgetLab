#!/usr/bin/env bash
# run_demo.sh - Fast verification and demonstration script for RouteBudget Lab
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${REPO_ROOT}"

echo "========================================================"
echo "  RouteBudget Lab — Quick Demonstration & Verification  "
echo "========================================================"

BIN_PROFILER="${REPO_ROOT}/build/reconcile_release/rbl_profile_partial_acceptance"
BIN_VALIDATOR="${REPO_ROOT}/build/reconcile_release/rbl_validate"

if [[ ! -f "${BIN_PROFILER}" || ! -f "${BIN_VALIDATOR}" ]]; then
  echo "Binaries missing. Building release targets..."
  cmake -B build/reconcile_release -DCMAKE_BUILD_TYPE=Release
  cmake --build build/reconcile_release -j4
fi

MAP="${REPO_ROOT}/tests/fixtures/recovery_cross.map"
SCEN="${REPO_ROOT}/tests/fixtures/recovery_cross.scen"
OUT_PATHS="/tmp/rbl_demo_paths.txt"
OUT_JSON="/tmp/rbl_demo_out.json"

echo "1. Running Prefix Resumption Policy on 2-Agent Crossing Fixture..."
"${BIN_PROFILER}" \
  --map "${MAP}" \
  --scen "${SCEN}" \
  --agents 2 \
  --budget 0.2 \
  --slice 0.01 \
  --policy RESUMPTION_NO_PA \
  --dump-final-paths "${OUT_PATHS}" \
  --output "${OUT_JSON}"

echo "2. Running Independent Plan Validator on Generated Paths..."
"${BIN_VALIDATOR}" \
  --map "${MAP}" \
  --scen "${SCEN}" \
  --agents 2 \
  --paths "${OUT_PATHS}" \
  --coords rc

echo ""
echo "========================================================"
echo "  DEMO SUCCESSFUL: 100% Validated Collision-Free Plan   "
echo "========================================================"

