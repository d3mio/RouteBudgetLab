#!/usr/bin/env bash
set -euo pipefail

# RouteBudget Lab - Reproducible Packaging Script
# Creates a versioned, self-contained tarball of RouteBudget Lab
# containing sources, benchmark scripts, documentation, viewer, and verified datasets.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCHIVE_DIR="${REPO_ROOT}/dist"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
ARCHIVE_NAME="RouteBudgetLab_Phase8_Final_${TIMESTAMP}.tar.gz"
ARCHIVE_PATH="${ARCHIVE_DIR}/${ARCHIVE_NAME}"

mkdir -p "${ARCHIVE_DIR}"

echo "================================================================================"
echo "ROUTEBUDGET LAB - REPRODUCIBLE PACKAGING"
echo "================================================================================"
echo "Target archive: ${ARCHIVE_PATH}"

tar --exclude='.git' \
    --exclude='build' \
    --exclude='*.pyc' \
    --exclude='__pycache__' \
    --exclude='.DS_Store' \
    --exclude='dist' \
    --exclude='results/phase7/verified_reference/baseline_tmp' \
    --exclude='results/phase7/verified_reference/raw' \
    -czf "${ARCHIVE_PATH}" \
    -C "${REPO_ROOT}" \
    CMakeLists.txt \
    README.md \
    IMPLEMENTATION_STATUS.md \
    src \
    tests \
    scripts \
    third_party \
    viewer \
    docs \
    data/sample \
    data/dataset_manifest.json \
    data/test \
    results/phase7/init_paths \
    results/phase7/verified_reference/verified_reference_manifest.json \
    results/phase7/verified_reference/paths \
    results/phase7/verified_statistical_analysis.json \
    results/phase8/replays

ARCHIVE_SIZE=$(ls -lh "${ARCHIVE_PATH}" | awk '{print $5}')
ARCHIVE_SHA256=$(shasum -a 256 "${ARCHIVE_PATH}" | awk '{print $1}')

echo "Archive successfully generated!"
echo "Size:   ${ARCHIVE_SIZE}"
echo "SHA256: ${ARCHIVE_SHA256}"
echo "Location: ${ARCHIVE_PATH}"
echo "================================================================================"
