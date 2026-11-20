#!/usr/bin/env bash
# verify_all.sh - Build Release and run all CTest (13/13) and Pytest (18/18) checks
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${REPO_ROOT}"

echo "1. Building Release target..."
cmake -S . -B build/reconcile_release -DCMAKE_BUILD_TYPE=Release
cmake --build build/reconcile_release -j4

echo "2. Running CTest (13/13)..."
ctest --test-dir build/reconcile_release --output-on-failure

echo "3. Running Pytest (18/18)..."
pytest tests/

echo "ALL CHECKS PASSED."
