#!/usr/bin/env bash
# reproduce_evaluation.sh - Reproduce unseen-map opportunity audit, confirmation runs, and summary CSVs
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${REPO_ROOT}"

cmake -S . -B build/reconcile_release -DCMAKE_BUILD_TYPE=Release
cmake --build build/reconcile_release --target rbl_profile_partial_acceptance rbl_validate -j4

python3 scripts/ieee_access_opportunity_audit.py
python3 scripts/ieee_access_confirmation_eval.py
python3 scripts/ieee_access_analyze_and_plot.py
